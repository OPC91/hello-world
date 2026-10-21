"""Phase correlation (overall and per band) and goniometer data.

Correlation is the normalised cross-correlation at lag 0, smoothed with
one-pole averagers (time constant ``tau``, default 300 ms)::

    r = <L R> / sqrt(<L^2> <R^2>)          in [-1, +1]

+1 = identical channels (mono), 0 = uncorrelated, -1 = polarity-inverted.
Below an energy floor (silence) the meter reads 0.

Per-band correlation splits both channels with the LR4 splitter (identical
allpass phase on L and R, so correlation is unaffected) into
low / low-mid / high-mid / high.

The mono-compatibility warning fires when correlation stays below 0 for longer
than ``warn_after`` seconds.

JUCE mapping: three one-pole smoothers per band (``juce::dsp::FirstOrderTPTFilter``
lowpass or a hand loop) + ``LinkwitzRileyFilter`` splitter.
"""

from __future__ import annotations

import math

import numpy as np
from scipy import signal

from mastermind.dsp.primitives.crossover import MultiBandSplitter

BAND_EDGES = (250.0, 2000.0, 8000.0)
BAND_LABELS = ("Low", "Low-Mid", "High-Mid", "High")
SILENCE_POWER = 1e-10  # -100 dB


class _Correlator:
    def __init__(self, sample_rate: float, tau_s: float):
        self.a = math.exp(-1.0 / (tau_s * sample_rate))
        self.reset()

    def reset(self) -> None:
        self.zi = np.zeros((3, 1))

    def process(self, l: np.ndarray, r: np.ndarray) -> np.ndarray:
        prods = np.vstack([l * r, l * l, r * r])
        sm, self.zi = signal.lfilter([1.0 - self.a], [1.0, -self.a], prods, axis=-1, zi=self.zi)
        lr, ll, rr = sm
        den = np.sqrt(np.maximum(ll * rr, 0.0))
        ok = den > SILENCE_POWER
        out = np.zeros_like(lr)
        out[ok] = lr[ok] / den[ok]
        return np.clip(out, -1.0, 1.0)


class CorrelationMeter:
    """Streaming correlation meter with peak-hold, per-band values and mono warning."""

    def __init__(
        self,
        sample_rate: float,
        tau_s: float = 0.3,
        hold_s: float = 2.0,
        warn_after: float = 1.0,
        bands: tuple[float, ...] = BAND_EDGES,
    ):
        self.sample_rate = sample_rate
        self.tau_s = tau_s
        self.hold_s = hold_s
        self.warn_after = warn_after
        self._main = _Correlator(sample_rate, tau_s)
        self._split = MultiBandSplitter(list(bands), sample_rate)
        self._bands = [_Correlator(sample_rate, tau_s) for _ in range(len(bands) + 1)]
        self.reset()

    def reset(self) -> None:
        self._main.reset()
        self._split.reset()
        for b in self._bands:
            b.reset()
        self.value = 0.0
        self.hold = 1.0  # most negative value seen within the hold window
        self._hold_age = 0.0
        self.band_values = [0.0] * len(self._bands)
        self.negative_run_s = 0.0
        self.band_negative_run_s = [0.0] * len(self._bands)
        self._sum_r = 0.0
        self._n = 0
        self._neg_n = 0

    def _run(self, r: np.ndarray, run: float) -> float:
        n = r.size
        nonneg = np.flatnonzero(r >= 0)
        if nonneg.size == 0:
            return run + n / self.sample_rate
        return (n - 1 - nonneg[-1]) / self.sample_rate

    def process(self, x: np.ndarray) -> None:
        n = x.shape[-1]
        if n == 0:
            return
        r = self._main.process(x[0], x[1])
        self.value = float(r[-1])
        self.negative_run_s = self._run(r, self.negative_run_s)
        self._sum_r += float(r.sum())
        self._n += n
        self._neg_n += int((r < 0).sum())

        lo = float(r.min())
        self._hold_age += n / self.sample_rate
        if lo <= self.hold or self._hold_age > self.hold_s:
            self.hold = lo
            self._hold_age = 0.0

        for i, (band, corr) in enumerate(zip(self._split.process(x), self._bands, strict=True)):
            rb = corr.process(band[0], band[1])
            self.band_values[i] = float(rb[-1])
            self.band_negative_run_s[i] = self._run(rb, self.band_negative_run_s[i])

    @property
    def mono_warning(self) -> bool:
        return self.negative_run_s > self.warn_after

    @property
    def band_flags(self) -> list[bool]:
        """True for bands currently below 0."""
        return [v < 0 for v in self.band_values]

    @property
    def mean(self) -> float:
        return self._sum_r / self._n if self._n else 0.0

    @property
    def negative_fraction(self) -> float:
        return self._neg_n / self._n if self._n else 0.0


def correlation(x: np.ndarray) -> float:
    """Whole-signal correlation coefficient (no smoothing)."""
    l, r = x[0], x[1]
    den = math.sqrt(float(np.dot(l, l)) * float(np.dot(r, r)))
    return float(np.dot(l, r) / den) if den > 0 else 0.0


def goniometer_points(x: np.ndarray, max_points: int = 512) -> tuple[np.ndarray, np.ndarray]:
    """Lissajous / vectorscope coordinates rotated 45 degrees (M/S).

    ``y = (L + R) / sqrt 2`` (mid, vertical), ``x = (R - L) / sqrt 2`` (side,
    horizontal): mono is a vertical line, left-only leans up-left, out-of-phase
    content spreads horizontally. Decimated to at most ``max_points``.
    """
    step = max(1, -(-x.shape[-1] // max_points))  # ceil
    l, r = x[0, ::step], x[1, ::step]
    s = 1.0 / math.sqrt(2.0)
    return (r - l) * s, (l + r) * s
