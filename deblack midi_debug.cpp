
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace std;

static constexpr uint8_t MIN_VELOCITY = 5;
static constexpr size_t BUFFER_SIZE = 64 * 1024;

struct Stats {
    uint64_t events = 0;
    uint64_t noteOn = 0;
    uint64_t kept = 0;
    uint64_t removed = 0;
    uint64_t bytesRead = 0;
};

static auto started = chrono::steady_clock::now();

static double elapsed() {
    return chrono::duration<double>(
        chrono::steady_clock::now() - started
    ).count();
}

static string formatTime(double seconds) {
    uint64_t s = static_cast<uint64_t>(seconds);
    ostringstream out;
    out << setfill('0')
        << setw(2) << s / 3600 << ":"
        << setw(2) << (s / 60) % 60 << ":"
        << setw(2) << s % 60;
    return out.str();
}

static void progress(
    const Stats& s, size_t track, size_t totalTracks,
    const string& filename, bool force = false
) {
    static auto last = chrono::steady_clock::now();
    auto now = chrono::steady_clock::now();

    if (!force &&
        chrono::duration_cast<chrono::milliseconds>(
            now - last
        ).count() < 200)
        return;

    last = now;

    cout << "\r\033[2K"
         << "[" << formatTime(elapsed()) << "] "
         << "Track " << track << "/" << totalTracks
         << " | Events: " << s.events
         << " | Notes kept: " << s.kept
         << " | Notes removed: " << s.removed
         << " | " << filename << flush;
}

class Reader {
    ifstream& in;
    uint64_t remaining;

public:
    Reader(ifstream& stream, uint64_t length)
        : in(stream), remaining(length) {}

    uint64_t left() const {
        return remaining;
    }

    bool byte(uint8_t& b) {
        if (!remaining) return false;

        char c;
        if (!in.get(c)) return false;

        b = static_cast<uint8_t>(
            static_cast<unsigned char>(c)
        );
        --remaining;
        return true;
    }

    bool bytes(vector<uint8_t>& out, uint64_t n) {
        if (n > remaining || n > SIZE_MAX)
            return false;

        out.resize(static_cast<size_t>(n));

        if (n && !in.read(
            reinterpret_cast<char*>(out.data()),
            static_cast<streamsize>(n)
        )) return false;

        remaining -= n;
        return true;
    }

    bool vlq(uint32_t& value) {
        value = 0;

        for (int i = 0; i < 4; ++i) {
            uint8_t b;
            if (!byte(b)) return false;

            value = (value << 7) | (b & 0x7F);

            if (!(b & 0x80))
                return true;
        }

        return false;
    }
};

static void writeByte(ostream& out, uint8_t b) {
    out.put(static_cast<char>(b));
}

static void writeBE16(ostream& out, uint16_t n) {
    writeByte(out, static_cast<uint8_t>(n >> 8));
    writeByte(out, static_cast<uint8_t>(n));
}

static void writeBE32(ostream& out, uint32_t n) {
    writeByte(out, static_cast<uint8_t>(n >> 24));
    writeByte(out, static_cast<uint8_t>(n >> 16));
    writeByte(out, static_cast<uint8_t>(n >> 8));
    writeByte(out, static_cast<uint8_t>(n));
}

static bool readBE16(ifstream& in, uint16_t& n) {
    char x, y;

    if (!in.get(x) || !in.get(y))
        return false;

    uint8_t a = static_cast<uint8_t>(
        static_cast<unsigned char>(x)
    );
    uint8_t b = static_cast<uint8_t>(
        static_cast<unsigned char>(y)
    );

    n = (uint16_t(a) << 8) | b;
    return true;
}

static bool readBE32(ifstream& in, uint32_t& n) {
    uint8_t b[4];
    char c;

    for (auto& v : b) {
        if (!in.get(c)) return false;
        v = static_cast<uint8_t>(
            static_cast<unsigned char>(c)
        );
    }

    n = (uint32_t(b[0]) << 24) |
        (uint32_t(b[1]) << 16) |
        (uint32_t(b[2]) << 8) |
        uint32_t(b[3]);

    return true;
}

static void writeVLQ(ostream& out, uint32_t value) {
    uint8_t b[4];
    int n = 0;

    b[n++] = static_cast<uint8_t>(value & 0x7F);

    while ((value >>= 7) != 0)
        b[n++] = static_cast<uint8_t>(
            0x80 | (value & 0x7F)
        );

    while (n--)
        writeByte(out, b[n]);
}

static bool writeDelta(
    ostream& out, uint64_t delta,
    uint64_t& insertedEvents
) {
    while (delta > 0x0FFFFFFF) {
        writeVLQ(out, 0x0FFFFFFF);
        writeByte(out, 0xFF);
        writeByte(out, 0x7F);
        writeByte(out, 0x00);

        delta -= 0x0FFFFFFF;
        ++insertedEvents;
    }

    writeVLQ(out, static_cast<uint32_t>(delta));
    return bool(out);
}

