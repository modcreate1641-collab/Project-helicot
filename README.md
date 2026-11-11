# Project Helicot

## MIDI Master Synchronizer, BlackMIDI Merger & DeBlack MIDI

**Android-first MIDI processing utilities for Termux.** Project Helicot
brings together three independent command-line tools for MIDI timing
normalization, sequential Black MIDI part merging, and velocity-based
MIDI filtering.

This repository documents three separate programs:

-   **`MIDI_Master_Synchronizer_Combined.py`** --- batch-processes MIDI
    files in three predefined folders, maps event timing to a target
    BPM, normalizes notes to a fixed velocity, optionally forces a piano
    program and output channel, and generates JSON reports.
-   **`BlackMIDI Merger (cpp).cpp`** --- a C++17 command-line utility
    that discovers `PT<number>` MIDI parts, scans their note-event
    ranges, and concatenates usable parts sequentially into one Type 1
    MIDI file.
-   **`deblack midi_debug(1).cpp`** --- a C++17 command-line utility
    that filters note-on events below a configurable velocity threshold,
    removes tracks with no retained notes or tempo-map data, preserves
    tempo-map tracks, and reports processing progress.

> **Important:** These programs are independent tools. The Python
> synchronizer does not automatically invoke either C++ utility. The
> BlackMIDI Merger and DeBlack MIDI also do not automatically invoke one
> another.

## Contents

