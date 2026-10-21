"""WAV export at 16-bit, 24-bit or 32-bit float, with optional TPDF dither.

Integer formats are written from integer arrays that we scale ourselves
(full scale = 2^(bits-1)), so there is no ambiguity about libsndfile's
float->int scaling. When dither is requested the chain's :class:`Dither`
module has already quantised the signal to the exact grid; without dither the
signal is rounded to nearest (undithered requantisation).
"""

from __future__ import annotations

from pathlib import Path
from typing import Literal

import numpy as np
import soundfile as sf

BitDepth = Literal["16", "24", "32f"]
BIT_DEPTHS: tuple[str, ...] = ("16", "24", "32f")


def quantise_to_int(x: np.ndarray, bits: int) -> np.ndarray:
    """Round a float signal to signed integers of ``bits`` (clipped to range)."""
    scale = 2.0 ** (bits - 1)
    q = np.round(x * scale)
    return np.clip(q, -scale, scale - 1).astype(np.int32)


def write_wav(path: str | Path, x: np.ndarray, sample_rate: int, bit_depth: BitDepth = "24") -> None:
    """Write a ``(2, n)`` float signal to WAV."""
    path = Path(path)
    frames = np.ascontiguousarray(x.T)
    if bit_depth == "32f":
        sf.write(str(path), frames.astype(np.float32), sample_rate, subtype="FLOAT")
    elif bit_depth == "16":
        sf.write(str(path), quantise_to_int(frames, 16).astype(np.int16), sample_rate, subtype="PCM_16")
    elif bit_depth == "24":
        # libsndfile takes the top 24 bits of an int32.
        sf.write(str(path), quantise_to_int(frames, 24) << 8, sample_rate, subtype="PCM_24")
    else:
        raise ValueError(f"unsupported bit depth {bit_depth!r}")
