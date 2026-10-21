# MasterMind

AI-assisted mastering prototype in Python. It exists to **validate the DSP and
matching logic** before a port to a C++/JUCE plugin, so the DSP is modular,
GUI-free, block-based and documented with its JUCE counterparts.

> **Status: Phases 1–3 complete** (mastering chain, metering, presets + A/B).
> Reference matching (Phase 4) and AI assist (Phase 5) are next.

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
uv run mastermind-cli master mix.wav --preset hip-hop/trap   # auto-gain to the preset's LUFS
uv run mastermind-cli master mix.wav --target Spotify        # default chain fitted to -14 LUFS
uv run mastermind-cli presets [--search house] [--category Metal]
uv run mastermind-cli chain-template > chain.json    # edit, then: --chain chain.json
uv run mastermind-cli analyze master.wav [--json] [--target Spotify | --target-lufs -11]
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
    metering/             kweight, loudness (M/S/I/LRA/TP/PLR/crest), correlation
                          (+ per band, goniometer), spectrum, targets, bank, analysis
  engine/                 offline.py (render/export), realtime.py (playback, A/B,
                          loudness-matched monitoring), ringbuffer.py, commands.py
  presets/                schema.py (versioned pydantic models), factory_specs.py
                          (genre table → presets), store.py (library), factory/*.json
  matching/               loudness_fit.py (limiter gain → target LUFS)
  io/                     audio_file.py (load), export.py (16/24/32f WAV)
  gui/                    PySide6 + pyqtgraph; gui/meters/ = meter widgets
  cli.py, report.py
tools/                    build_factory_presets.py
tests/                    pytest: dsp/, metering/, presets/, engine/, cli/, gui/
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

### Metering (Phase 2)

All meter maths lives in `dsp/metering` and is shared by the live meters, the
full-file analysis and the CLI report.

| Meter | Implementation |
|---|---|
| Loudness | BS.1770-4 K-weighting (matches the published 48 kHz coefficients to 1e-15, re-derived for other rates). 100 ms hops → momentary (400 ms), short-term (3 s), gated integrated (-70 LUFS abs, -10 LU rel), **LRA** per EBU Tech 3342, max M/S |
| Peaks | True peak (BS.1770 4x), sample peak, PLR (TP − I), crest factor (whole file and 3 s) |
| Targets | Spotify -14, Apple -16, YouTube -14, Club -8, CD -9, Custom. Green within ±1 LU, amber within ±3 LU, red beyond. True peak turns red above the limiter ceiling |
| Correlation | ⟨LR⟩/√(⟨L²⟩⟨R²⟩) with 300 ms one-pole smoothing, 2 s peak-hold (minimum), per-band (<250 Hz, 250–2k, 2k–8k, >8k via LR4) with negative-band flags, mono-compatibility warning after > 1 s below 0 |
| Goniometer | M/S-rotated Lissajous (mono = vertical), auto-scaled |
| Spectrum | 8192-pt Hann FFT, power averaged over channels, ~250 ms ballistics, 1/3-octave smoothing on a 1/24-octave grid. A full-scale sine reads 0 dB and pink noise reads flat. Input and output are overlaid, plus a reference curve with variance band (filled in Phase 4) |
| Gain reduction | Multiband (4), glue, limiter |

The real-time engine runs an input (pre-chain) and an output (post-chain) meter
bank. A single toggle switches every meter between them, and the spectrum
always overlays both. Meters only run while playing, so pauses don't count as
silence, and **Reset** clears the integrated values. The **Analysis** tab
analyses the full input and an offline render of the current chain on a
background thread, then shows a stats table, short-term loudness over time and
the long-term average spectrum.

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
* Loudness meter vs **EBU Tech 3341**: cases 1–5 (steady tones and gating),
  9 (short-term constant), 12 (momentary constant), true-peak cases 15–18;
  **EBU Tech 3342** LRA cases 1–4; agreement with pyloudnorm within 0.1 LU;
  sample-rate and block-size independence. The EBU signals are regenerated
  from their published descriptions (tones get a 20 ms fade so the
  true-peak cases don't measure the onset discontinuity).
* Correlation reads +1 for identical channels, -1 for inverted channels and ≈ 0
  (|mean| < 0.02) for decorrelated noise. Also tested: smoothing time,
  peak-hold, the 1 s mono-warning threshold, and per-band isolation (only
  content above 8 kHz inverted → only the High band is flagged).
* Presets: all 35 requested genres ship, JSON is in sync with the generator,
  presets are pairwise distinct, genre character holds (e.g. Trap sub > Classic
  Rock sub by 5 dB+, Classical < Jazz < Folk < House loudness). Covered: search,
  favourites persistence, save / import / export / delete, version handling,
  rejection of invalid files.
* Loudness fit: on a full render, the fitted gain lands within 0.5 LU of the
  preset target with true peak under the ceiling. Unreachable targets are
  reported as not converged.
* A/B: B starts as a copy and is independent; queued edits keep their target;
  A, B and Original line up sample-for-sample; loudness matching turns an
  8 dB-louder chain down by 8 dB on the monitor only; switching is crossfaded.
* End-to-end CLI and offscreen GUI tests (load, play, edit, bypass, export,
  meter colour coding, input/output toggle, full-file analysis, preset
  browser, apply with auto-gain, save, A/B toolbar, reference curve).

## Presets (Phase 3)

A preset is versioned JSON with two parts:

* `chain`: exactly `MasteringChain.to_dict()` (module order, bypass, all parameters).
* `target`: the sound it aims for. `lufs`, `true_peak_ceiling`, `crest_db`,
  `lra_lu`, a 31-band 1/3-octave `spectrum` (dB, normalised to 0 dB power mean
  over 100 Hz–10 kHz, so it describes *shape*), per-band stereo `band_width`
  (%, low / low-mid / high-mid / high) and `mono_freq` (0 = off).

```json
{"format_version": 1, "id": "user/my-sound", "name": "My Sound", "category": "User",
 "description": "...", "tags": ["warm"], "chain": {"version": 1, "slots": [...]},
 "target": {"lufs": -9.0, "true_peak_ceiling": -1.0, "crest_db": 10.5, "lra_lu": 5.0,
            "spectrum": [31 values], "band_width": [0, 100, 120, 125], "mono_freq": 110}}
```

**Factory presets (35):** 10 Electronic, 5 Hip-Hop, 4 Pop, 4 Rock, 4 Metal and
8 Other. They're generated from one table in
`mastermind/presets/factory_specs.py`, whose module docstring documents where the
numbers come from (genre loudness norms, the shape of commercial-music
spectra, stereo conventions). Treat them as informed starting points. They
were not measured from a reference corpus; Phase 4 learns from real references.

**Adding or editing a factory preset:** add or edit a `GenreSpec` entry, then run
`uv run python tools/build_factory_presets.py`. A test fails if the shipped
JSON and the generator disagree, and another checks that all presets stay
distinct.

**User presets** live in `$MASTERMIND_HOME/presets` (default
`~/.mastermind`). Favourites are stored in `favorites.json` in the same folder.
In the GUI's **Presets** tab you can:

* search by name, genre or tags
* filter by category (or show only favourites)
* star favourites
* apply, save the current settings, import, export, or delete (user presets only)

Imports are validated, and an imported preset never overwrites a factory one.

**Apply + auto-gain:** applying a preset loads its chain into the active A/B
slot, sets the meter target to its LUFS and overlays its spectral curve on the
analyser. With auto-gain on, it then fits the limiter gain on a background
thread so *this* track hits the preset loudness. The fit renders up to six 5 s
excerpts, runs the pre-limiter part of the chain once, and uses a secant
search. On a 90 s mix it takes about 5 s and lands within 0.2 LU of target
for the whole file. The CLI does the same with `--preset` or `--target`.

**Format versions:** `format_version` is checked on load. Older versions go
through `presets.schema.migrate` and newer ones are rejected.

## A/B comparison (Phase 3)

The engine runs chain **A**, chain **B** (created as a copy of A on first use)
and the unprocessed **Original** every block, all aligned to the largest chain
latency. Switching between them is instant and crossfaded. Edits always go to
the active slot, and edits still queued keep their target even if you switch
mid-edit. **Copy A→B / B→A** duplicates settings.

**Match loudness** measures each path's recent gated loudness (6 s, BS.1770)
and turns the monitored path down to the quietest one, slewing at most
12 dB/s. The toolbar shows each path's LUFS and the applied monitor gain. This
gain affects *monitoring only*: the meters show the real output and exports
never include it.

## Adding a module

1. Subclass `mastermind.dsp.base.Processor`, set `type_id` / `display_name`,
   declare `param_specs()`, and implement `_prepare()` and `process()`
   (plus `latency_samples` if non-zero).
2. Decorate it with `@registry.register` and import it in `dsp/modules/__init__.py`.
3. The GUI parameter panel is generated from the ParamSpecs automatically.
4. Add a block-size-invariance case to `tests/dsp/test_chain.py`.

## JUCE port notes

See [docs/juce_port_map.md](docs/juce_port_map.md) for the module-by-module mapping.
