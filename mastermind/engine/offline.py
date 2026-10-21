"""Offline rendering: the same block-based chain as real-time, run over a whole file."""

from __future__ import annotations

from collections.abc import Callable
from pathlib import Path

import numpy as np

from mastermind.dsp.chain import MasteringChain
from mastermind.dsp.modules.dither import Dither
from mastermind.io.export import BitDepth, write_wav

DEFAULT_BLOCK = 1024


def render(
    chain: MasteringChain,
    audio: np.ndarray,
    sample_rate: float,
    block_size: int = DEFAULT_BLOCK,
    progress: Callable[[float], bool | None] | None = None,
) -> np.ndarray:
    """Process ``audio`` (2, n) through ``chain`` and return a latency-aligned (2, n) result.

    The chain is (re)prepared, fed the input followed by ``latency`` samples of
    silence, and the first ``latency`` output samples are discarded, so the output
    lines up sample-for-sample with the input. ``progress`` receives a fraction
    in [0, 1]; returning ``False`` cancels the render (raises ``InterruptedError``).
    """
    chain.prepare(sample_rate, block_size)
    latency = chain.latency_samples
    n = audio.shape[-1]
    padded = np.concatenate([audio, np.zeros((audio.shape[0], latency))], axis=-1)
    out = np.empty_like(padded)
    total = padded.shape[-1]
    for start in range(0, total, block_size):
        stop = min(start + block_size, total)
        out[:, start:stop] = chain.process(padded[:, start:stop])
        if progress is not None and progress(stop / total) is False:
            raise InterruptedError("render cancelled")
    return out[:, latency : latency + n]


def render_to_file(
    chain: MasteringChain,
    audio: np.ndarray,
    sample_rate: int,
    path: str | Path,
    bit_depth: BitDepth = "24",
    dither: bool = True,
    seed: int = 0,
    block_size: int = DEFAULT_BLOCK,
    progress: Callable[[float], bool | None] | None = None,
) -> np.ndarray:
    """Render and write a WAV. Uses a copy of ``chain`` so live playback is untouched.

    Dither is applied only for integer formats, only if requested, and only if
    the chain's dither slot is not bypassed.
    """
    chain = chain.copy()
    dither_on = dither and bit_depth != "32f"
    try:
        idx = chain.index_of(Dither.type_id)
        dither_on = dither_on and not chain.slots[idx].bypass
        d: Dither = chain.slots[idx].processor  # type: ignore[assignment]
        d.arm(int(bit_depth) if dither_on else None, seed)
    except KeyError:
        pass
    y = render(chain, audio, sample_rate, block_size, progress)
    write_wav(path, y, sample_rate, bit_depth)
    return y
