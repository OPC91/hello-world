"""FFT spectrum analysis with fractional-octave smoothing.

Scaling: values are in dB such that a full-scale sine reads 0 dB in the band
containing it. Band power is the *sum* of bin powers in the band, so pink noise
reads flat on a 1/3-octave display (the usual convention for mastering
analysers and reference curves).

Fractional-octave bands are integrated from the cumulative bin power with
linear interpolation at the band edges, so bands narrower than one FFT bin
(low frequencies) still get sensible values.

JUCE mapping: ``juce::dsp::FFT::performFrequencyOnlyForwardTransform`` +
``WindowingFunction::hann``; the smoothing is a few lines of C++.
"""

from __future__ import annotations

import numpy as np

DEFAULT_FFT = 8192


def third_octave_centers(lo: float = 20.0, hi: float = 20000.0) -> np.ndarray:
    """Base-2 1/3-octave centres ``1000 * 2^(k/3)`` within [lo, hi] (31 bands for 20 Hz-20 kHz)."""
    k = np.arange(-30, 31)
    f = 1000.0 * 2.0 ** (k / 3.0)
    return f[(f >= lo * 0.98) & (f <= hi * 1.02)]


def log_grid(lo: float = 20.0, hi: float = 20000.0, points_per_octave: int = 24) -> np.ndarray:
    n = int(np.ceil(np.log2(hi / lo) * points_per_octave)) + 1
    return lo * 2.0 ** (np.arange(n) / points_per_octave)


class _Window:
    def __init__(self, n_fft: int):
        self.n_fft = n_fft
        self.w = np.hanning(n_fft)
        s1 = self.w.sum()
        self.enbw = n_fft * np.sum(self.w**2) / s1**2
        self.scale = 2.0 / s1


def bin_power(frames: np.ndarray, win: _Window) -> np.ndarray:
    """Per-bin power normalised so that summing the bins of a sine of amplitude A gives A^2."""
    amp = np.abs(np.fft.rfft(frames * win.w, axis=-1)) * win.scale
    return amp**2 / win.enbw


def band_power(freqs_bins: np.ndarray, power: np.ndarray, centers: np.ndarray, fraction: float = 3.0) -> np.ndarray:
    """Integrate bin ``power`` (..., n_bins) over fractional-octave bands around ``centers``."""
    df = freqs_bins[1] - freqs_bins[0]
    edges = np.concatenate([[0.0], freqs_bins + df / 2])  # upper edge of every bin
    cum = np.concatenate([np.zeros(power.shape[:-1] + (1,)), np.cumsum(power, axis=-1)], axis=-1)
    half = 2.0 ** (1.0 / (2.0 * fraction))
    lo, hi = centers / half, centers * half

    def interp(f: np.ndarray) -> np.ndarray:
        return np.apply_along_axis(lambda c: np.interp(f, edges, c), -1, cum)

    return np.maximum(interp(hi) - interp(lo), 0.0)


def to_db(p: np.ndarray, floor_db: float = -150.0) -> np.ndarray:
    return 10.0 * np.log10(np.maximum(p, 10.0 ** (floor_db / 10.0)))


class SpectrumAnalyzer:
    """Streaming analyser: keeps the last ``n_fft`` samples (mid of L/R power) and
    produces an exponentially averaged, 1/3-octave-smoothed curve on request.

    Power is averaged over channels (not computed on L+R), so out-of-phase
    content still shows up.
    """

    def __init__(
        self,
        sample_rate: float,
        n_fft: int = DEFAULT_FFT,
        average_s: float = 0.25,
        grid: np.ndarray | None = None,
        fraction: float = 3.0,
    ):
        self.sample_rate = sample_rate
        self._win = _Window(n_fft)
        self.n_fft = n_fft
        self.freqs_bins = np.fft.rfftfreq(n_fft, 1.0 / sample_rate)
        self.grid = log_grid(hi=min(20000.0, sample_rate / 2 * 0.95)) if grid is None else grid
        self.fraction = fraction
        self.average_s = average_s
        self.reset()

    def reset(self) -> None:
        self._buf = np.zeros((2, self.n_fft))
        self._avg: np.ndarray | None = None
        self._last_t = 0.0
        self._pending = 0

    def push(self, x: np.ndarray) -> None:
        n = x.shape[-1]
        if n >= self.n_fft:
            self._buf = x[:, -self.n_fft :].copy()
        else:
            self._buf = np.concatenate([self._buf[:, n:], x], axis=-1)
        self._pending += n

    def compute(self) -> np.ndarray:
        """Average in the newest frame and return dB on :attr:`grid`."""
        p = bin_power(self._buf, self._win).mean(axis=0)
        elapsed = self._pending / self.sample_rate
        self._pending = 0
        if self._avg is None:
            self._avg = p
        else:
            a = np.exp(-elapsed / self.average_s) if self.average_s > 0 else 0.0
            self._avg = a * self._avg + (1 - a) * p
        return to_db(band_power(self.freqs_bins, self._avg, self.grid, self.fraction))


def long_term_spectrum(
    x: np.ndarray,
    sample_rate: float,
    centers: np.ndarray | None = None,
    n_fft: int = DEFAULT_FFT,
    fraction: float = 3.0,
) -> tuple[np.ndarray, np.ndarray]:
    """Long-term average spectrum (power mean over 50 %-overlapped frames, both channels).

    Returns ``(centers, dB)``; defaults to the 31 standard 1/3-octave bands.
    """
    centers = third_octave_centers() if centers is None else centers
    centers = centers[centers < sample_rate / 2 * 0.95]
    win = _Window(n_fft)
    hop = n_fft // 2
    n = x.shape[-1]
    if n < n_fft:
        x = np.concatenate([x, np.zeros((x.shape[0], n_fft - n))], axis=-1)
        n = n_fft
    acc = np.zeros(n_fft // 2 + 1)
    count = 0
    for start in range(0, n - n_fft + 1, hop):
        acc += bin_power(x[:, start : start + n_fft], win).sum(axis=0)
        count += x.shape[0]
    p = acc / max(count, 1)
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sample_rate)
    return centers, to_db(band_power(freqs, p, centers, fraction))
