# BreakForge

A drum sequencer, pattern generator and sampler built only for **drum and bass**: two-step, rollers, jungle breaks, halftime, liquid, neuro, jump-up and minimal. VST3 instrument (plus AU on macOS and a standalone app), built with JUCE 8 and CMake.

BreakForge focuses on two things: **groove** (swing, velocity, timing feel) and **sound** (a procedurally synthesized DnB kit plus your own samples).

## Features

- **Sequencer**
  - 8 renameable tracks: Kick, Snare, Ghost Snare, Closed Hat, Open Hat, Ride, Perc, FX.
  - 16 pattern slots, 16 or 32 steps each, at 1/16 or 1/32 resolution.
  - Sample-accurate host sync with no drift.
  - Per step: velocity, probability, ratchet (1–4), nudge (±50 %), pitch (±12 st) and length.
- **Pattern switching from MIDI**: notes **C1–D#2** (36–51, C3 = 60) select patterns 1–16, in *Latch*, *Hold* or *Off* mode. Notes C3–G3 (60–67) play tracks 1–8 directly.
- **Generator**
  - One-click, rule-based DnB patterns for 7 sub-styles.
  - Kick and snare anchors follow DnB conventions: snare on 2 and 4, or on 3 for halftime.
  - A Variation control sets how far it strays from the core pattern.
  - Per-track Gen and Lock, a one-bar Fill, and a ghost-note helper.
- **Groove engine**
  - Swing 50–75 % on 16ths (optionally 8ths), computed in beats so it is tempo-relative, with per-track swing override.
  - Groove templates (Straight, Light Shuffle, Heavy Swing, Amen Feel, Rolling, plus your own).
  - Seeded humanize for timing (ms) and velocity (%).
  - An accent row, Velocity Depth, and per-track velocity→filter and velocity→pitch.
- **Sampler**
  - Per track: volume, pan, pitch ±24 st, fine tune, start offset, One-Shot or ADSR envelope, low- and high-pass filter with resonance, and choke groups (the closed hat chokes the open hat by default).
  - Load WAV, AIFF or FLAC by drag-and-drop or the file browser.
  - Kit folders, per-track randomize and sample lock.
- **Built-in kits** (Clean, Neuro, Jungle), synthesized at the host sample rate on first load.
- **Output**
  - Stereo main out, or multi-out with one stereo bus per track.
  - MIDI out of the sequence.
  - Drive saturation and a transparent look-ahead limiter on the master.
  - **Drag-and-drop export** of the pattern as MIDI, a WAV mix or WAV stems. Exports are rendered offline at the host tempo and rate, with swing and humanize applied, and loop seamlessly.
- **Factory patterns**: Two-Step, Rolling Two-Step, Amen Break, Halftime, Jungle Break, Minimal Liquid.

## Building

Requirements: CMake ≥ 3.22, a C++20 compiler (Xcode 14+, Visual Studio 2022, or GCC 11+/Clang 14+), and Git. JUCE 8.0.15 and Catch2 are fetched automatically.

