# SEAM by Exnilo

The transition engine for drum & bass. SEAM goes on the drum bus and handles the moments between sections:
fills, breakdowns and pre-drop builds. One idea, few controls, clean when off.

Windows VST3 (plus a standalone app). Sibling of [Nil2Max](https://exnilo.co.uk/nil2max/).

## Build

```
cmake -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The first configure fetches JUCE 8.0.15 and Catch2 v3.16.1. To use local checkouts, add
`-DFETCHCONTENT_SOURCE_DIR_JUCE=<path> -DFETCHCONTENT_SOURCE_DIR_CATCH2=<path>`.

## Layout

| Path | What |
|---|---|
| `shared/exnilo_rt` | Header-only, JUCE-free realtime helpers shared by Exnilo plugins (lock-free queue and exchange, PRNG). |
| `shared/exnilo_ui` | JUCE module with the Exnilo family look: genre themes, LookAndFeel, hero knob, energy display, widgets, scalable editor. |
| `seam/engine` | All of SEAM's audio and timing logic. Pure C++20, never JUCE (enforced by a test). |
| `seam/plugin` | The JUCE plugin: parameters, processor, editor. |
| `tests` | Catch2 engine tests and an offscreen UI snapshot tool. |
| `docs/DECISIONS.md` | Every default and judgement call, with the reason. |
