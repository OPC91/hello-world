# Benchmark

Benchmark (working name) is a mix-referencing plugin. Put it on your master bus, load commercial reference tracks, and switch between your mix and a reference with one click or the space bar. The levels are loudness-matched so the comparison is fair.

Formats: **VST3**, **AU** (macOS) and **Standalone**, built with C++20, JUCE 8 and CMake.

> **Status:** Phase 4 of 5. Reference loading, A/B switching, loudness matching, the reference transport (waveform, seeking, loops, markers, Free/Sync) and the comparison analysis work. Band solo and alignment come in Phase 5.

## Using it

1. Put Benchmark on your **master bus**, as the last plugin.
2. Drag up to 12 reference tracks (WAV, AIFF, FLAC or MP3) onto the window, or click **Add references**. They decode and convert to your session's sample rate in the background. Click one to make it active.
3. Click **MIX / REFERENCE** or press the **space bar** to switch. The switch uses a 15 ms equal-power crossfade, so there are no clicks. The switch is also a host parameter ("Mix / Reference"), so you can automate it or map it to a controller.
4. With **Match** on, whichever source is louder is turned down so both play at the same integrated loudness (ITU-R BS.1770-4). Nothing is ever turned up. The panel shows what is being turned down and by how much.
   - Your mix is measured continuously, whichever source you are listening to. Silence is ignored. Click **Reset** to restart the measurement after you change the mix or move to a different section.
   - When the mix is the louder source, Benchmark turns your mix down. **Bypass or remove Benchmark before you bounce or export.**
5. The **Reference Transport** panel shows the active reference's waveform and playhead.
   - **Click** to jump. **Drag** to make a loop region, then drag its edges to resize it or its middle to move it. Turn looping on and off with **Loop**. Right-click for *Clear loop*, *Loop whole track* and saving to markers.
   - **Markers 1–3** store up to three loops per reference. Click an empty marker to save the current loop, and click a filled one to jump back to it. Right-click a marker to overwrite or clear it. Loops and markers are saved with the session.
   - **Free**: the reference plays on its own. Use the play/pause button, seeking and loops.
   - **Sync**: the reference follows your DAW's transport (play/stop, position and cycle jumps). It is silent outside the file and doesn't loop. If the host gives no position, Benchmark plays freely instead.
   - Jumps, loop wraps and pause/resume use 5 ms crossfades, so they don't click.
6. The **Comparison** panel analyses your mix and the playing reference side by side, always before any loudness-match gain. Each tab has plain-language notes on the right.
   - **Tonal balance**: 1/3-octave smoothed spectra of both, each normalised to its own loudness, so you compare tone rather than level. Below them is the difference: above the line, the mix has more in that range; below it, less. Hover to read exact values at any frequency.
   - **Stereo width**: for four bands (Low below 150 Hz, Low-mid up to 1 kHz, High-mid up to 5 kHz, High above that), the share of side energy and the L/R correlation for each source.
   - **Dynamics**: PLR (sample peak minus integrated loudness), crest factor, loudness range (LRA), and 60 s of short-term loudness history.
   - **Average** builds up from the last **Reset**, and silence is ignored, so a paused transport doesn't skew it. **Live** follows roughly the last 1.5 s. Loop a section of the reference and play the matching section of your mix for a like-for-like comparison. The reference side resets when you choose a different reference.
7. Missing reference files stay in the list in red. Click one to find the file again. Right-click a reference for more options.

Memory: references are stored decoded in memory, at about 11.5 MB per stereo minute at 48 kHz. Above 1.5 GB in total, the least recently used references are unloaded and reload when you select them.

## Requirements

| | macOS | Windows |
|---|---|---|
| OS | macOS 10.15+ to run, macOS 13+ to build | Windows 10/11 |
| Compiler | Xcode 15 or newer (Apple Clang) | Visual Studio 2022 (MSVC 17.x) with "Desktop development with C++" |
| CMake | 3.22+ (`brew install cmake`) | 3.22+ (bundled with VS 2022, or from cmake.org) |
| Other | Git | Git |

