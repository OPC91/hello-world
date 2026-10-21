"""FIR design from a magnitude response, and a stateful FFT convolver.

JUCE mapping: :class:`FIRConvolver` ~ ``juce::dsp::Convolution`` (uniform
partitioned, with ``Convolution::Latency``/``NonUniform``) or
``juce::dsp::FIR::Filter`` for short kernels. The design routine would be
implemented with ``juce::dsp::FFT`` + a window from
``juce::dsp::WindowingFunction``.
"""

from __future__ import annotations

from collections.abc import Callable

import numpy as np
from scipy import fft as sfft
from scipy import signal


def linear_phase_from_magnitude(
    magnitude: Callable[[np.ndarray], np.ndarray],
    n_taps: int,
    sample_rate: float,
    oversample_grid: int = 4,
) -> np.ndarray:
    """Design a symmetric (type I) linear-phase FIR approximating ``|H(f)|``.

    Frequency-sampling method: the target magnitude is sampled on a dense grid
    (``oversample_grid`` x the kernel length), turned into a zero-phase impulse
    response with an inverse real FFT, centred, truncated to ``n_taps`` and
    windowed (Blackman-Harris, ~92 dB sidelobes). Truncation smooths features
    narrower than roughly ``sample_rate / n_taps`` Hz.

    Args:
        magnitude: Function mapping an array of frequencies (Hz) to linear magnitude.
        n_taps: Odd kernel length; group delay is ``(n_taps - 1) / 2`` samples.
        sample_rate: Sample rate in Hz.
    """
    if n_taps % 2 == 0:
        raise ValueError("n_taps must be odd for a type I linear-phase FIR")
    n_grid = sfft.next_fast_len(oversample_grid * n_taps)
    freqs = np.fft.rfftfreq(n_grid, d=1.0 / sample_rate)
    mag = np.asarray(magnitude(freqs), dtype=float)
    ir = np.fft.irfft(mag, n=n_grid)
    half = (n_taps - 1) // 2
    ir = np.roll(ir, half)[:n_taps]
    return ir * signal.windows.blackmanharris(n_taps, sym=True)


class FIRConvolver:
    """Stateful overlap-save convolution of a ``(channels, n)`` block with one kernel
    per channel (or a single kernel shared by all channels).

    Any block length is accepted: each call convolves ``[history, block]`` with an
    FFT sized to fit, keeping the last ``n_taps - 1`` inputs as history. Output is
    therefore identical regardless of block size. Kernel spectra are cached per
    FFT size.

    When the kernel changes, the next block is computed with both the old and
    new kernels and linearly crossfaded to avoid a click.
    """

    def __init__(self, kernels: np.ndarray, channels: int = 2):
        self.channels = channels
        self._kernels = self._shape(kernels)
        self._old: np.ndarray | None = None
        self._cache: dict[int, np.ndarray] = {}
        self._old_cache: dict[int, np.ndarray] = {}
        self._hist = np.zeros((channels, self.n_taps - 1))

    def _shape(self, k: np.ndarray) -> np.ndarray:
        k = np.atleast_2d(np.asarray(k, dtype=float))
        if k.shape[0] == 1:
            k = np.repeat(k, self.channels, axis=0)
        return k

    @property
    def n_taps(self) -> int:
        return self._kernels.shape[-1]

    def set_kernels(self, kernels: np.ndarray, crossfade: bool = True) -> None:
        k = self._shape(kernels)
        if k.shape != self._kernels.shape:
            raise ValueError("kernel length cannot change; create a new convolver")
        if crossfade:
            self._old, self._old_cache = self._kernels, self._cache
        self._kernels, self._cache = k, {}

    def reset(self) -> None:
        self._hist[:] = 0.0
        self._old = None

    def _spectrum(self, kernels, cache, nfft) -> np.ndarray:
        spec = cache.get(nfft)
        if spec is None:
            spec = cache[nfft] = sfft.rfft(kernels, n=nfft, axis=-1)
        return spec

    def process(self, x: np.ndarray) -> np.ndarray:
        n = x.shape[-1]
        joined = np.concatenate([self._hist, x], axis=-1)
        nfft = sfft.next_fast_len(joined.shape[-1])
        X = sfft.rfft(joined, n=nfft, axis=-1)
        t0 = self.n_taps - 1
        y = sfft.irfft(X * self._spectrum(self._kernels, self._cache, nfft), n=nfft, axis=-1)[:, t0 : t0 + n]
        if self._old is not None:
            y_old = sfft.irfft(X * self._spectrum(self._old, self._old_cache, nfft), n=nfft, axis=-1)
            y_old = y_old[:, t0 : t0 + n]
            fade = np.linspace(0.0, 1.0, n, endpoint=False) if n > 1 else np.ones(1)
            y = y_old + (y - y_old) * fade
            self._old = None
            self._old_cache = {}
        self._hist = joined[:, -t0:].copy() if t0 else self._hist
        return y
