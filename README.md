# Benchmark

Benchmark (working name) is a mix-referencing plugin. Put it on your master bus, load commercial reference tracks, and switch between your mix and a reference with one click or the space bar. The levels are loudness-matched so the comparison is fair.

Formats: **VST3**, **AU** (macOS) and **Standalone**, built with C++20, JUCE 8 and CMake.

> **Status:** Phase 1 of 5. The plugin builds, passes audio through unchanged, saves its state, and shows the UI layout. Loading references, A/B switching and loudness matching come in Phase 2.

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
