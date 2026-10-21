"""RBJ-cookbook biquad design and a stateful stereo SOS filter.

Coefficients follow Robert Bristow-Johnson's "Audio EQ Cookbook", normalised
so that a0 == 1 and laid out as scipy second-order sections
``[b0, b1, b2, 1, a1, a2]``.

JUCE mapping: ``juce::dsp::IIR::Coefficients<float>::makePeakFilter`` etc. use
the same cookbook formulas, so magnitude responses should match bit-for-bit
modulo float precision; :class:`SOSFilter` maps to a
``juce::dsp::ProcessorDuplicator<IIR::Filter, IIR::Coefficients>`` per section.
"""

from __future__ import annotations

import math

import numpy as np
from scipy import signal

IDENTITY_SOS = np.array([1.0, 0.0, 0.0, 1.0, 0.0, 0.0])

FILTER_TYPES = ("bell", "low_shelf", "high_shelf", "low_cut", "high_cut", "notch", "allpass")


def _norm(b0, b1, b2, a0, a1, a2) -> np.ndarray:
    return np.array([b0 / a0, b1 / a0, b2 / a0, 1.0, a1 / a0, a2 / a0])


def design(kind: str, freq: float, sample_rate: float, q: float = 0.7071, gain_db: float = 0.0) -> np.ndarray:
    """Return one second-order section for an RBJ cookbook filter.

    Args:
        kind: One of :data:`FILTER_TYPES`.
        freq: Centre / corner frequency in Hz (clamped below Nyquist).
        sample_rate: Sample rate in Hz.
        q: Quality factor. For shelves this is the cookbook "Q" (0.7071 = no overshoot).
        gain_db: Gain for bell / shelf types (ignored otherwise).
    """
    freq = min(max(freq, 1.0), 0.499 * sample_rate)
    q = max(q, 1e-3)
    w0 = 2.0 * math.pi * freq / sample_rate
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2.0 * q)
    A = 10.0 ** (gain_db / 40.0)

    if kind == "bell":
        return _norm(1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A)
    if kind == "low_shelf":
        sa = 2.0 * math.sqrt(A) * alpha
        return _norm(
            A * ((A + 1) - (A - 1) * cw + sa),
            2 * A * ((A - 1) - (A + 1) * cw),
            A * ((A + 1) - (A - 1) * cw - sa),
            (A + 1) + (A - 1) * cw + sa,
            -2 * ((A - 1) + (A + 1) * cw),
            (A + 1) + (A - 1) * cw - sa,
        )
    if kind == "high_shelf":
        sa = 2.0 * math.sqrt(A) * alpha
        return _norm(
            A * ((A + 1) + (A - 1) * cw + sa),
            -2 * A * ((A - 1) + (A + 1) * cw),
            A * ((A + 1) + (A - 1) * cw - sa),
            (A + 1) - (A - 1) * cw + sa,
            2 * ((A - 1) - (A + 1) * cw),
            (A + 1) - (A - 1) * cw - sa,
        )
    if kind == "low_cut":  # high-pass
        return _norm((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha)
    if kind == "high_cut":  # low-pass
        return _norm((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha)
    if kind == "notch":
        return _norm(1, -2 * cw, 1, 1 + alpha, -2 * cw, 1 - alpha)
    if kind == "allpass":
        return _norm(1 - alpha, -2 * cw, 1 + alpha, 1 + alpha, -2 * cw, 1 - alpha)
    raise ValueError(f"unknown filter type {kind!r}")


def sos_response(sos: np.ndarray, freqs: np.ndarray, sample_rate: float) -> np.ndarray:
    """Complex frequency response of an SOS cascade at ``freqs`` (Hz)."""
    sos = np.atleast_2d(sos)
    _, h = signal.sosfreqz(sos, worN=np.asarray(freqs, dtype=float), fs=sample_rate)
    return h


class SOSFilter:
    """Stateful cascade of second-order sections applied to a ``(channels, n)`` block.

    The number of sections is fixed at construction so the filter state survives
    coefficient updates (only the coefficients change, the memories are kept),
    which avoids discontinuities when a user drags an EQ band.
    """

    def __init__(self, n_sections: int, channels: int = 2):
        self.sos = np.tile(IDENTITY_SOS, (n_sections, 1))
        self.zi = np.zeros((n_sections, channels, 2))

    def set_sos(self, sos: np.ndarray) -> None:
        sos = np.atleast_2d(np.asarray(sos, dtype=float))
        if sos.shape != self.sos.shape:
            raise ValueError(f"expected {self.sos.shape} sections, got {sos.shape}")
        self.sos = sos

    def set_section(self, i: int, section: np.ndarray) -> None:
        self.sos[i] = section

    def reset(self) -> None:
        self.zi[:] = 0.0

    def process(self, x: np.ndarray) -> np.ndarray:
        y, self.zi = signal.sosfilt(self.sos, x, axis=-1, zi=self.zi)
        return y
