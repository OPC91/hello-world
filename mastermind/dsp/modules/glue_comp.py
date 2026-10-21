"""Bus-style "glue" compressor.

Wideband, stereo-linked, RMS-ish (5 ms) detector with an optional side-chain
high-pass so the low end does not drive the gain reduction. Mix enables
parallel compression (the module has no latency, so dry needs no delay).

JUCE mapping: custom gain computer + ``juce::dsp::BallisticsFilter``;
``juce::dsp::DryWetMixer`` for mix.
"""

from __future__ import annotations

import numpy as np

from mastermind.dsp.base import ParamSpec, Processor, db_to_gain, registry
from mastermind.dsp.primitives.dynamics import CompressorCore


@registry.register
class GlueCompressor(Processor):
    type_id = "glue"
    display_name = "Glue Compressor"

    @classmethod
    def param_specs(cls) -> list[ParamSpec]:
        return [
            ParamSpec("threshold", "Threshold", 0.0, -40.0, 0.0, "dB"),
            ParamSpec("ratio", "Ratio", 2.0, 1.0, 10.0, ":1", log=True),
            ParamSpec("attack", "Attack", 10.0, 0.1, 100.0, "ms", log=True),
            ParamSpec("release", "Release", 200.0, 20.0, 1500.0, "ms", log=True),
            ParamSpec("knee", "Knee", 3.0, 0.0, 12.0, "dB"),
            ParamSpec("sc_hpf", "SC High-pass", 0.0, 0.0, 300.0, "Hz"),
            ParamSpec("makeup", "Make-up", 0.0, -6.0, 18.0, "dB", smooth=True),
            ParamSpec("mix", "Mix", 100.0, 0.0, 100.0, "%", smooth=True),
        ]

    def _configure(self) -> None:
        p = self.params
        self._core.configure(
            p.get("threshold"),
            p.get("ratio"),
            p.get("attack"),
            p.get("release"),
            p.get("knee"),
            detector="rms",
            rms_ms=5.0,
            sc_hpf_hz=p.get("sc_hpf"),
        )

    def _prepare(self) -> None:
        self._core = CompressorCore(self.sample_rate)
        self._configure()
        self._meters = {"gr": 0.0}

    def process(self, block: np.ndarray) -> np.ndarray:
        if self.params.consume_changes() - {"makeup", "mix"}:
            self._configure()
        n = block.shape[-1]
        gr = self._core.gain_reduction_db(block)
        self._meters["gr"] = self._core.last_gr_db
        wet = block * db_to_gain(gr + self.params.ramp("makeup", n))
        mix = np.asarray(self.params.ramp("mix", n)) * 0.01
        return block + (wet - block) * mix