struct TrackResult {
    uint64_t length = 0;
    uint64_t keptNotes = 0;
    bool hasTempoMap = false;
    bool ok = false;
};

static TrackResult filterTrack(
    ifstream& input,
    uint32_t trackLength,
    const fs::path& tempPath,
    Stats& stats,
    size_t trackIndex,
    size_t totalTracks,
    const string& filename
) {
    TrackResult result;
    Reader r(input, trackLength);

    ofstream out(tempPath, ios::binary | ios::trunc);
    if (!out) return result;

    uint8_t runningStatus = 0;
    uint64_t pendingDelta = 0;
    uint64_t insertedEvents = 0;

    array<array<uint32_t, 128>, 16> active{};

    while (r.left() > 0) {
        uint32_t delta = 0;
        if (!r.vlq(delta)) return result;

        pendingDelta += delta;

        uint8_t first;
        if (!r.byte(first)) return result;

        uint8_t status;
        uint8_t data1 = 0;
        bool hasFirstData = false;

        if (first < 0x80) {
            if (runningStatus < 0x80 ||
                runningStatus >= 0xF0) {
                cerr << "\nInvalid running status in track "
                     << trackIndex << "\n";
                return result;
            }

            status = runningStatus;
            data1 = first;
            hasFirstData = true;
        } else {
            status = first;
        }

        vector<uint8_t> event;
        event.reserve(16);

        bool keep = true;
        bool isNoteOn = false;
        bool isNoteOff = false;

        uint8_t channel = 0;
        uint8_t pitch = 0;
        uint8_t velocity = 0;

        if (status >= 0x80 && status <= 0xEF) {
            runningStatus = status;
            channel = status & 0x0F;

            int kind = status & 0xF0;
            int count =
                (kind == 0xC0 || kind == 0xD0) ? 1 : 2;

            if (!hasFirstData && !r.byte(data1))
                return result;

            uint8_t data2 = 0;
            if (count == 2 && !r.byte(data2))
                return result;

            if (data1 >= 0x80 ||
                (count == 2 && data2 >= 0x80))
                return result;

            event.push_back(status);
            event.push_back(data1);
            if (count == 2) event.push_back(data2);

            pitch = data1;
            velocity = data2;

            isNoteOn = (kind == 0x90 && velocity != 0);
            isNoteOff = (
                kind == 0x80 ||
                (kind == 0x90 && velocity == 0)
            );

            if (isNoteOn) {
                ++stats.noteOn;

                if (velocity < MIN_VELOCITY) {
                    keep = false;
                    ++stats.removed;
                } else {
                    ++stats.kept;
                    ++result.keptNotes;
                    ++active[channel][pitch];
                }
            } else if (isNoteOff) {
                if (active[channel][pitch] == 0) {
                    keep = false;
                } else {
                    --active[channel][pitch];
                }
            }
        } else if (status == 0xFF) {
            // Meta events do not replace the previous
            // channel running status in this SMF parser.
            uint8_t type;
            if (!r.byte(type)) return result;

            uint32_t len;
            if (!r.vlq(len)) return result;

            vector<uint8_t> payload;
            if (!r.bytes(payload, len)) return result;

            // MIDI tempo event: FF 51 03 tt tt tt
            // Keep the entire track if it contains tempo data,
            // even if all of its notes were filtered out.
            if (type == 0x51 && len == 3)
                result.hasTempoMap = true;

            event.push_back(0xFF);
            event.push_back(type);

            uint8_t enc[4];
            int en = 0;
            uint32_t v = len;

            enc[en++] = static_cast<uint8_t>(v & 0x7F);
            while ((v >>= 7) != 0)
                enc[en++] = static_cast<uint8_t>(
                    0x80 | (v & 0x7F)
                );

            while (en--)
                event.push_back(enc[en]);

            event.insert(
                event.end(),
                payload.begin(),
                payload.end()
            );
        } else if (status == 0xF0 || status == 0xF7) {
            runningStatus = 0;

            uint32_t len;
            if (!r.vlq(len)) return result;

            vector<uint8_t> payload;
            if (!r.bytes(payload, len)) return result;

            event.push_back(status);

            uint8_t enc[4];
            int en = 0;
            uint32_t v = len;

            enc[en++] = static_cast<uint8_t>(v & 0x7F);
            while ((v >>= 7) != 0)
                enc[en++] = static_cast<uint8_t>(
                    0x80 | (v & 0x7F)
                );

            while (en--)
                event.push_back(enc[en]);

            event.insert(
                event.end(),
                payload.begin(),
                payload.end()
            );
        } else {
            runningStatus = 0;

            int count = 0;
            switch (status) {
                case 0xF1:
                case 0xF3:
                    count = 1;
                    break;

                case 0xF2:
                    count = 2;
                    break;

                case 0xF6:
                case 0xF8:
                case 0xF9:
                case 0xFA:
                case 0xFB:
                case 0xFC:
                case 0xFD:
                case 0xFE:
                    count = 0;
                    break;

                default:
                    return result;
            }

            event.push_back(status);

            for (int i = 0; i < count; ++i) {
                uint8_t b;
                if (!r.byte(b)) return result;
                event.push_back(b);
            }
        }

        ++stats.events;

        if (keep) {
            if (!writeDelta(out, pendingDelta, insertedEvents))
                return result;

            out.write(
                reinterpret_cast<const char*>(event.data()),
                static_cast<streamsize>(event.size())
            );

            if (!out) return result;
            pendingDelta = 0;
        }

        progress(stats, trackIndex, totalTracks, filename);
    }

    if (pendingDelta) {
        if (!writeDelta(out, pendingDelta, insertedEvents))
            return result;
    }

    // Ensure every retained track ends with End Of Track.
    writeByte(out, 0x00);
    writeByte(out, 0xFF);
    writeByte(out, 0x2F);
    writeByte(out, 0x00);

    out.flush();
    if (!out) return result;
    out.close();

    result.length = fs::file_size(tempPath);
    result.ok = result.length <= UINT32_MAX;
    return result;
}

