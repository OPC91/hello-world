"""Stateful polyphase-equivalent up/down sampling by an integer factor.

Uses a single linear-phase Kaiser-windowed low-pass FIR of length
``taps_per_phase * factor + 1`` (odd, so the delay is an integer number of
high-rate samples), applied with :class:`FIRConvolver` after zero-stuffing
(upsampling) or before decimation (downsampling). The kernel is chosen so the
round-trip latency is an integer number of base-rate samples.

JUCE mapping: ``juce::dsp::Oversampling<float>`` (filterHalfBandFIREquiripple,
which is likewise linear phase and reports ``getLatencyInSamples()``).
"""

from __future__ import annotations

import numpy as np
from scipy import signal

from mastermind.dsp.primitives.fir import FIRConvolver


def lowpass_kernel(factor: int, taps_per_phase: int = 32, passband: float = 0.90, beta: float = 9.0) -> np.ndarray:
    """Interpolation/anti-aliasing kernel for ``factor``x oversampling.

    ``passband`` is the -6 dB point as a fraction of the base-rate Nyquist.
    The kernel has DC gain 1.
    """
    n = taps_per_phase * factor + 1
    h = signal.firwin(n, cutoff=passband / factor, window=("kaiser", beta))
    return h / h.sum()


class Upsampler:
    """Zero-stuff by ``factor`` and interpolate. Delay: ``(n_taps - 1) / 2`` high-rate samples."""

    def __init__(self, factor: int, taps_per_phase: int = 32, channels: int = 2, passband: float = 0.90):
        self.factor = factor
        self.kernel = lowpass_kernel(factor, taps_per_phase, passband) * factor
        self._conv = FIRConvolver(self.kernel, channels) if factor > 1 else None

    @property
    def delay_high(self) -> int:
        return (len(self.kernel) - 1) // 2 if self.factor > 1 else 0

    def reset(self) -> None:
        if self._conv:
            self._conv.reset()

    def process(self, x: np.ndarray) -> np.ndarray:
        if self.factor == 1:
            return x
        up = np.zeros((x.shape[0], x.shape[1] * self.factor))
        up[:, :: self.factor] = x
        return self._conv.process(up)


class Downsampler:
    """Anti-alias filter and decimate by ``factor``. Delay: ``(n_taps - 1) / 2`` high-rate samples."""

    def __init__(self, factor: int, taps_per_phase: int = 32, channels: int = 2, passband: float = 0.90):
        self.factor = factor
        self.kernel = lowpass_kernel(factor, taps_per_phase, passband)
        self._conv = FIRConvolver(self.kernel, channels) if factor > 1 else None

    @property
    def delay_high(self) -> int:
        return (len(self.kernel) - 1) // 2 if self.factor > 1 else 0

    def reset(self) -> None:
        if self._conv:
            self._conv.reset()

    def process(self, x: np.ndarray) -> np.ndarray:
        if self.factor == 1:
            return x
        return self._conv.process(x)[:, :: self.factor]


class Oversampler:
    """Up/down pair with a combined latency reported in base-rate samples.

    Usage::

        hi = os.up(block)        # (2, n * factor)
        hi = nonlinearity(hi)
        out = os.down(hi)        # (2, n), delayed by os.latency samples
    """

    def __init__(self, factor: int, taps_per_phase: int = 32, channels: int = 2):
        if factor not in (1, 2, 4, 8):
            raise ValueError("oversampling factor must be 1, 2, 4 or 8")
        self.factor = factor
        self._up = Upsampler(factor, taps_per_phase, channels)
        self._down = Downsampler(factor, taps_per_phase, channels)

    @property
    def latency(self) -> int:
        total_high = self._up.delay_high + self._down.delay_high
        return total_high // self.factor

    def reset(self) -> None:
        self._up.reset()
        self._down.reset()

    def up(self, x: np.ndarray) -> np.ndarray:
        return self._up.process(x)

    def down(self, x: np.ndarray) -> np.ndarray:
        return self._down.process(x)
