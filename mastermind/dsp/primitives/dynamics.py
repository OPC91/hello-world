"""Compressor building blocks: level detection, static gain curve and ballistics.

Design follows Giannoulis, Massberg & Reiss, "Digital Dynamic Range
Compressor Design - A Tutorial and Analysis" (JAES 2012): a feed-forward,
log-domain, "smooth decoupled" topology:

    level_dB = detector(x)                      # peak or RMS, stereo-linked
    gr_dB    = static_curve(level_dB) - level_dB  # <= 0, soft knee
    gr_s     = attack/release one-pole on gr_dB  # numba kernel
    y        = x * 10^((gr_s + makeup) / 20)

JUCE mapping: ``juce::dsp::BallisticsFilter`` / custom gain computer; the numba
kernel :func:`smooth_gain_db` is a direct template for the C++ inner loop.
"""

from __future__ import annotations

import math

import numpy as np
from numba import njit
from scipy import signal

from mastermind.dsp.primitives.biquad import SOSFilter, design

EPS = 1e-12


def time_to_coef(time_ms: float, sample_rate: float) -> float:
    """One-pole coefficient reaching 1 - 1/e of a step after ``time_ms``."""
    if time_ms <= 0:
        return 0.0
    return math.exp(-1.0 / (time_ms * 1e-3 * sample_rate))


def static_curve_gr(level_db: np.ndarray, threshold_db: float, ratio: float, knee_db: float) -> np.ndarray:
    """Gain reduction (dB, <= 0) of a soft-knee downward compressor."""
    over = level_db - threshold_db
    slope = 1.0 / ratio - 1.0
    gr = np.where(over > 0, slope * over, 0.0)
    if knee_db > 0:
        in_knee = np.abs(over) <= knee_db / 2
        gr = np.where(in_knee, slope * (over + knee_db / 2) ** 2 / (2 * knee_db), gr)
    return gr


@njit(cache=True)
def smooth_gain_db(target: np.ndarray, attack: float, release: float, state: np.ndarray) -> np.ndarray:
    """Attack/release ballistics on a gain-reduction signal in dB.

    ``attack`` is used when the reduction deepens (target more negative), and
    ``release`` when it recovers. ``state[0]`` carries the last value across blocks.
    """
    n = target.shape[0]
    out = np.empty(n)
    y = state[0]
    for i in range(n):
        t = target[i]
        if t < y:
            y = attack * y + (1.0 - attack) * t
        else:
            y = release * y + (1.0 - release) * t
        out[i] = y
    state[0] = y
    return out


class LevelDetector:
    """Stereo-linked level detector returning dB per sample.

    Modes:
        ``peak``: ``max(|L|, |R|)``.
        ``rms``:  one-pole mean of ``(L^2 + R^2) / 2`` with time constant ``rms_ms``.

    An optional side-chain high-pass (2nd order) reduces bass pumping.
    """

    def __init__(self, sample_rate: float, mode: str = "peak", rms_ms: float = 10.0, sc_hpf_hz: float = 0.0):
        self.sample_rate = sample_rate
        self.mode = mode
        self._zi = np.zeros(1)
        self._hpf = SOSFilter(1, 2)
        self.configure(mode, rms_ms, sc_hpf_hz)

    def configure(self, mode: str, rms_ms: float, sc_hpf_hz: float) -> None:
        self.mode = mode
        self._a = time_to_coef(rms_ms, self.sample_rate)
        self.sc_hpf_hz = sc_hpf_hz
        if sc_hpf_hz > 0:
            self._hpf.set_sos(design("low_cut", sc_hpf_hz, self.sample_rate, 0.7071)[None, :])

    def reset(self) -> None:
        self._zi[:] = 0.0
        self._hpf.reset()

    def process(self, x: np.ndarray) -> np.ndarray:
        if self.sc_hpf_hz > 0:
            x = self._hpf.process(x)
        if self.mode == "rms":
            p = 0.5 * (x[0] * x[0] + x[1] * x[1])
            p, self._zi = signal.lfilter([1.0 - self._a], [1.0, -self._a], p, zi=self._zi)
            return 10.0 * np.log10(np.maximum(p, EPS))
        return 20.0 * np.log10(np.maximum(np.maximum(np.abs(x[0]), np.abs(x[1])), EPS))


class CompressorCore:
    """Stereo-linked feed-forward compressor computing a per-sample gain.

    Parameters are plain attributes set via :meth:`configure`; the owning
    Processor maps its ParamSet onto them.
    """

    #: Reference level used for auto make-up gain.
    AUTO_MAKEUP_REF_DB = -18.0

    def __init__(self, sample_rate: float):
        self.sample_rate = sample_rate
        self.detector = LevelDetector(sample_rate)
        self._state = np.zeros(1)
        self.threshold_db = 0.0
        self.ratio = 1.0
        self.knee_db = 0.0
        self.attack = 0.0
        self.release = 0.0
        self.last_gr_db = 0.0

    def configure(
        self,
        threshold_db: float,
        ratio: float,
        attack_ms: float,
        release_ms: float,
        knee_db: float = 0.0,
        detector: str = "peak",
        rms_ms: float = 10.0,
        sc_hpf_hz: float = 0.0,
    ) -> None:
        self.threshold_db = threshold_db
        self.ratio = max(ratio, 1.0)
        self.knee_db = knee_db
        self.attack = time_to_coef(attack_ms, self.sample_rate)
        self.release = time_to_coef(release_ms, self.sample_rate)
        if (detector, sc_hpf_hz) != (self.detector.mode, self.detector.sc_hpf_hz) or detector == "rms":
            self.detector.configure(detector, rms_ms, sc_hpf_hz)

    def auto_makeup_db(self) -> float:
        """Make-up that restores a signal sitting at -18 dBFS to its input level."""
        ref = np.array([self.AUTO_MAKEUP_REF_DB])
        return float(-static_curve_gr(ref, self.threshold_db, self.ratio, self.knee_db)[0])

    def reset(self) -> None:
        self.detector.reset()
        self._state[:] = 0.0
        self.last_gr_db = 0.0

    def gain_reduction_db(self, x: np.ndarray) -> np.ndarray:
        """Smoothed per-sample gain reduction (dB, <= 0) for block ``x``."""
        level = self.detector.process(x)
        target = static_curve_gr(level, self.threshold_db, self.ratio, self.knee_db)
        gr = smooth_gain_db(target, self.attack, self.release, self._state)
        self.last_gr_db = float(gr.min()) if gr.size else 0.0
        return gr
