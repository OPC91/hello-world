"""Oversampled saturation: tape, tube and soft-clip.

All three curves are normalised to unity small-signal gain, so ``drive`` adds
harmonics rather than level; the output trim compensates loudness by ear.

Let ``d = 10^(drive/20)`` and ``x`` be the oversampled input:

tape
    High-shelf pre-emphasis (+4 dB @ 3 kHz), ``tanh(d*x)/d``, matching
    de-emphasis. High frequencies hit the curve harder, imitating tape's
    HF self-compression. Odd harmonics.
tube
    Biased tanh, ``(tanh(d*x + b) - tanh(b)) / (d * (1 - tanh(b)^2))`` with bias
    ``b = 0.6 * asymmetry``. Asymmetric -> even harmonics; the resulting
    signal-dependent DC is removed by a 5 Hz high-pass.
soft_clip
    Cubic, ``u - u^3/3`` for ``|u| < 1`` else ``±2/3`` with ``u = d*x``, scaled by
    ``1/d``. Smooth knee into hard ceiling.

The nonlinearity runs at ``oversampling`` x the base rate to keep aliasing
down; the dry path is delayed by the oversampler latency so ``mix`` is
phase-coherent.

JUCE mapping: ``juce::dsp::Oversampling`` + ``juce::dsp::WaveShaper`` (or a
hand-written loop), ``juce::dsp::DryWetMixer`` for the mix with latency.
"""

from __future__ import annotations

import numpy as np

from mastermind.dsp.base import ParamSpec, Processor, db_to_gain, registry
from mastermind.dsp.primitives.biquad import SOSFilter, design
from mastermind.dsp.primitives.delay import DelayLine
from mastermind.dsp.primitives.oversampling import Oversampler

MODES = ("tape", "tube", "soft_clip")
TAPE_EMPHASIS_DB = 4.0
TAPE_EMPHASIS_HZ = 3000.0


def tape_curve(x: np.ndarray, d: np.ndarray | float) -> np.ndarray:
    return np.tanh(d * x) / d


def tube_curve(x: np.ndarray, d: np.ndarray | float, asymmetry: float) -> np.ndarray:
    b = 0.6 * asymmetry
    tb = np.tanh(b)
    return (np.tanh(d * x + b) - tb) / (d * (1.0 - tb * tb))


def soft_clip_curve(x: np.ndarray, d: np.ndarray | float) -> np.ndarray:
    u = np.clip(d * x, -1.0, 1.0)
    return (u - u**3 / 3.0) / d


@registry.register
class Saturation(Processor):
    type_id = "saturation"
    display_name = "Saturation"

    @classmethod
    def param_specs(cls) -> list[ParamSpec]:
        return [
            ParamSpec("mode", "Mode", "tape", kind="choice", choices=MODES),
            ParamSpec("drive_db", "Drive", 0.0, 0.0, 24.0, "dB", smooth=True),
            ParamSpec("asymmetry", "Asymmetry (tube)", 0.3, 0.0, 1.0),
            ParamSpec("mix", "Mix", 100.0, 0.0, 100.0, "%", smooth=True),
            ParamSpec("output_db", "Output", 0.0, -24.0, 12.0, "dB", smooth=True),
            ParamSpec(
                "oversampling", "Oversampling", "4", kind="choice", choices=("1", "2", "4", "8"), structural=True
            ),
        ]

    def _prepare(self) -> None:
        sr = self.sample_rate
        self._os = Oversampler(int(self.params.get("oversampling")))
        self._dry = DelayLine(self._os.latency)
        self._pre = SOSFilter(1)
        self._post = SOSFilter(1)
        self._pre.set_sos(design("high_shelf", TAPE_EMPHASIS_HZ, sr, 0.7071, TAPE_EMPHASIS_DB)[None])
        self._post.set_sos(design("high_shelf", TAPE_EMPHASIS_HZ, sr, 0.7071, -TAPE_EMPHASIS_DB)[None])
        self._dc = SOSFilter(1)
        self._dc.set_sos(design("low_cut", 5.0, sr, 0.7071)[None])

    @property
    def latency_samples(self) -> int:
        return self._os.latency if self.is_prepared else 0

    def process(self, block: np.ndarray) -> np.ndarray:
        if "oversampling" in self.params.consume_changes():
            self._prepare()
        n = block.shape[-1]
        mode = self.params.get("mode")
        dry = self._dry.process(block)

        x = self._pre.process(block) if mode == "tape" else block
        hi = self._os.up(x)
        d = db_to_gain(self.params.ramp("drive_db", n))
        if isinstance(d, np.ndarray):
            d = np.repeat(d, self._os.factor)
        if mode == "tape":
            hi = tape_curve(hi, d)
        elif mode == "tube":
            hi = tube_curve(hi, d, self.params.get("asymmetry"))
        else:
            hi = soft_clip_curve(hi, d)
        wet = self._os.down(hi)
        if mode == "tape":
            wet = self._post.process(wet)
        elif mode == "tube":
            wet = self._dc.process(wet)

        mix = np.asarray(self.params.ramp("mix", n)) * 0.01
        out = dry + (wet - dry) * mix
        out *= db_to_gain(self.params.ramp("output_db", n))
        return out
