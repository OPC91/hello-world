"""True-peak brickwall limiter with lookahead.

Algorithm (per sample, stereo-linked)::

    x' = x * drive
    p[n]  = 4x-oversampled peak of x' around sample n      (TruePeakDetector)
    r[n]  = min(1, ceiling / p[n])                         required gain
    h[n]  = min(r[n-W+1 .. n])                             hold over lookahead W
    g[n]  = h[n] if h[n] <= g[n-1]                         instant attack ...
            else h[n] - (h[n] - g[n-1]) * a_release        ... exponential release
    s[n]  = mean(g[n-W+1 .. n])                            W-tap box smoothing
    y[n]  = x'[n - D - W + 1] * s[n]                       D = detector delay

Guarantee: every g[k] for k in [n-W+1, n] is <= h[k] <= r[n-W+1], so
s[n] <= r[n-W+1], i.e. the gain applied to the delayed sample is never more than
the gain that sample requires. Gain therefore ramps down over exactly W samples
*before* the peak (a smooth fade, not a step) and the peak lands on the
ceiling. A final sample-peak clip at the ceiling catches any floating-point
residue. Inter-sample overs between output samples are bounded by the 4x
detection accuracy (see :mod:`mastermind.dsp.primitives.truepeak`), which a
small internal safety margin covers.

JUCE mapping: no direct equivalent (``juce::dsp::Limiter`` has no lookahead or
true-peak detection). Port :func:`limiter_gain` verbatim as the inner loop,
with ``juce::dsp::Oversampling`` for detection and ``juce::dsp::DelayLine`` for
the audio path.
"""

from __future__ import annotations

import numpy as np
from numba import njit

from mastermind.dsp.base import ParamSpec, Processor, db_to_gain, gain_to_db, registry
from mastermind.dsp.primitives.delay import DelayLine
from mastermind.dsp.primitives.dynamics import time_to_coef
from mastermind.dsp.primitives.truepeak import TruePeakDetector

#: Internal headroom below the ceiling covering 4x-detection under-reads.
SAFETY_MARGIN_DB = 0.15


@njit(cache=True)
def limiter_gain(r: np.ndarray, a_rel: float, ring_min: np.ndarray, ring_box: np.ndarray, st: np.ndarray) -> np.ndarray:
    """Lookahead gain computer (see module docstring).

    Args:
        r: Required gain per sample (<= 1).
        a_rel: Release one-pole coefficient.
        ring_min: Length-W ring of recent ``r`` values (state).
        ring_box: Length-W ring of recent ``g`` values (state).
        st: ``[write_index, box_sum, g_prev]`` (state).
    """
    n = r.shape[0]
    w = ring_min.shape[0]
    out = np.empty(n)
    idx = int(st[0])
    bsum = st[1]
    g = st[2]
    for i in range(n):
        ring_min[idx] = r[i]
        h = ring_min[0]
        for k in range(1, w):
            if ring_min[k] < h:
                h = ring_min[k]
        if h <= g:
            g = h
        else:
            g = h - (h - g) * a_rel
        bsum += g - ring_box[idx]
        ring_box[idx] = g
        idx += 1
        if idx == w:
            idx = 0
            bsum = 0.0  # re-sum once per window to stop rounding drift
            for k in range(w):
                bsum += ring_box[k]
        out[i] = bsum / w
    st[0] = idx
    st[1] = bsum
    st[2] = g
    return out


@registry.register
class TruePeakLimiter(Processor):
    type_id = "limiter"
    display_name = "True-Peak Limiter"

    @classmethod
    def param_specs(cls) -> list[ParamSpec]:
        return [
            ParamSpec("drive_db", "Gain", 0.0, 0.0, 24.0, "dB", smooth=True),
            ParamSpec("ceiling_db", "Ceiling", -1.0, -6.0, 0.0, "dBTP"),
            ParamSpec("release_ms", "Release", 80.0, 1.0, 1000.0, "ms", log=True),
            ParamSpec("lookahead_ms", "Lookahead", 1.5, 0.5, 5.0, "ms", structural=True),
        ]

    def _prepare(self) -> None:
        self._w = max(1, int(round(self.params.get("lookahead_ms") * 1e-3 * self.sample_rate)))
        self._det = TruePeakDetector(taps_per_phase=32, passband=0.995, refine=True)
        self._abs_delay = DelayLine(self._det.delay, channels=1)
        self._audio = DelayLine(self._det.delay + self._w - 1)
        self._ring_min = np.ones(self._w)
        self._ring_box = np.ones(self._w)
        self._st = np.array([0.0, float(self._w), 1.0])
        self._meters = {"gr": 0.0}

    @property
    def latency_samples(self) -> int:
        return self._audio.delay if self.is_prepared else 0

    def process(self, block: np.ndarray) -> np.ndarray:
        if "lookahead_ms" in self.params.consume_changes():
            self._prepare()
        n = block.shape[-1]
        x = block * db_to_gain(self.params.ramp("drive_db", n))
        ceiling = db_to_gain(self.params.get("ceiling_db"))
        target = ceiling * db_to_gain(-SAFETY_MARGIN_DB)

        peak = self._det.process(x)
        sample_peak = self._abs_delay.process(np.abs(x).max(axis=0, keepdims=True))[0]
        peak = np.maximum(peak, sample_peak)
        r = np.minimum(1.0, target / np.maximum(peak, 1e-12))
        a_rel = time_to_coef(self.params.get("release_ms"), self.sample_rate)
        s = limiter_gain(r, a_rel, self._ring_min, self._ring_box, self._st)

        y = self._audio.process(x) * s
        np.clip(y, -ceiling, ceiling, out=y)
        self._meters["gr"] = float(gain_to_db(s.min())) if n else 0.0
        return y

    @property
    def lookahead_samples(self) -> int:
        return self._w
