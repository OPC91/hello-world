"""True-peak (inter-sample peak) estimation per ITU-R BS.1770-4 Annex 2.

The signal is upsampled 4x with a linear-phase interpolation filter and the
absolute maximum of the oversampled signal is taken. BS.1770 notes that 4x
oversampling can under-read a full-scale tone at fs/2 by up to ~0.7 dB; for
programme material at 44.1/48 kHz the error is a small fraction of a dB.

JUCE mapping: ``juce::dsp::Oversampling`` (4x, FIR) followed by a peak scan.
"""

from __future__ import annotations

import numpy as np

from mastermind.dsp.primitives.oversampling import Upsampler

#: 24 taps per phase (97 taps). The BS.1770 example filter uses 12 per phase;
#: the longer kernel keeps the response flat to 0.98 x Nyquist, so the
#: worst-case steady-state under-read for tones up to 20 kHz @ 48 kHz is
#: ~0.3 dB (mostly the inherent 4x sampling error) and it never over-reads.
TP_TAPS_PER_PHASE = 24
TP_FACTOR = 4


def parabolic_peaks(a: np.ndarray, prev: np.ndarray) -> np.ndarray:
    """Refine sampled magnitudes ``a`` (channels, n) with 3-point parabolic peak estimates.

    For each sample that is a local maximum of ``|u|``, the vertex of the
    parabola through it and its neighbours estimates the true maximum between
    oversampled points. Returned values are >= the input. ``prev`` (channels, 2)
    holds the last two samples of the previous block and is updated in place.
    Output index k corresponds to input index k - 1 (one sample of lag).
    """
    ext = np.concatenate([prev, a], axis=-1)
    prev[:] = ext[:, -2:]
    y0, y1, y2 = ext[:, :-2], ext[:, 1:-1], ext[:, 2:]
    den = y0 - 2.0 * y1 + y2
    is_peak = (y1 >= y0) & (y1 >= y2) & (den < 0)
    safe = np.where(is_peak, den, -1.0)
    vertex = y1 - 0.125 * (y0 - y2) ** 2 / safe
    return np.where(is_peak, np.maximum(vertex, y1), y1)


class TruePeakDetector:
    """Streaming 4x true-peak detector.

    :meth:`process` returns, for each input sample, the stereo-linked maximum
    of the interpolated waveform in the interval around that sample (the
    oversampled points following it plus those preceding it). The output is
    delayed by :attr:`delay` base-rate samples relative to the input.

    ``refine=True`` (used by the limiter, not the meter) adds parabolic
    interpolation between the 4x points. Plain 4x sampling can miss the crest of
    a 20 kHz tone by up to ~0.5 dB; refinement cuts that to a few hundredths.
    """

    def __init__(
        self,
        channels: int = 2,
        factor: int = TP_FACTOR,
        taps_per_phase: int = TP_TAPS_PER_PHASE,
        passband: float = 0.98,
        refine: bool = False,
    ):
        self.factor = factor
        self.refine = refine
        self._up = Upsampler(factor, taps_per_phase, channels, passband=passband)
        self._prev = 0.0
        self._prev_hi = np.zeros((channels, 2))

    @property
    def delay(self) -> int:
        return self._up.delay_high // self.factor

    def reset(self) -> None:
        self._up.reset()
        self._prev = 0.0
        self._prev_hi[:] = 0.0

    def process(self, x: np.ndarray) -> np.ndarray:
        n = x.shape[-1]
        hi = np.abs(self._up.process(x))
        if self.refine:
            # One high-rate sample of lag; absorbed by the two-segment window below.
            hi = parabolic_peaks(hi, self._prev_hi)
        hi = hi.max(axis=0)  # stereo link
        seg = hi.reshape(n, self.factor).max(axis=1)  # covers [m, m+1)
        prev = np.concatenate([[self._prev], seg[:-1]])  # covers [m-1, m)
        self._prev = float(seg[-1])
        return np.maximum(seg, prev)


def true_peak(x: np.ndarray, factor: int = TP_FACTOR) -> float:
    """Offline true peak (linear) of a whole ``(channels, n)`` or ``(n,)`` signal."""
    x = np.atleast_2d(x)
    det = TruePeakDetector(x.shape[0], factor)
    pad = np.zeros((x.shape[0], det.delay + 2))
    peaks = det.process(np.concatenate([x, pad], axis=-1))
    return float(max(peaks.max(), np.abs(x).max()))


def true_peak_dbtp(x: np.ndarray) -> float:
    return float(20.0 * np.log10(max(true_peak(x), 1e-12)))