static bool writeEmptyTrack(const fs::path& path) {
    ofstream out(path, ios::binary | ios::trunc);
    if (!out) return false;

    writeByte(out, 0x00);
    writeByte(out, 0xFF);
    writeByte(out, 0x2F);
    writeByte(out, 0x00);

    out.flush();
    return bool(out);
}

static bool processFile(
    const fs::path& src,
    const fs::path& dest,
    const fs::path& tempDir
) {
    started = chrono::steady_clock::now();
    Stats stats;

    ifstream in(src, ios::binary);
    if (!in) {
        cerr << "\nCannot open: " << src << "\n";
        return false;
    }

    char magic[4];
    if (!in.read(magic, 4) ||
        string(magic, 4) != "MThd") {
        cerr << "\nInvalid MIDI header: " << src << "\n";
        return false;
    }

    uint32_t headerLength;
    if (!readBE32(in, headerLength) || headerLength < 6)
        return false;

    uint16_t format, tracks, division;
    if (!readBE16(in, format) ||
        !readBE16(in, tracks) ||
        !readBE16(in, division))
        return false;

    if (format > 2 || tracks == 0 ||
        (division & 0x8000)) {
        cerr << "\nUnsupported MIDI format or time division.\n";
        return false;
    }

    vector<uint8_t> extraHeader(headerLength - 6);
    if (!extraHeader.empty() &&
        !in.read(
            reinterpret_cast<char*>(extraHeader.data()),
            static_cast<streamsize>(extraHeader.size())
        ))
        return false;

    vector<fs::path> tempFiles;
    vector<uint32_t> trackLengths;

    cout << "\n\nDeBlack MIDI: " << src.filename() << "\n"
         << "Velocity threshold: " << int(MIN_VELOCITY) << "\n"
         << "Empty-track removal: enabled\n"
         << "Tempo-map preservation: enabled\n";

    size_t removedTracks = 0;
    size_t retainedTempoTracks = 0;

    for (uint16_t i = 0; i < tracks; ++i) {
        char id[4];
        uint32_t len;

        if (!in.read(id, 4) ||
            string(id, 4) != "MTrk" ||
            !readBE32(in, len)) {
            cerr << "\nInvalid track chunk.\n";
            return false;
        }

        fs::path tmp = tempDir /
            (src.filename().string() + ".track" +
             to_string(i) + ".tmp");

        TrackResult res = filterTrack(
            in, len, tmp, stats, i + 1, tracks,
            src.filename().string()
        );

        if (!res.ok) {
            cerr << "\nTrack processing failed: "
                 << i + 1 << "\n";
            return false;
        }

        // Keep a track if it contains retained notes OR
        // original tempo-map events.
        if (res.keptNotes > 0 || res.hasTempoMap) {
            tempFiles.push_back(tmp);
            trackLengths.push_back(
                static_cast<uint32_t>(res.length)
            );

            if (res.hasTempoMap && res.keptNotes == 0) {
                ++retainedTempoTracks;
                cout << "\n[KEEP TEMPO TRACK " << (i + 1)
                     << "] No notes, but tempo-map data exists.\n";
            } else {
                cout << "\n[KEEP TRACK " << (i + 1)
                     << "] Notes: " << res.keptNotes
                     << (res.hasTempoMap
                         ? " | Tempo map preserved"
                         : "")
                     << "\n";
            }
        } else {
            ++removedTracks;

            error_code ec;
            fs::remove(tmp, ec);

            cout << "\n[REMOVE TRACK " << (i + 1)
                 << "] No retained notes or tempo map.\n";
        }
    }

    // A MIDI file needs a track even if no notes and no
    // tempo events survived the filtering.
    if (tempFiles.empty()) {
        fs::path emptyTrack = tempDir /
            (src.filename().string() + ".empty.tmp");

        if (!writeEmptyTrack(emptyTrack)) {
            cerr << "\nCannot create fallback empty track.\n";
            return false;
        }

        tempFiles.push_back(emptyTrack);
        trackLengths.push_back(4);

        cout << "\n[INFO] No tracks with notes or tempo data. "
             << "Writing one valid empty track.\n";
    }

    if (tempFiles.size() > UINT16_MAX) {
        cerr << "\nToo many output tracks.\n";
        return false;
    }

    fs::path assembling = dest;
    assembling += ".tmp";

    ofstream out(assembling, ios::binary | ios::trunc);
    if (!out) return false;

    out.write("MThd", 4);
    writeBE32(
        out,
        static_cast<uint32_t>(6 + extraHeader.size())
    );
    writeBE16(out, format);
    writeBE16(out, static_cast<uint16_t>(tempFiles.size()));
    writeBE16(out, division);

    if (!extraHeader.empty()) {
        out.write(
            reinterpret_cast<const char*>(extraHeader.data()),
            static_cast<streamsize>(extraHeader.size())
        );
    }

    for (size_t i = 0; i < tempFiles.size(); ++i) {
        out.write("MTrk", 4);
        writeBE32(out, trackLengths[i]);

        ifstream track(tempFiles[i], ios::binary);
        if (!track) return false;

        array<char, BUFFER_SIZE> buffer;

        while (track) {
            track.read(buffer.data(), buffer.size());
            streamsize got = track.gcount();

            if (got > 0)
                out.write(buffer.data(), got);
        }

        if (!track.eof() || !out)
            return false;
    }

    out.flush();
    if (!out) return false;
    out.close();

    for (const auto& p : tempFiles) {
        error_code ec;
        fs::remove(p, ec);
    }

    error_code renameError;
    fs::rename(assembling, dest, renameError);

    if (renameError) {
        error_code removeError;
        fs::remove(dest, removeError);

        renameError.clear();
        fs::rename(assembling, dest, renameError);
    }

    if (renameError) {
        cerr << "\nCannot finalize output: "
             << renameError.message() << "\n";
        return false;
    }

    progress(stats, tracks, tracks, src.filename().string(), true);

    cout << "\n\nCompleted in " << formatTime(elapsed())
         << "\nNote-on events: " << stats.noteOn
         << "\nNotes kept:     " << stats.kept
         << "\nNotes removed:  " << stats.removed
         << "\nTracks before:  " << tracks
         << "\nTracks removed: " << removedTracks
         << "\nTempo-only tracks retained: "
         << retainedTempoTracks
         << "\nTracks output:  " << tempFiles.size()
         << "\nOutput: " << dest << "\n";

    return true;
}

