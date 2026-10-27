# Benchmark

Benchmark (working name) is a mix-referencing plugin. Put it on your master bus, load commercial reference tracks, and compare your mix against them inside your DAW:

- switch instantly between your mix and a loudness-matched reference;
- loop and mark the sections you want to compare;
- see where your mix differs in tonal balance, stereo width and dynamics, in plain language;
- solo a frequency band on both, to compare bass with bass and so on.

Made by [Exnilo](https://exnilo.co.uk), whose other plugins include Nil2Max. The interface uses the same look: navy panels, pill controls, and the cyan and violet brand gradient. Mix is shown in cyan and Reference in pink.

Formats: **VST3**, **AU** (macOS) and **Standalone**, built with C++20, JUCE 8 and CMake.

## Using it

1. **Setup.** Put Benchmark on your **master bus** as the last plugin. Bypass or remove it before you bounce or export (see *Loudness matching*).
2. **References.** Drag up to 12 tracks (WAV, AIFF, FLAC or MP3) onto the window, or click **Add references**.
   - They decode and convert to the session's sample rate in the background, with progress shown in the list. They are converted again if the session rate changes.
   - Click a reference to make it active. Right-click for *Locate file*, *Show in file browser* and *Remove*.
   - Missing files stay in the list in red with their path kept. Click one to find it again.
3. **Mix / Reference.** Click the big switch, press the **space bar**, or automate the *Mix / Reference* parameter. The switch is a 15 ms equal-power crossfade, so it doesn't click.
4. **Loudness matching** (the **Match** switch). Whichever source is louder is turned down so both play at the same integrated loudness (ITU-R BS.1770-4). Nothing is ever turned up.
   - Your mix is measured all the time, whichever source you are listening to. Silence is ignored. Click **Reset** to measure again after you change your mix or move to another section.
   - References are measured once when they load.
5. **Reference Transport.** Shows the waveform of the active reference with a playhead.
   - **Click** to jump. **Drag** to make a loop region, then drag its edges to resize it or its middle to move it. **Loop** turns looping on and off.
   - **Markers 1–3** store up to three loops per reference. Click an empty marker to save the current loop, and click a filled one to jump back to it. Right-click to overwrite or clear.
   - **Sync** (default): the reference stays at the same song position as your mix. It follows your DAW's play/stop, position and cycle jumps, so switching always lands on the same moment. If the host gives no position, it plays freely instead.
   - **Free**: the reference plays on its own, with play/pause, seeking and loops. Use it to audition a reference section that isn't lined up with your song.
   - Jumps, loop points and pause/resume use 5 ms crossfades.
6. **Align to session** (Sync mode). Use this when a reference is a version of a song that is also in your session.
   - Click **Align to session**, then play the session where that song plays. Benchmark listens for 15 s and finds where the excerpt sits in the reference to the exact sample, so the two then play in step.
   - The result shows a match percentage. If no confident match is found, it says so.
   - Right-click the button to clear the alignment. The alignment is saved per reference.
7. **Comparison.** Analyses your mix and the playing reference side by side, always before any matching gain. Each tab has notes on the right, for example *"Mix has +2.5 dB more 200-500 Hz (low-mids) than the reference"*.
   - **Tonal balance**: 1/3-octave smoothed spectra, each normalised to its own loudness, so you compare tone rather than level. The difference plot below shows where the mix has more or less. Hover for exact values.
   - **Stereo width**: the side-energy share and L/R correlation of each source, in four bands.
   - **Dynamics**: PLR (sample peak minus integrated loudness), crest factor, loudness range (LRA), and 60 s of short-term loudness.
   - **Average** builds up from **Reset** and ignores silence. **Live** follows roughly the last 1.5 s. Loop a reference section and play the matching part of your mix for a like-for-like comparison.
8. **Band solo** (header bar, or keys **1–4**, with **0** for off). Solos Low (< 150 Hz), Low-mid (150 Hz–1 kHz), High-mid (1–5 kHz) or High (> 5 kHz) on whatever is playing, Mix or Reference.
   - **Zero latency** (default): Linkwitz-Riley 24 dB/oct crossovers. They add no latency, but bands overlap around the crossover frequencies and the phase shifts near them.
   - **Linear phase**: steep, phase-accurate FIR bands that sum back exactly. They add about **43 ms of latency**, which is reported to your DAW for delay compensation. In this mode the latency stays constant whether a band is soloed or not, so playback never jumps in time. Switching modes fades the output briefly, and some DAWs only apply the new latency after you stop the transport.

### Current behaviour worth knowing

- **Loudness matching can turn your mix down.** When your mix is louder than the reference, Benchmark lowers your mix rather than raising the reference, so it never boosts. Leave it active while you export and the export will be quieter. Bypass or remove it first.
- **References follow your session by default (Sync).** Projects saved before this change keep the mode they were saved with. In Free mode, references keep playing while you listen to your mix unless you pause them.
- **Matching uses each reference's whole-track loudness**, not the loop region.
- **The analysis only covers audio that has played.** The reference side resets when you pick a different reference.
- **The AU is built and validated in CI** (`auval` and pluginval). The project was developed on Linux, so the macOS and Windows builds have only been checked by CI, not in a DAW.
- **Memory:** references are held decoded in memory, at about 11.5 MB per stereo minute at 48 kHz. Above 1.5 GB in total, the least recently used references are unloaded and reload when selected.

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

If your DAW doesn't show the AU after you install it, run `killall -9 AudioComponentRegistrar`, then check it with `auval -v aufx Bnch Exnl`.

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

## Architecture

```
source/
  PluginProcessor, PluginEditor, Parameters   plugin wiring, parameters (APVTS), state
  dsp/        real-time code: LoudnessMeter (BS.1770-4), KWeighting, GainMatcher, Crossfader,
              ReferencePlayer (seek/loop/sync with de-clicking), BandSolo (LR4 + linear-phase FIR)
  reference/  ReferenceLibrary (list, loops, markers, alignment, state), ReferenceLoader (thread pool),
              ReferenceDecoder, Resampler (polyphase windowed sinc), Aligner (GCC-PHAT + refinement),
              WaveformOverview
  analysis/   AnalysisEngine (thread + FIFOs), SpectralAnalyser (tonal + stereo), DynamicsAnalyser,
              HintGenerator (plain-language notes)
  ui/         Theme, ABToggle, ReferenceListPanel, WaveformView, LoudnessPanel, AnalysisPanel, BandSoloBar
  util/       RealtimeHandoff (hazard-pointer object handoff), TransportState (atomics), StereoFifo (SPSC)
tests/        Catch2 tests, plus an offscreen UI snapshot test ("[.snapshot]")
```

**Threads.** The audio thread never allocates, locks, logs or touches files.
- **Decoding:** reference files decode and resample on a background thread pool, and each finished reference reaches the audio thread through a lock-free hazard-pointer handoff. Replaced references are freed later on the message thread.
- **Commands:** transport commands (seek, loop, play) and the Sync offset are atomics.
- **Analysis:** audio for analysis goes through wait-free FIFOs to a low-priority analysis thread, and the UI copies snapshots from it.
- **Linear-phase filters:** these load on the message thread. JUCE's convolution crossfades to the new filter.
- **Alignment capture:** filled by the audio thread into a buffer allocated beforehand, then analysed on its own thread.

**Testing.** Run `ctest` (or `BenchmarkTests`). The tests cover:
- the loudness meter, against EBU Tech 3341 cases 1–5 and 9 at 44.1, 48, 96 and 192 kHz, and Tech 3342 LRA cases 1–4;
- loudness-matching gain maths, the crossfade, the resampler (passband accuracy, aliasing, odd sample rates) and the lock-free primitives (including multithreaded stress tests);
- the decoder and library (missing files, memory budget, state), the transport (Free/Sync with a simulated host playhead), the analysers (pink and white noise, stereo cases, dynamics) and the hint wording;
- band-solo filters (flat sum, band rejection, latency), alignment (sample-accurate, rejects non-matches, end to end), and processor state restored from a background thread.

## Identifiers

Manufacturer: Exnilo (`Exnl`). Plugin code: `Bnch`. Bundle ID: `uk.co.exnilo.benchmark`. Website: https://exnilo.co.uk. Hosts use these codes to identify the plugin in saved sessions, so fix them before release. If Nil2Max uses a different manufacturer code, use the same one here, so both plugins appear under one vendor.
