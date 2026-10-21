"""TPDF dither with optional first-order noise shaping. Export-only.

In real-time preview the module is a pass-through. The offline exporter calls
:meth:`Dither.arm` with the target bit depth, after which :meth:`process`
quantises to that grid (values are exact multiples of ``2^-(bits-1)``), so the
writer only needs to scale and cast.

TPDF: the sum of two independent uniform variables of +-0.5 LSB, i.e. a
triangular PDF spanning +-1 LSB, which makes the first two moments of the
quantisation error independent of the signal.

Noise shaping (error feedback, ``E(z) * (1 - z^-1)``) pushes the noise floor up
in frequency, where hearing is less sensitive.

JUCE mapping: no built-in; a few lines in ``processBlock`` with ``juce::Random``.
"""

from __future__ import annotations

import numpy as np
from numba import njit

from mastermind.dsp.base import ParamSpec, Processor, registry


@njit(cache=True)
def _quantise_shaped(x: np.ndarray, noise: np.ndarray, lsb: float, err: np.ndarray) -> np.ndarray:
    ch, n = x.shape
    y = np.empty_like(x)
    lo = -1.0
    hi = 1.0 - lsb
    for c in range(ch):
        e = err[c]
        for i in range(n):
            v = x[c, i] - e
            q = np.floor((v + noise[c, i]) / lsb + 0.5) * lsb
            if q < lo:
                q = lo
            elif q > hi:
                q = hi
            e = q - v
            y[c, i] = q
        err[c] = e
    return y


@registry.register
class Dither(Processor):
    type_id = "dither"
    display_name = "Dither (export only)"
    pinned_last = True

    @classmethod
    def param_specs(cls) -> list[ParamSpec]:
        return [
            ParamSpec("noise_shaping", "Noise Shaping", False, kind="bool"),
        ]

    def __init__(self, **params):
        super().__init__(**params)
        self._bits: int | None = None
        self._rng = np.random.default_rng(0)

    def arm(self, bits: int | None, seed: int = 0) -> None:
        """Enable quantisation to ``bits`` (16/24); ``None`` disarms (pass-through)."""
        self._bits = bits
        self._rng = np.random.default_rng(seed)
        self._err = np.zeros(2)

    @property
    def armed_bits(self) -> int | None:
        return self._bits

    def _prepare(self) -> None:
        self._err = np.zeros(2)

    def process(self, block: np.ndarray) -> np.ndarray:
        if self._bits is None:
            return block
        lsb = 2.0 ** -(self._bits - 1)
        noise = (self._rng.random(block.shape) - self._rng.random(block.shape)) * lsb
        if self.params.get("noise_shaping"):
            return _quantise_shaped(np.ascontiguousarray(block), noise, lsb, self._err)
        q = np.floor((block + noise) / lsb + 0.5) * lsb
        return np.clip(q, -1.0, 1.0 - lsb)
