"""Fit the limiter gain so the rendered master hits a target integrated loudness.

Rendering a whole song for every iteration is too slow for interactive use, so
the fit renders a *probe*: up to ``n_segments`` evenly spaced excerpts (each
with a pre-roll so compressors settle; the pre-roll isn't measured) and pools
their 400 ms gating blocks into one BS.1770 gated integrated loudness. Files
shorter than ``n_segments * segment_s`` are rendered whole.

Loudness vs limiter gain is monotonic and close to 1:1 until the limiter
saturates, so a secant search converges in 2-4 renders. Everything *before*
the limiter in the chain does not depend on the limiter gain, so that part is
rendered once and only the limiter (and anything after it) is re-run per
iteration.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from mastermind.dsp.chain import MasteringChain
from mastermind.dsp.metering.loudness import LoudnessMeter, gated_integrated
from mastermind.engine.offline import render

DRIVE_RANGE = (-12.0, 24.0)


@dataclass
class LoudnessFit:
    drive_db: float
    achieved_lufs: float
    target_lufs: float
    renders: int
    converged: bool


def probe_segments(n: int, sr: float, n_segments: int = 6, segment_s: float = 5.0, preroll_s: float = 1.0):
    """(start, stop, measure_from) sample indices of the probe excerpts."""
    seg, pre = int(segment_s * sr), int(preroll_s * sr)
    if n <= n_segments * (seg + pre):
        return [(0, n, 0)]
    starts = np.linspace(pre, n - seg, n_segments).astype(int)
    return [(int(s) - pre, int(s) + seg, pre) for s in starts]


def probe_loudness(chain: MasteringChain, excerpts: list[tuple[np.ndarray, int]], sr: float) -> float:
    """Pooled gated loudness of ``chain`` applied to pre-cut ``(audio, measure_from)`` excerpts."""
    blocks: list[float] = []
    for audio, measure_from in excerpts:
        y = render(chain, audio, sr)
        m = LoudnessMeter(sr, true_peak=False)
        m.process(y[:, measure_from:])
        blocks += m.block_powers
    return gated_integrated(np.array(blocks))


def split_at(chain: MasteringChain, type_id: str) -> tuple[MasteringChain, MasteringChain]:
    """Two independent chains: slots before ``type_id`` and from ``type_id`` on."""
    work = chain.copy()
    i = work.index_of(type_id)
    return MasteringChain(work.slots[:i]), MasteringChain(work.slots[i:])


def fit_limiter_drive(
    chain: MasteringChain,
    x: np.ndarray,
    sr: float,
    target_lufs: float,
    tolerance: float = 0.2,
    max_renders: int = 6,
    **probe_kw,
) -> LoudnessFit:
    """Find the limiter ``drive_db`` that brings the master to ``target_lufs``.

    ``chain`` is not modified (a copy is rendered). Returns the best drive found;
    ``converged`` is False if the target is unreachable within the drive range
    (e.g. asking for -5 LUFS from a very dynamic mix).
    """
    pre, post = split_at(chain, "limiter")
    lim = post.find("limiter")
    segs = probe_segments(x.shape[-1], sr, **probe_kw)
    excerpts = [(render(pre, x[:, a:b], sr), m) for a, b, m in segs]
    lo, hi = DRIVE_RANGE

    def loud(d: float) -> float:
        lim.set_param("drive_db", d)
        return probe_loudness(post, excerpts, sr)

    d0 = float(lim.get_param("drive_db"))
    l0 = loud(d0)
    renders = 1
    best = (abs(l0 - target_lufs), d0, l0)
    if not np.isfinite(l0):
        return LoudnessFit(d0, l0, target_lufs, renders, False)
    d1 = float(np.clip(d0 + (target_lufs - l0), lo, hi))
    while renders < max_renders and best[0] > tolerance:
        if d1 == d0:
            break
        l1 = loud(d1)
        renders += 1
        if abs(l1 - target_lufs) < best[0]:
            best = (abs(l1 - target_lufs), d1, l1)
        slope = (l1 - l0) / (d1 - d0)
        d0, l0 = d1, l1
        # Loudness can't grow faster than gain; a flat slope means the limiter is saturating.
        slope = float(np.clip(slope, 0.05, 1.0))
        d1 = float(np.clip(d1 + (target_lufs - l1) / slope, lo, hi))
    err, d, achieved = best
    return LoudnessFit(round(d, 2), achieved, target_lufs, renders, err <= tolerance)