```bash
# macOS (universal binary: VST3, AU, Standalone)
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release

# Windows (VST3, Standalone), from a "Developer PowerShell for VS 2022"
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

# Linux (VST3, Standalone)
sudo apt install libasound2-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev \
                 libxcomposite-dev libfontconfig1-dev libfreetype-dev libgl1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Useful options:

| Option | Effect |
| --- | --- |
| `-DBREAKFORGE_COPY_PLUGIN=ON` | Copy the plugin into your system plug-in folder after each build |
| `-DBREAKFORGE_BUILD_TESTS=OFF` | Skip the unit tests |
| `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE` | Build against a local JUCE checkout (likewise `..._CATCH2`) |

Built plugins end up in `build/BreakForge_artefacts/Release/{VST3,AU,Standalone}`.

## Installing

Copy the built bundle into your plug-in folder, then rescan plug-ins in your DAW:

| Format | macOS | Windows | Linux |
| --- | --- | --- | --- |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` | `C:\Program Files\Common Files\VST3\` | `~/.vst3/` |
| AU | `~/Library/Audio/Plug-Ins/Components/` | — | — |

Load BreakForge as an **instrument**. Press play in the DAW: the sequencer follows the host's tempo, transport and bar position. Pattern slot 1 holds the Two-Step factory pattern.

## Using it

- **Grid**
  - Click a step to toggle it.
  - Shift-drag paints steps; Alt-drag paints velocity.
  - Right-click a step to edit probability, ratchet, nudge, pitch and length.
  - The lane under the selected track edits velocities by dragging bars.
  - The orange row at the top is the accent row.
- **Piano roll** (Piano Roll button)
  - Drag notes off-grid to set timing; they hop to the next step past half a step. Hold Shift for fine control.
  - Drag a note's right edge to change its length, and double-click to add or remove notes.
- **Generate** builds a pattern in the selected sub-style and sets a groove that suits it.
  - **Lock** a track to keep it, or use a track's **Gen** to re-roll just that track.
  - **Fill** writes a one-bar fill into the next slot.
- **Arranging in the DAW**: put MIDI notes C1–D#2 on the BreakForge track to switch patterns. *Hold* mode plays a pattern only while its note is held.
- **Export**: press the **Drag MIDI** handle and drag it onto a DAW track. The arrow picks MIDI, audio mix or stems, and the length in bars.
- **Undo/redo**: Ctrl/Cmd+Z and Ctrl/Cmd+Shift+Z. Copy and Paste put patterns on the clipboard.

## Loading your own samples

Every track's sample slot (next to the track name) accepts:

- **drag and drop** of a WAV, AIFF, FLAC or OGG file — anywhere on the track's header row works;
- **click → Load sample…** to use the file browser;
- **click → Kit sounds** to choose any sound of the current kit for that role.

### A different sample on each hit

Drop an audio file onto **a step** (in the grid or the piano roll) and just that hit plays it; the
rest of the track keeps the track's sample. The step is switched on if it was off, and a folded
corner on the step marks hits with their own sample.

- Drop **several files** at once and they fill consecutive steps in file-name order
  (`slice 1.wav`, `slice 2.wav` … `slice 10.wav`), which is handy for laying out a chopped break.
- **Right-click a step** to see which sample it plays, load a different one, or **Use track** to
  go back to the track's sample.
- Each track can hold up to 15 per-hit samples. Dropping the same file again reuses its slot, and
  slots that no step uses any more are recycled.
- Per-hit samples are saved with the project, used in audio exports, and if a file goes missing
  that hit plays the track's sample instead.

Files are decoded on a background thread and resampled to the host rate with a band-limited sinc resampler. Clips are capped at 30 s. The project stores the file path.

If a file has moved or can't be read, the slot turns yellow with a **!** and its tooltip explains why. The track keeps playing a built-in fallback sound, and the original path is still saved, so the sample comes back once the file returns.

**Lock sample** (in the slot menu) keeps a favourite sound when you change kits or press *Shuffle*.

## Adding kits

A kit is a folder of samples inside your **Kits folder**:

| OS | Kits folder |
| --- | --- |
| macOS | `~/Library/BreakForge/Kits/` |
| Windows | `%APPDATA%\BreakForge\Kits\` |
| Linux | `~/.config/BreakForge/Kits/` |

(Kit menu **… → Open Kits folder** creates and opens it. **Rescan** picks up new folders without reloading the plugin.)

Each sub-folder becomes a kit in the kit selector. BreakForge sorts the samples into drum roles using sub-folder names and file names, searching the file name first, then the folders above it:

| Role | Recognised words (case-insensitive) |
| --- | --- |
| Kick | `kick`, `bassdrum`, `bd`, `kik` |
| Snare | `snare`, `snr`, `sd`, `sn` |
| Ghost Snare | `ghost` (falls back to the kit's snares) |
| Closed Hat | `hat`, `hihat`, `hh`, `ch`, `chh`, `closed` |
| Open Hat | `open`, `openhat`, `oh`, `ohh` |
| Ride | `ride`, `cymbal`, `bell` |
| Perc | `perc`, `rim`, `clap`, `shaker`, `tom`, `conga`, `bongo`, `tamb`, `cowbell`, `clave`, `snap` |
| FX | `fx`, `sfx`, `crash`, `reverse`, `rev`, `riser`, `sweep`, `impact`, `noise` |

A layout like this works out of the box:

```
Kits/
  Dusty Breaks/
    Kicks/      BD_01.wav, BD_02.wav
    Snares/     Snare Tight.wav, ghost soft.wav
    Hats/       hh closed 1.wav, oh long.aif
    Ride.wav
    FX/         reverse cymbal.flac
```

Selecting a kit loads its first sample for each role into every unlocked track. **Shuffle** picks random sounds for each role from the current kit. The built-in kits (Clean, Neuro, Jungle) are always listed first.

User **groove templates** live next to the kits, in `…/BreakForge/Grooves/*.bfgroove`. Save and load them from the **…** button beside the groove selector.

## Tests and validation

```bash
ctest --test-dir build -C Release --output-on-failure   # or run the BreakForgeTests binary directly
scripts/pluginval.sh /path/to/pluginval build Release   # pluginval at strictness level 5
```

The suite (44 test cases) covers:
- swing timing maths and tempo independence;
- step scheduling across random block sizes and jittery host positions;
- generator output validity: every style, variation and length;
- MIDI export timing, offline rendering, the limiter ceiling and drive latency;
- sample loading, resampling, missing-file fallbacks and kit-folder classification;
- full plugin state save and restore.

Hidden helpers:
- `BreakForgeTests "[.render]"` with `BF_RENDER_DIR=…` writes every built-in sound to WAV.
- `BreakForgeTests "[.print]"` prints generated patterns as ASCII.

CI (`.github/workflows/build.yml`) builds macOS (universal, VST3 + AU + auval), Windows and Linux, runs the tests and runs pluginval at strictness 5.

## How it is built

- **Audio thread** (`Source/Core/Engine.cpp`, `Scheduler.cpp`, `Voice.cpp`, `MasterBus.cpp`): no allocation, locks or file I/O.
  - Patterns reach it through a lock-free triple buffer.
  - Samples reach it through atomically swapped, reference-counted buffers that are only ever freed on the message thread.
  - Parameters are read from cached APVTS atomics.
- **Scheduling**: each block computes the grooved time of every candidate step from the host's PPQ position and keeps the hits that round into the block. Ownership uses the same half-sample rounding as placement, and contiguous blocks are stitched together, so there's no drift and no doubled or dropped hits at block edges.
- **Background threads** decode and resample files, render the built-in kits, scan kit folders, generate patterns and render exports. Results are installed on the message thread.
- **Latency**: the drive's oversampler plus the limiter's 1.5 ms look-ahead. The total is reported to the host, kept constant when either is bypassed, and matched on the multi-out buses.

## Known limitations

- The standalone app has no transport of its own; it's meant for checking sounds and editing. Use the plugin inside a DAW to play patterns.
- Some hosts (for example Logic Pro) ignore MIDI output from AU instruments. Use the VST3 version, or the MIDI export, to drive other drum plugins there.
- Resolution (1/16 or 1/32) is set per pattern, not per track. Ratchets give fast rolls on a 1/16 pattern.

## Licence

BreakForge's own code, synthesized sounds and patterns are original. It is built on [JUCE](https://juce.com), which is dual-licensed (AGPLv3 or commercial): distributing builds requires complying with one of those licences.
