# SEAM by Exnilo

The transition engine for drum & bass. SEAM goes on the drum bus and handles the moments between sections:
fills, breakdowns and pre-drop builds. One idea, few controls, clean when off.

Windows 10/11 VST3 (plus a standalone app). Sibling of [Nil2Max](https://exnilo.co.uk/nil2max/): same family look,
same genre themes, same licensing and demo mode.

## What it does

- **Phrase tracking.** Reads the host's tempo, time signature and position, and counts down to the next *seam*
  (every 8, 16 or 32 bars) in the energy display.
- **TRANSITION (the hero knob).** 0% is a true bypass, bit for bit. Turning it up moves a drum-bus high-pass, a reverb
  throw on the snares, a beat-locked stutter and a ducking dip together. Pull it down and it snaps back clean on the
  next downbeat.
- **Fills.** Drop a breakbeat WAV on the fill panel. SEAM slices it into kicks, snares, ghosts and hats and plays a
  generated fill in the last 1, 2 or 4 bars before each seam: snare rolls ramping 1/16 to 1/32 triplets, reversed
  snares, ghost-note stutter chops and half-time switches. Liquid, Neuro, Jungle or Jump-Up; Complexity sets the
  density; Mix or Replace; New fill re-rolls it; DRAG MIDI exports it as a clip (C1 = slice 1). A built-in break means
  fills work before you drop anything.
- **Vacuum.** A silent 1/16, 1/8 or 1/4 gap hard-snapped to the seam's downbeat, with an optional tape-stop on the
  final beat.
- **Auto ramp.** TRANSITION rises by itself over the final 2/4/8/16 bars of each phrase (linear or late surge) and drops
  to zero on the seam.
- **Advanced panel** (ADV): scale Filter, Reverb, Stutter, Duck and Fill from 0 to 150%.
- **12 factory presets** across the four styles, plus your own (saved in `%APPDATA%\Exnilo\SEAM\Presets`).

Automatable: Transition, Style, Complexity, Fill length, Vacuum, Auto mode (and the rest). Stereo in/out, and mono in
to stereo out. Resizable 75-200%. No noise or tails when the transport is stopped.

## Build

```
cmake -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
scripts\run-pluginval.ps1 -Strictness 10          # Windows (scripts/run-pluginval.sh on Linux/macOS)
```

The first configure fetches JUCE 8.0.15 and Catch2 v3.16.1 (add `-DFETCHCONTENT_SOURCE_DIR_JUCE=<path>` and
`-DFETCHCONTENT_SOURCE_DIR_CATCH2=<path>` to use local checkouts). The VST3 is at
`build\seam\plugin\SEAM_artefacts\Release\VST3\SEAM.vst3`.

Licensing: pass `-DSEAM_LEMON_STORE_ID=<id> -DSEAM_LEMON_PRODUCT_ID=<id>` (from the Lemon Squeezy dashboard). Without
them the build is demo-only: it explains that activation is not set up in this build.

Installer: `ISCC.exe /DAppVersion=1.0.0 installer\SEAM.iss` (Inno Setup 6) writes
`installer\Output\SEAM-Windows-Setup.exe`. CI (`.github/workflows/build.yml`) builds Windows and Linux, runs every test
and pluginval at strictness 10, and uploads the installer, the VST3 zip and the pluginval logs.

## Layout

| Path | What |
|---|---|
| `shared/exnilo_rt` | Header-only, JUCE-free realtime helpers shared by Exnilo plugins: lock-free queue and exchange, PRNG, demo gate. |
| `shared/exnilo_ui` | JUCE module with the Exnilo family: genre themes, LookAndFeel, hero knob, energy display, widgets, scalable editor, Advanced panel, preset store, Lemon Squeezy licensing. |
| `seam/engine` | All of SEAM's audio and timing: phrase tracker, TRANSITION chain, vacuum, slicer, fill generator and player, MIDI export. Pure C++20, never JUCE (enforced by a test). |
| `seam/plugin` | The JUCE plugin: parameters, processor, editor, fill worker thread, presets. |
| `tests` | Catch2 engine tests, and a tool that renders the editor offscreen and checks presets, state and licence replies. |
| `installer` | Inno Setup script. |
| `docs/DECISIONS.md` | Every default and judgement call, with the reason. |
| `docs/TESTING.md` | What to listen for in a DAW. |