-   [Project overview](#project-overview)
-   [Features](#features)
-   [Tool 1 --- MIDI Master
    Synchronizer](#tool-1--midi-master-synchronizer)
-   [Tool 2 --- BlackMIDI Merger](#tool-2--blackmidi-merger)
-   [Tool 3 --- DeBlack MIDI](#tool-3--deblack-midi)
-   [Android and Termux installation](#android-and-termux-installation)
-   [Running the Python synchronizer](#running-the-python-synchronizer)
-   [Building and running the C++
    tools](#building-and-running-the-c-tools)
-   [Recommended workflow](#recommended-workflow)
-   [Project usage terms](#project-usage-terms)
-   [Limitations and troubleshooting](#limitations-and-troubleshooting)
-   [Raspberry Pi OS (optional)](#raspberry-pi-os-optional)
-   [Official references](#official-references)
-   [License](#license)

## Project overview

Project Helicot is an Android-first collection of MIDI processing
utilities intended to run locally through Termux. Each tool addresses a
different stage or type of MIDI processing:

  -----------------------------------------------------------------------
  Tool                    Primary purpose         Language
  ----------------------- ----------------------- -----------------------
  MIDI Master             Tempo conversion and    Python
  Synchronizer            MIDI event              
                          normalization           

  BlackMIDI Merger        Sequential              C++17
                          concatenation of        
                          numbered Black MIDI     
                          parts                   

  DeBlack MIDI            Velocity filtering and  C++17
                          removal of empty tracks 
  -----------------------------------------------------------------------

The tools can be used separately. Their output formats and processing
behavior should be checked in a MIDI player or DAW before using the
results in another stage of a workflow.

## Features

### Python synchronizer

The supplied source currently defines these defaults:

  ------------------------------------------------------------------------------------
  Setting                                                Default Effect
  --------------------------------- ---------------------------- ---------------------
  `TARGET_BPM`                                           `120.0` Target tempo used by
                                                                 BPM-lock conversion

  `FIXED_VELOCITY`                                          `90` Velocity assigned to
                                                                 note-on events when
                                                                 normalization is
                                                                 enabled

  `PIANO_PROGRAM`                                            `0` General MIDI program
                                                                 number used for the
                                                                 output program change

  `OUTPUT_CHANNEL`                                           `0` Output MIDI channel
                                                                 when channel mapping
                                                                 is enabled

  `ENABLE_BPM_LOCK`                                       `True` Converts event ticks
                                                                 using the source
                                                                 tempo map and target
                                                                 tempo

  `ENABLE_PIANO_MODE`                                     `True` Adds a piano program
                                                                 change on the output
                                                                 channel

  `ENABLE_FIXED_VELOCITY`                                 `True` Normalizes note-on
                                                                 velocity

  `ENABLE_CHANNEL_MAPPING`                                `True` Maps note events to
                                                                 the configured output
                                                                 channel

  `ENABLE_REMOVE_NON_NOTE_EVENTS`                         `True` Removes non-note
                                                                 events, including
                                                                 meta events during
                                                                 collection

  `ENABLE_TEMPO_CLEANING`                                 `True` Removes source tempo
                                                                 events when BPM lock
                                                                 is enabled

  `ENABLE_REPORT`                                         `True` Writes per-folder and
                                                                 global JSON reports
  ------------------------------------------------------------------------------------

The script processes these folders:

``` text
/sdcard/Download/
├── midi_input/
│   ├── folder_1/
│   ├── folder_2/
│   └── folder_3/
├── midi_fixed/
│   ├── folder_1/
│   ├── folder_2/
│   └── folder_3/
└── midi_reports/
    ├── folder_1/
    ├── folder_2/
    ├── folder_3/
    └── global_report.json
```

Input file extensions supported by the Python source: `.mid`, `.midi`,
`.MID`, and `.MIDI`.

**Note duration is not capped or deliberately shortened by this
script.** It maps note-event times according to its tempo conversion.
Its default cleanup removes other event types, so it is not intended to
preserve every original MIDI controller, instrument, lyric, marker, or
other metadata event.

### C++ BlackMIDI Merger

The supplied C++ source currently:

-   Uses C++17 features, including `std::filesystem`.
-   Creates its input, output, and temporary directories automatically.
-   Looks for `.mid` and `.midi` files directly inside the input
    directory.
-   Selects filenames containing `PT` followed by a part number;
    matching is case-insensitive and spaces, underscores, or hyphens
    before the number are accepted.
-   Sorts matching files by part number, then by filename.
-   Scans MIDI format 0 or 1 files and requires a compatible, positive
    ticks-per-beat division.
-   Skips files that contain no detected note events.
-   Merges the tracks within each source part by event time, then writes
    one output track for each usable part.
-   Places the parts sequentially, rather than aligning every part to
    the same starting time.
-   Writes to a temporary file, checks the output header, then renames
    it to the final destination.

Default paths in the supplied source:

``` text
/sdcard/Download/BlackMIDI_Merger/
├── input/
├── output/
│   └── BlackMIDI_Merged.mid
└── temp/
    └── merged.tmp
```

The merger does **not** perform the same BPM-lock, fixed-velocity, or
piano conversion as the Python script. It preserves many event bytes
from the source tracks, but it trims each part to the detected
note-event range and replaces the end-of-track event. See
[Limitations](#limitations-and-troubleshooting).

### C++ DeBlack MIDI

DeBlack MIDI is a separate C++17 utility for filtering low-velocity note
events from MIDI files. It is designed for batch processing files in a
local input directory and writing the processed versions to a separate
output directory.

The supplied source currently provides these behaviors:

-   Processes `.mid` and `.midi` files found directly inside the input
    directory; it does not scan nested subdirectories.
-   Uses `MIN_VELOCITY = 50` as the default threshold.
-   Removes note-on events whose velocity is lower than the configured
    threshold. A note-on event with velocity equal to or greater than
    the threshold is retained.
-   Retains corresponding note-off events for notes that were kept.
    Note-off events without a matching retained active note are
    discarded.
-   Removes tracks that contain neither retained notes nor a valid MIDI
    tempo-map event.
-   Preserves a track containing tempo-map data even if that track has
    no retained notes.
-   Creates a valid empty fallback track if no track with retained notes
    or tempo data remains.
-   Retains other parsed event types, including supported channel
    events, meta events, and SysEx events, subject to the parser's
    handling and validation.
-   Displays elapsed time, current track, event count, notes kept, notes
    removed, and the filename during processing.
-   Uses temporary track files and a temporary assembled output file
    rather than holding the entire processed MIDI file in memory.
-   Creates its input, output, and temporary directories automatically.

Default paths in the supplied source:

``` text
/sdcard/Download/DeBlackMIDI/
├── input/
├── output/
└── temp/
```

The output keeps the original filename unless a file with that name
already exists in the output directory. In that case, the program
appends `_deblack` before the extension.

**Important threshold detail:** the attached source sets `MIN_VELOCITY`
to `50`. To change the filtering threshold, edit this constant near the
beginning of the C++ source and recompile the program. The threshold is
a MIDI note velocity filter; it is not an analysis of perceived loudness
or SoundFont volume.

DeBlack MIDI is a direct event-filtering utility. It should not be
described as a complete musical reconstruction, intelligent arrangement,
or full Black MIDI-to-human-playable conversion system.

## Android and Termux installation

This project is designed for local command-line use on Android through
[Termux](https://github.com/termux/termux-app). A rooted phone is not
inherently required.

### 1. Install Termux from an official source

Choose one source:

-   **Termux on F-Droid:** https://f-droid.org/en/packages/com.termux/
-   **Official Termux GitHub repository and releases:**
    https://github.com/termux/termux-app

Install Termux and its optional plugins from the same distribution
source. APKs from different sources may use different signing keys and
may not install over one another. Read the [official Termux installation
instructions](https://github.com/termux/termux-app#installation).

### 2. Update packages

Open Termux and run:

``` bash
pkg update
pkg upgrade
```

### 3. Install the required tools

``` bash
pkg install python git clang
```

The Python script requires the `mido` package. Install it with:

``` bash
python -m pip install mido
```

Mido's official source and installation guidance:
https://github.com/mido/mido

### 4. Grant access to shared storage

``` bash
termux-setup-storage
```

Accept Android's permission prompt. The source code uses
`/sdcard/Download/...` paths; these are shared-storage paths, not paths
inside the Termux home directory.

Verify access:

``` bash
ls /sdcard/Download
```

If your device or Android configuration uses a different accessible
path, update the `INPUT_ROOT`, `OUTPUT_ROOT`, and `REPORT_ROOT`
constants in the Python script and the relevant base-directory constants
in the C++ sources. The directory paths are hard-coded in the supplied
versions.

## Running the Python synchronizer

### 1. Prepare the folder structure

Run:

``` bash
mkdir -p /sdcard/Download/midi_input/folder_1
mkdir -p /sdcard/Download/midi_input/folder_2
mkdir -p /sdcard/Download/midi_input/folder_3
```

Place the MIDI files to process inside the appropriate `folder_1`,
`folder_2`, or `folder_3` directory. The script creates output and
report directories automatically.

### 2. Put the script in an accessible location

For example, if the script is in Downloads:

``` bash
cd /sdcard/Download
```

Confirm the file exists:

``` bash
ls -l MIDI_Master_Synchronizer_Combined.py
```

### 3. Run it

``` bash
python MIDI_Master_Synchronizer_Combined.py
```

Review the console output. Generated MIDI files are written to:

``` text
/sdcard/Download/midi_fixed/folder_1/
/sdcard/Download/midi_fixed/folder_2/
/sdcard/Download/midi_fixed/folder_3/
```

Reports are written to:

``` text
/sdcard/Download/midi_reports/
```

The global report is `global_report.json`.

### Configuration

Edit the constants near the beginning of
`MIDI_Master_Synchronizer_Combined.py` before running it. For example,
changing `TARGET_BPM` changes the target tempo. Make a backup before
editing the script.

The script's `FOLDER_NAMES` list is currently fixed to `folder_1`,
`folder_2`, and `folder_3`; it does not recursively scan an arbitrary
directory tree.

## Building and running the C++ tools

Both C++ programs use C++17 and can be compiled with Termux's Clang
compiler. Compile each source separately because they are independent
programs with different entry points and processing logic.

### Tool 2 --- BlackMIDI Merger

#### 1. Prepare the input directory

``` bash
mkdir -p /sdcard/Download/BlackMIDI_Merger/input
mkdir -p /sdcard/Download/BlackMIDI_Merger/output
mkdir -p /sdcard/Download/BlackMIDI_Merger/temp
```

Place the source parts directly inside `input/`. Example names:

``` text
PT 1.mid
PT 2.mid
PT_3.mid
PT-4.midi
```

The part number is used for sorting. Files without a `PT<number>`
pattern are ignored. Files inside nested subdirectories are not
searched.

#### 2. Compile

Save the source as `BlackMIDI Merger (cpp).cpp` or use the filename
present in your checkout. From the directory containing the source, run:

``` bash
clang++ -O2 -std=c++17 \
  -include cstring -include cerrno \
  "BlackMIDI Merger (cpp).cpp" \
  -o blackmidi_merger
```

The explicit header flags accommodate the supplied source's use of
`std::strerror` and `errno` without directly including `<cstring>` and
`<cerrno>`. A cleaner source-level fix is to add those two includes and
then remove the `-include` flags.

If compilation fails, read the actual compiler diagnostic; do not assume
the executable was created.

#### 3. Run

``` bash
./blackmidi_merger
```

The program reports scan and merge progress. On success, the destination
is:

``` text
/sdcard/Download/BlackMIDI_Merger/output/BlackMIDI_Merged.mid
```

It also prints output size and elapsed time. These values depend on the
input files and device; no universal speed or maximum note count is
guaranteed.

### Tool 3 --- DeBlack MIDI

#### 1. Prepare the directories

``` bash
mkdir -p /sdcard/Download/DeBlackMIDI/input
mkdir -p /sdcard/Download/DeBlackMIDI/output
mkdir -p /sdcard/Download/DeBlackMIDI/temp
```

Place `.mid` or `.midi` files directly inside:

``` text
/sdcard/Download/DeBlackMIDI/input/
```

The program processes files in this directory only. It does not search
nested folders.

#### 2. Configure the velocity threshold (optional)

Open the source file and locate:

``` cpp
static constexpr uint8_t MIN_VELOCITY = 50;
```

Change `50` to the threshold you want, then save the source. The program
removes note-on events with velocity values **below** this number. For
example, with a threshold of `50`, velocities from `1` through `49` are
removed, while `50` and higher are retained.

The threshold is applied to MIDI event velocity values. It does not
measure perceived loudness, soundfont balance, or the volume produced by
a particular playback engine.

#### 3. Compile

Save the source as `deblack_midi.cpp` or use the filename present in
your checkout. For example:

``` bash
clang++ -O2 -std=c++17 \
  "deblack_midi.cpp" \
  -o deblack_midi
```

The supplied DeBlack source includes the standard headers it uses and
does not require an external MIDI library.

If compilation fails, inspect the compiler output before attempting to
run the executable.

#### 4. Run

``` bash
./deblack_midi
```

The program automatically creates the required directories and processes
every supported MIDI file directly inside `input/`.

Processed files are written to:

``` text
/sdcard/Download/DeBlackMIDI/output/
```

Temporary per-track files and assembly data are stored in:

``` text
/sdcard/Download/DeBlackMIDI/temp/
```

If the output filename already exists, the program uses a filename with
`_deblack` appended before the extension. The original input file is not
intentionally overwritten.

During processing, the console displays elapsed time, track progress,
event count, notes kept, notes removed, and the current filename. A
final summary reports note and track counts.

## Recommended workflow

1.  **Back up original MIDI files.**
2.  Test the Python synchronizer on a small MIDI file and verify its
    output in a MIDI player or DAW.
3.  If merging split parts, put the correctly named `PT<number>` files
    in the BlackMIDI Merger's input directory and run that utility
    separately.
4.  If filtering low-velocity events, place the intended MIDI files in
    the DeBlack MIDI input directory and confirm the configured
    `MIN_VELOCITY` value before running it.
5.  Validate each output's track count, tempo behavior, note timing, and
    playback in a MIDI player or DAW.
6.  Keep JSON reports and console output when investigating problems.

The tools are not an automatic pipeline. Choose the processing order
based on the source material and desired result, and inspect
intermediate files before continuing to another stage.

## Project usage terms

Project Helicot makes its source code publicly available so users can inspect the implementation, build the programs, and run them locally on their own devices. The following guidelines explain the project's intended use and attribution expectations.

1. **Local use and execution.** Users may download the publicly available source files, compile the programs, and run them on their own devices.
2. **Study and local modification.** Users may inspect the source code and make modifications for personal or internal use. Modified copies should be clearly distinguished from the original Project Helicot versions.
3. **Attribution when sharing.** If you share excerpts, modified versions, or other material derived from this project, identify Project Helicot as the original source where applicable and retain existing copyright or attribution notices.
4. **No false authorship claims.** Do not claim that you created the original Project Helicot project or its unmodified source code. Do not present a modified version as an official release.
5. **No misleading endorsement.** Do not use the Project Helicot name in a way that falsely suggests that a fork, derivative, or third-party release is official or endorsed by the original project maintainer.
6. **Respect third-party rights.** MIDI files, libraries, dependencies, and other third-party materials may have their own copyright and license terms. Processing a MIDI file does not remove those restrictions.
7. **Use MIDI files lawfully.** Only process, publish, or redistribute MIDI files that you created, have permission to use, or are otherwise legally entitled to use.
8. **Protect original data.** Keep backups and verify output paths before batch processing. Review source code before running builds obtained from untrusted locations.
9. **Responsible bug reports.** Include the device/OS version, command, relevant error message, and a minimal reproducible example. Do not upload copyrighted MIDI files or private data unless you have permission.

### License status

Project Helicot currently has **no formal software license**. These guidelines describe the project's intended use and attribution expectations; they are not an OSI-approved open-source license or a substitute for a formal `LICENSE` file. Public access to source code does not, by itself, grant unrestricted redistribution rights.

If a formal license is added in the future, its terms should be published in a `LICENSE` file in the repository. Third-party MIDI files and dependencies remain subject to their own terms.

These notes are project guidance, not legal advice.
## Limitations and troubleshooting

### Python synchronizer: event cleanup

With the default settings, non-note events are removed during
collection. This can remove controller events (such as sustain pedal),
instrument changes, lyrics, markers, and other metadata. The script then
writes a single-track Type 0 MIDI file and inserts a target tempo event
and, when enabled, a piano program change.

The code does not implement a note-duration cap. Its report
configuration explicitly records `duration_cap: false`.

### Python synchronizer: unmatched notes

The script reports unmatched note events. MIDI files with missing
note-off events, unusual overlapping repetitions of the same
pitch/channel, or malformed event pairs may need manual inspection. Do
not treat report counts alone as proof that a file is musically
identical to the original.

### C++ merger: sequential concatenation, not parallel alignment

The merger calculates each part's first and last detected note-event
ticks and appends the part after the preceding part's note duration. It
is therefore a **sequential concatenator**, not a tool for stacking all
parts at the same start time.

Events before a part's first detected note are shifted to that part's
start; events after its last detected note are not written. The final
end-of-track event is generated by the program. Source tempo events and
other retained meta events may coexist across output tracks; inspect
tempo behavior in your target DAW/player.

### C++ merger: input compatibility

The source accepts MIDI format 0 or 1, but rejects unsupported formats,
SMPTE-style time division, and input parts with differing ticks-per-beat
divisions. Files with no detected note events are skipped. The source
also limits the number of output tracks to 65,535 and rejects a track
payload exceeding the MIDI 32-bit track-length field.

### DeBlack MIDI: filtering behavior

The supplied source uses a default threshold of `50`. A note-on event is
removed when its velocity is below this value. Matching note-off events
are retained only for note-ons that survived filtering; note-off events
without a matching retained active note are discarded.

The program removes tracks with no retained notes and no valid tempo-map
event. Tracks containing a tempo-map event are retained even if they
contain no retained notes. If every track is removed, the program writes
one valid empty fallback track so that the output remains a valid MIDI
file.

The utility is a direct event filter. It does not intelligently identify
melody, harmony, musical importance, or human-playable arrangements. A
velocity threshold that works for one MIDI file may remove important
quiet notes from another, so review the output before treating it as a
finished arrangement.

### DeBlack MIDI: parser and format considerations

The supplied source accepts MIDI format values 0, 1, and 2, and rejects
SMPTE-style time division. It parses standard channel events, meta
events, SysEx events, and selected system-common/realtime events.
Malformed event data, invalid running status, unsupported system status
values, or truncated track data can cause processing to fail.

The source retains the original MIDI format value and time division in
the output. It writes processed track data through temporary files and
uses a temporary assembled output before finalizing the destination. A
failed run may leave temporary files in the `temp/` directory; inspect
the console error before deleting temporary data.

### Common issues

  -----------------------------------------------------------------------------------
  Symptom                                         Checks
  ----------------------------------------------- -----------------------------------
  `ModuleNotFoundError: No module named 'mido'`   Run `python -m pip install mido`

  Input folder appears empty                      Check the exact hard-coded path,
                                                  folder names, file extensions, and
                                                  Android storage permission

  C++ merger finds no parts                       Ensure files are directly in
                                                  `input/` and filenames contain `PT`
                                                  followed by a number

  `Different MIDI time divisions`                 Re-export source parts with
                                                  matching ticks-per-beat settings

  Compilation fails around `strerror` or `errno`  Use the merger compile command
                                                  above or add `<cstring>` and
                                                  `<cerrno>` to the source

  DeBlack reports `Invalid running status`        Check whether the input MIDI is
                                                  malformed or uses event encoding
                                                  not handled by the supplied parser

  DeBlack removes more notes than expected        Check `MIN_VELOCITY`; the supplied
                                                  default is `50`, and the comparison
                                                  removes values strictly below the
                                                  threshold

  DeBlack output has fewer tracks                 Tracks without retained notes or
                                                  tempo-map data are intentionally
                                                  removed

  Process is killed or the phone becomes          Reduce the test workload, close
  unresponsive                                    other intensive apps, keep Termux
                                                  in the foreground, and allow
                                                  adequate free storage

  Output plays differently                        Check retained tempo events, event
                                                  trimming, source timing, and the
                                                  target player's MIDI interpretation
  -----------------------------------------------------------------------------------

Android may terminate resource-intensive or background processes.
Keeping Termux in the foreground and avoiding simultaneous heavy
workloads may help, but does not guarantee that a long job will
complete.

## Raspberry Pi OS (optional)

Raspberry Pi is an optional Linux environment for testing these tools;
it is not required for the Android workflow. Compatibility and
performance must be verified on the exact board and OS image.

Official links:

-   **Raspberry Pi OS documentation:**
    https://www.raspberrypi.com/documentation/computers/os.html
-   **Raspberry Pi Imager downloads:**
    https://www.raspberrypi.com/software/
-   **Getting started / OS installation:**
    https://www.raspberrypi.com/documentation/computers/getting-started.html

Install Raspberry Pi OS using Raspberry Pi Imager on a compatible
computer and write it to the intended boot storage. **Writing an OS
image erases the selected storage device. Back up files and verify the
target device before writing.**

On Raspberry Pi OS, install common build tools:

``` bash
sudo apt update
sudo apt full-upgrade
sudo apt install python3 python3-pip git build-essential
```

For the Python synchronizer, install Mido if required:

``` bash
python3 -m pip install mido
```

Use a virtual environment if your OS's Python packaging policy
recommends it. The source files currently hard-code
`/sdcard/Download/...` paths, which are Android-specific; change the
paths for Raspberry Pi before running. The C++ sources also hard-code
their base directories. Raspberry Pi compatibility is not guaranteed
until tested.

## Official references

-   **Termux app / source / installation:**
    https://github.com/termux/termux-app
-   **Termux on F-Droid:** https://f-droid.org/en/packages/com.termux/
-   **Termux package documentation:**
    https://github.com/termux/termux-packages
-   **Mido Python MIDI library:** https://github.com/mido/mido
-   **Mido documentation:** https://mido.readthedocs.io/
-   **Raspberry Pi OS documentation:**
    https://www.raspberrypi.com/documentation/computers/os.html
-   **Raspberry Pi Imager:** https://www.raspberrypi.com/software/

## License

Project Helicot currently has **no formal software license**. Refer to [Project usage terms](#project-usage-terms) for the project's stated expectations around local use, modification, attribution, and sharing. Public source availability should not be interpreted as unrestricted permission to redistribute the code. A formal license can be added later if the project maintainer decides to grant broader permissions.
------------------------------------------------------------------------

**Project Helicot • Android-first • Termux-native workflow • MIDI timing
tools • Black MIDI part processing**