int main() {
    fs::path base = "/sdcard/Download/DeBlackMIDI";
    fs::path inputDir = base / "input";
    fs::path outputDir = base / "output";
    fs::path tempDir = base / "temp";

    try {
        fs::create_directories(inputDir);
        fs::create_directories(outputDir);
        fs::create_directories(tempDir);
    } catch (const exception& e) {
        cerr << "Directory error: " << e.what() << "\n";
        return 1;
    }

    vector<fs::path> files;

    for (const auto& entry : fs::directory_iterator(inputDir)) {
        if (!entry.is_regular_file()) continue;

        string ext = entry.path().extension().string();

        transform(
            ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) {
                return static_cast<char>(tolower(c));
            }
        );

        if (ext == ".mid" || ext == ".midi")
            files.push_back(entry.path());
    }

    sort(files.begin(), files.end());

    if (files.empty()) {
        cout << "No MIDI files found.\n"
             << "Put .mid/.midi files in:\n"
             << inputDir << "\n";
        return 0;
    }

    cout << "DeBlack MIDI C++ | Streaming mode\n"
         << "Files: " << files.size() << "\n"
         << "RAM-friendly track-by-track processing\n"
         << "Empty-track removal: enabled\n"
         << "Tempo-map preservation: enabled\n";

    size_t success = 0;

    for (const auto& file : files) {
        fs::path output = outputDir / file.filename();

        if (fs::exists(output)) {
            output = outputDir /
                (file.stem().string() + "_deblack" +
                 file.extension().string());
        }

        if (processFile(file, output, tempDir))
            ++success;
        else
            cerr << "\nFAILED: " << file.filename() << "\n";
    }

    cout << "\nAll done: " << success << "/"
         << files.size() << " files processed.\n";

    return success == files.size() ? 0 : 1;
}
