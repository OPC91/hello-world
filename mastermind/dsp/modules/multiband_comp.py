"""4-band compressor with Linkwitz-Riley (LR4) crossovers.

Signal flow::

    x -> MultiBandSplitter(f1, f2, f3) -> [band_k * gain_k] -> sum -> mix -> out
                                       `-> sum (unprocessed) = dry, phase-aligned

The bands sum to an allpass of the input, so with all bands at unity the
module is magnitude-transparent. The dry path for the mix control is the sum of
the *unprocessed* bands (same allpass phase), avoiding comb filtering in
parallel compression.

Auto make-up gain per band restores a -18 dBFS signal to its original level
(see :meth:`CompressorCore.auto_makeup_db`).

JUCE mapping: ``juce::dsp::LinkwitzRileyFilter`` tree + per-band
``juce::dsp::Compressor``-style gain computer (custom, to expose soft knee and GR).
"""

from __future__ import annotations

import numpy as np

from mastermind.dsp.base import ParamSpec, Processor, db_to_gain, registry
from mastermind.dsp.primitives.crossover import MultiBandSplitter
from mastermind.dsp.primitives.dynamics import CompressorCore

N_BANDS = 4
XOVER_DEFAULTS = (120.0, 1000.0, 6000.0)
XOVER_RANGES = ((20.0, 500.0), (200.0, 5000.0), (1500.0, 18000.0))
BAND_NAMES = ("Low", "Low-Mid", "High-Mid", "High")


def crossover_specs() -> list[ParamSpec]:
    return [
        ParamSpec(f"xover{i}", f"Crossover {i}", d, lo, hi, "Hz", log=True, group="Crossovers")
        for i, (d, (lo, hi)) in enumerate(zip(XOVER_DEFAULTS, XOVER_RANGES, strict=True), start=1)
    ]


def ordered_crossovers(params, n: int = 3) -> list[float]:
    """Crossover frequencies forced to be strictly increasing (>= 1/3 octave apart)."""
    freqs = [params.get(f"xover{i}") for i in range(1, n + 1)]
    for i in range(1, n):
        freqs[i] = max(freqs[i], freqs[i - 1] * 2 ** (1 / 3))
    return freqs


@registry.register
class MultibandCompressor(Processor):
    type_id = "multiband"
    display_name = "Multiband Compressor"

    @classmethod
    def param_specs(cls) -> list[ParamSpec]:
        specs = crossover_specs() + [
            ParamSpec("auto_makeup", "Auto Make-up", True, kind="bool"),
            ParamSpec("mix", "Mix", 100.0, 0.0, 100.0, "%", smooth=True),
            ParamSpec("output_db", "Output", 0.0, -24.0, 24.0, "dB", smooth=True),
        ]
        for i, name in enumerate(BAND_NAMES, start=1):
            g = f"Band {i} ({name})"
            specs += [
                ParamSpec(f"b{i}_threshold", "Threshold", 0.0, -60.0, 0.0, "dB", group=g),
                ParamSpec(f"b{i}_ratio", "Ratio", 2.0, 1.0, 20.0, ":1", log=True, group=g),
                ParamSpec(f"b{i}_attack", "Attack", 10.0, 0.1, 200.0, "ms", log=True, group=g),
                ParamSpec(f"b{i}_release", "Release", 150.0, 5.0, 2000.0, "ms", log=True, group=g),
                ParamSpec(f"b{i}_knee", "Knee", 6.0, 0.0, 24.0, "dB", group=g),
                ParamSpec(f"b{i}_makeup", "Make-up", 0.0, -12.0, 12.0, "dB", group=g),
                ParamSpec(f"b{i}_bypass", "Bypass", False, kind="bool", group=g),
            ]
        return specs

    def _configure(self) -> None:
        p = self.params
        self._split.set_frequencies(ordered_crossovers(p))
        for i, core in enumerate(self._cores, start=1):
            core.configure(
                p.get(f"b{i}_threshold"),
                p.get(f"b{i}_ratio"),
                p.get(f"b{i}_attack"),
                p.get(f"b{i}_release"),
                p.get(f"b{i}_knee"),
            )

    def _prepare(self) -> None:
        self._split = MultiBandSplitter(ordered_crossovers(self.params), self.sample_rate)
        self._cores = [CompressorCore(self.sample_rate) for _ in range(N_BANDS)]
        self._configure()
        self._meters = {f"gr_b{i}": 0.0 for i in range(1, N_BANDS + 1)}

    def process(self, block: np.ndarray) -> np.ndarray:
        if self.params.consume_changes() - {"mix", "output_db"}:
            self._configure()
        n = block.shape[-1]
        bands = self._split.process(block)
        dry = np.sum(bands, axis=0)
        wet = np.zeros_like(block)
        auto = self.params.get("auto_makeup")
        for i, (band, core) in enumerate(zip(bands, self._cores, strict=True), start=1):
            if self.params.get(f"b{i}_bypass"):
                wet += band
                self._meters[f"gr_b{i}"] = 0.0
                continue
            gr = core.gain_reduction_db(band)
            makeup = self.params.get(f"b{i}_makeup") + (core.auto_makeup_db() if auto else 0.0)
            wet += band * db_to_gain(gr + makeup)
            self._meters[f"gr_b{i}"] = core.last_gr_db
        mix = np.asarray(self.params.ramp("mix", n)) * 0.01
        out = dry + (wet - dry) * mix
        out *= db_to_gain(self.params.ramp("output_db", n))
        return out
