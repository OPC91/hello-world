# MasterMind

AI-assisted mastering prototype in Python. It exists to **validate the DSP and
matching logic** before a port to a C++/JUCE plugin, so the DSP is modular,
GUI-free, block-based and documented with its JUCE counterparts.

> **Status: Phase 1 (mastering chain) complete.** Metering (Phase 2), presets
> (Phase 3), reference matching (Phase 4) and AI assist (Phase 5) are next.

## Setup

Requires Python ≥ 3.11 and [uv](https://docs.astral.sh/uv/).

```bash
uv sync                      # core + GUI + dev tools
uv sync --extra ai           # + onnxruntime (Phase 5)
uv sync --extra train        # + torch for training the classifier (large)
```

System libraries (Linux): `libportaudio2` for playback, `libegl1` for Qt.
Without an audio device the app still runs: playback falls back to a silent
real-time "null" output so the engine and meters keep working.

```bash
uv run mastermind [file.wav]                         # GUI
uv run mastermind-cli master mix.wav -o master.wav --gain 6 --ceiling -1 --bits 24
uv run mastermind-cli chain-template > chain.json    # edit, then: --chain chain.json
uv run mastermind-cli analyze master.wav [--json]
uv run pytest                                        # full suite (~30 s, GUI runs offscreen)
```

## Architecture

```
mastermind/
  dsp/                    PURE DSP: numpy / scipy / numba only. No Qt, no I/O.
    base.py               Processor, ParamSpec, ParamSet (smoothing), registry
    chain.py              MasteringChain: order, bypass (crossfaded, latency-compensated)
    primitives/           biquad, fir, oversampling, crossover (LR4), dynamics,
                          truepeak, delay, midside
    modules/              input_gain, eq, multiband_comp, saturation, imager,
                          glue_comp, limiter, dither
  engine/                 offline.py (render/export), realtime.py (playback),
                          ringbuffer.py, commands.py
  io/                     audio_file.py (load), export.py (16/24/32f WAV)
  gui/                    PySide6 + pyqtgraph
  cli.py, report.py
tests/                    pytest: dsp/, engine/, cli/, gui/
docs/juce_port_map.md     Python module -> JUCE class mapping
```

### Processing model

* Every processor works on stereo float64 blocks shaped `(2, n)` and implements
  `prepare(sample_rate, max_block)`, `process(block)`, `reset()` and
  `latency_samples`. This is the same contract as `juce::dsp::ProcessorBase`.
* **One code path:** offline rendering and real-time preview call the same
  `MasteringChain.process`. Processors are **block-size invariant**: a test
  renders a busy chain with 64-, 1000- and 4096-sample blocks and requires
  identical output.
* **Latency:** the chain reports total latency (linear-phase EQ, oversampling,
  limiter lookahead). Offline render pads and trims so output aligns
  sample-for-sample with input. Bypassed slots delay their dry path by the
  processor's latency, so bypass never shifts timing.
* **Bypass:** processors keep running while bypassed (state stays warm), and
  bypass crossfades over 20 ms. Once the fade finishes, output is bit-exact
  with the delayed input.

### Threads (real-time preview)

```
GUI thread ──commands──▶ CommandQueue ─┐
                                        ▼
engine thread:  source ─▶ chain.process ─▶ AudioRingBuffer ─▶ PortAudio callback (memcpy only)
                               └─▶ SnapshotQueue (meters) ─▶ GUI QTimer @ 30 fps
```

The audio callback never touches the GUI or the DSP. The DSP runs on a
dedicated engine thread about 4 blocks ahead, so GIL contention from the GUI
can't cause dropouts. The cost is about 90 ms of control latency at
1024/44.1k. All chain edits are posted as commands and applied at the top of
the next block.

### Phase 1 modules

| Module | Notes |
|---|---|
| Input gain | Smoothed gain + balance |
| Parametric EQ | 8 bands (bell / shelves / cuts / notch), per-band stereo / mid / side target. **Minimum-phase** (RBJ biquads, 0 latency) or **linear-phase** (FIR designed from the same magnitude response, 16383 taps @ 48k, FFT overlap-save, crossfaded updates) |
| Multiband compressor | 4 bands, LR4 crossovers with allpass compensation (sum is flat to < 0.01 dB), soft knee, per-band auto make-up, phase-aligned dry/wet |
| Saturation | Tape (emphasis + tanh), tube (biased tanh, even harmonics, DC-blocked), soft-clip (cubic). Unity small-signal gain, 1/2/4/8x oversampling, latency-compensated mix |
| Stereo imager | Per-band width (0–200 %) on the side channel via LR4 bands, with mid allpass-matched. Mono-maker = LR4 high-pass on side below *f* |
| Glue compressor | RMS detector, side-chain HPF, parallel mix |
| True-peak limiter | 4x oversampled detection with parabolic refinement, lookahead hold + box-smoothed gain (provably reaches target before the peak), release, sample-peak safety clip, 0.15 dB internal margin |
| Dither | TPDF with optional 1st-order noise shaping. **Export only**: pass-through in preview |

### Validation highlights (all in `tests/`)

* Crossovers: bands sum to 0 ± 0.01 dB at 44.1/48/96 kHz for several splits.
* Limiter never exceeds the ceiling, tested at 44.1/48/96 kHz, ceilings of -1 and
  -0.1 dBTP, and 0 or 18 dB of drive, on music-like signals, noise, sines up to 17 kHz,
  square waves, impulses, bursts and HF noise. It is measured two ways:
  * by our BS.1770 4x meter, on everything including full-band pathological signals
  * by an **independent** scipy polyphase interpolator at 4x *and* 8x, on
    programme-like (≤ 20 kHz) material

  On full-band white noise right up to Nyquist, the independent meter can read
  up to about +0.05 dB over. That is inherent to 4x detection, and BS.1770
  itself documents the same limitation.
* Linear-phase EQ magnitude matches the minimum-phase design within 0.25 dB
  (40 Hz–20 kHz), and its kernel is exactly symmetric.
* Dither: output lies on the integer grid, error variance = LSB²/4 (TPDF), and
  noise shaping tilts the noise floor upward.
* End-to-end CLI and an offscreen GUI test (load, play, edit, bypass, export).

## Adding presets

Preset files and the browser arrive in Phase 3. Until then, a chain is plain
JSON (`mastermind-cli chain-template`): `{"version": 1, "slots": [{"type",
"params", "bypass"}, ...]}`. The slot order is the processing order. Unknown
parameters are ignored and out-of-range values are clamped. Files with a newer
`version` are rejected.

## Adding a module

1. Subclass `mastermind.dsp.base.Processor`, set `type_id` / `display_name`,
   declare `param_specs()`, and implement `_prepare()` and `process()`
   (plus `latency_samples` if non-zero).
2. Decorate it with `@registry.register` and import it in `dsp/modules/__init__.py`.
3. The GUI parameter panel is generated from the ParamSpecs automatically.
4. Add a block-size-invariance case to `tests/dsp/test_chain.py`.

## JUCE port notes

See [docs/juce_port_map.md](docs/juce_port_map.md) for the module-by-module mapping.