The first configure step downloads JUCE (pinned to 8.0.15) and Catch2. You need an internet connection, and it takes a few minutes.

## Building on macOS

```bash
git clone <this repo> benchmark && cd benchmark

# Configure (Release builds a universal arm64 + x86_64 binary)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build all formats and the tests
cmake --build build --config Release --parallel

# Run the unit tests
ctest --test-dir build -C Release --output-on-failure
```

To use an Xcode project instead, configure with `-G Xcode` and open `build/Benchmark.xcodeproj`.

Build outputs are in `build/Benchmark_artefacts/Release/`:

- `VST3/Benchmark.vst3` → copy to `~/Library/Audio/Plug-Ins/VST3/`
- `AU/Benchmark.component` → copy to `~/Library/Audio/Plug-Ins/Components/`
- `Standalone/Benchmark.app`

To have the build copy the plugins into place automatically, configure with `-DBENCHMARK_COPY_PLUGIN_AFTER_BUILD=ON`.

If your DAW doesn't show the AU after you install it, run `killall -9 AudioComponentRegistrar`, then check it with `auval -v aufx Bnch Cito`.

## Building on Windows

From a **Developer PowerShell for VS 2022** (or any shell with CMake on the PATH):

```powershell
git clone <this repo> benchmark; cd benchmark

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

You can also open the folder directly in Visual Studio, which detects `CMakeLists.txt`, or open `build/Benchmark.sln`.

Build outputs are in `build\Benchmark_artefacts\Release\`:

- `VST3\Benchmark.vst3` → copy to `C:\Program Files\Common Files\VST3\`
- `Standalone\Benchmark.exe`

`-DBENCHMARK_COPY_PLUGIN_AFTER_BUILD=ON` copies the VST3 into place automatically, but this needs an elevated (administrator) shell.

## Building on Linux (development and CI)

```bash
sudo apt install libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxrender-dev libgl1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build
```

## Validating with pluginval

Download [pluginval](https://github.com/Tracktion/pluginval/releases) and run:

```bash
pluginval --strictness-level 10 --validate-in-process --validate build/Benchmark_artefacts/Release/VST3/Benchmark.vst3
```

To render the editor to PNG files for a visual check: `BENCHMARK_SNAPSHOT_DIR=/some/dir ./build/tests/BenchmarkTests "[.snapshot]"`.

CI (`.github/workflows/ci.yml`) builds on Linux, macOS and Windows. It runs the unit tests and pluginval at strictness 10 on every push, and on macOS it also runs `auval`.

## CMake options

| Option | Default | Meaning |
|---|---|---|
| `BENCHMARK_BUILD_TESTS` | `ON` | Build the Catch2 test runner `BenchmarkTests` |
| `BENCHMARK_COPY_PLUGIN_AFTER_BUILD` | `OFF` | Install plugins into the user plugin folders after building |
| `BENCHMARK_JUCE_TAG` | `8.0.15` | JUCE version to fetch |
| `FETCHCONTENT_SOURCE_DIR_JUCE` | — | Path to a local JUCE checkout, so it isn't downloaded |

## Project layout

```
source/
  PluginProcessor, PluginEditor, Parameters   # plugin wiring only
  dsp/        real-time-safe audio code (crossfade, loudness, playback, crossovers)
  reference/  file loading, decoding, resampling, alignment (background threads)
  analysis/   spectrum / stereo / dynamics analysis (background thread)
  ui/         theme and components (original dark design, scales with the window)
  util/       lock-free FIFOs and cross-thread helpers
tests/        Catch2 unit tests
```

## Identifiers

Manufacturer: Citosoft (`Cito`). Plugin code: `Bnch`. Bundle ID: `com.citosoft.benchmark`. These are placeholders. Change them in `CMakeLists.txt` before release, because hosts use them to identify the plugin in saved sessions.
