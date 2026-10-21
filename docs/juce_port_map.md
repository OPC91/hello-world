# Python → JUCE port map

The `mastermind.dsp` package is written to be ported line by line. Conventions
that carry over:

* Stereo blocks `(2, n)` ↔ `juce::dsp::AudioBlock<float>` / `ProcessContextReplacing`.
* `prepare / process / reset / latency_samples` ↔ `ProcessorBase::prepare / process / reset`
  and `AudioProcessor::setLatencySamples`.
* numba `@njit` kernels (`smooth_gain_db`, `limiter_gain`, `_quantise_shaped`)
  are written as plain C-style loops on purpose: port them verbatim.
* Prototype precision is float64. The plugin will use float32, so check the
  deep-filter cases (LR4 at 20 Hz, shelves at 96 kHz); JUCE's TPT/SVF filters
  may be preferable there.

| Python | JUCE | Notes |
|---|---|---|
| `dsp.base.Processor` | `juce::dsp::ProcessorBase` | |
| `dsp.base.ParamSpec` / `ParamSet` | `RangedAudioParameter` + `AudioProcessorValueTreeState` | `smooth=True` → `juce::SmoothedValue<float, ValueSmoothingTypes::Linear>` (20 ms) |
| `dsp.chain.MasteringChain` / `Slot` | custom `std::vector<std::unique_ptr<ProcessorBase>>` or `AudioProcessorGraph` | Bypass crossfade + dry delay ≈ `juce::dsp::DryWetMixer` (has latency compensation) |
| `primitives.biquad` (`design`, `SOSFilter`) | `dsp::IIR::Coefficients::makePeakFilter / makeLowShelf / …`, `dsp::IIR::Filter` | Same RBJ cookbook formulas |
| `primitives.fir.linear_phase_from_magnitude` | `dsp::FFT` + `dsp::WindowingFunction` (Blackman-Harris) | Design off the audio thread |
| `primitives.fir.FIRConvolver` | `dsp::Convolution` (uniform partitioned) | JUCE crossfades IR swaps itself |
| `primitives.oversampling.Oversampler` | `dsp::Oversampling<float>` (FIR equiripple) | Report `getLatencyInSamples()` |
| `primitives.crossover.MultiBandSplitter` | `dsp::LinkwitzRileyFilter<float>` (LP/HP/AP) | Allpass compensation tree is identical |
| `primitives.dynamics.LevelDetector` / `CompressorCore` | `dsp::BallisticsFilter` + custom gain computer | Soft-knee formula from Giannoulis et al. 2012 |
| `primitives.truepeak.TruePeakDetector` | `dsp::Oversampling` (4x) + peak scan (+ parabolic refine for limiter) | |
| `primitives.delay.DelayLine` | `dsp::DelayLine<float, DelayLineInterpolationTypes::None>` | |
| `primitives.midside` | inline: `M=(L+R)/2, S=(L-R)/2` | |
| `modules.InputGain` | `dsp::Gain<float>` | |
| `modules.ParametricEQ` | IIR cascade / `dsp::Convolution` | Linear-phase latency changes with mode → `setLatencySamples` + host notification |
| `modules.MultibandCompressor` | LR tree + 4 × custom compressor | |
| `modules.Saturation` | `dsp::Oversampling` + `dsp::WaveShaper` / custom loop | |
| `modules.StereoImager` | LR filters on side, `SmoothedValue` widths | |
| `modules.GlueCompressor` | custom compressor + `DryWetMixer` | |
| `modules.TruePeakLimiter` | custom (`dsp::Limiter` lacks lookahead/TP) | Port `limiter_gain` loop as-is |
| `modules.Dither` | custom, `juce::Random` | Export path only |
| `engine.ringbuffer.AudioRingBuffer` | `juce::AbstractFifo` | Plugin won't need it (host drives `processBlock`) |
| `engine.ringbuffer.SnapshotQueue` | lock-free FIFO of meter structs + `juce::Timer` on the message thread | |
| `engine.commands` | APVTS atomics; structural edits via a FIFO consumed in `processBlock` | |
