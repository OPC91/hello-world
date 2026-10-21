"""Streaming loudness meter per ITU-R BS.1770-4 / EBU R128 / EBU Tech 3341-3342.

The K-weighted mean square is accumulated in 100 ms *hops*. From the hop history:

momentary (M)  mean of the last 4 hops (400 ms window, updated every 100 ms)
short-term (S) mean of the last 30 hops (3 s window)
integrated (I) two-stage gated mean of all 400 ms blocks (75 % overlap):
               absolute gate -70 LUFS, then relative gate -10 LU below the
               absolute-gated mean
LRA            EBU Tech 3342: short-term values (every 100 ms, once a full 3 s
               window exists) gated at -70 LUFS absolute and -20 LU relative;
               LRA = 95th - 10th percentile of the remaining values

Loudness of a mean-square power ``z`` (sum over channels, all weights 1 for
stereo): ``L = -0.691 + 10 log10(z)``. A stereo 1 kHz sine at -23 dBFS peak per
channel reads -23.0 LUFS.

Also tracked: maximum true peak (BS.1770 Annex 2, 4x), sample peak, and
unweighted RMS for crest factor. PLR = true peak (dBTP) - integrated (LUFS).

JUCE mapping: K-weighting = 2 ``IIR::Filter``; the hop accumulator and gating
are plain C++ (``std::vector<float>`` of block powers).
"""

from __future__ import annotations

import collections
import math

import numpy as np

from mastermind.dsp.metering.kweight import KWeighting
from mastermind.dsp.primitives.truepeak import TruePeakDetector

LUFS_OFFSET = -0.691
ABS_GATE_LUFS = -70.0
REL_GATE_LU = -10.0
LRA_REL_GATE_LU = -20.0
HOP_S = 0.1
MOMENTARY_HOPS = 4
SHORT_TERM_HOPS = 30
NEG_INF = float("-inf")


def power_to_lufs(z: float | np.ndarray) -> float | np.ndarray:
    with np.errstate(divide="ignore"):
        return LUFS_OFFSET + 10.0 * np.log10(z)


def lufs_to_power(lufs: float) -> float:
    return 10.0 ** ((lufs - LUFS_OFFSET) / 10.0)


def gated_integrated(block_powers: np.ndarray) -> float:
    """BS.1770 two-stage gated loudness of 400 ms block powers."""
    z = np.asarray(block_powers, dtype=float)
    z = z[z > lufs_to_power(ABS_GATE_LUFS)]
    if z.size == 0:
        return NEG_INF
    rel = power_to_lufs(z.mean()) + REL_GATE_LU
    z = z[z > lufs_to_power(rel)]
    return float(power_to_lufs(z.mean())) if z.size else NEG_INF


def loudness_range(short_term_powers: np.ndarray) -> float:
    """EBU Tech 3342 loudness range from short-term (3 s) powers."""
    z = np.asarray(short_term_powers, dtype=float)
    z = z[z > lufs_to_power(ABS_GATE_LUFS)]
    if z.size == 0:
        return 0.0
    rel = power_to_lufs(z.mean()) + LRA_REL_GATE_LU
    z = z[z > lufs_to_power(rel)]
    if z.size == 0:
        return 0.0
    st = power_to_lufs(z)
    lo, hi = np.percentile(st, [10, 95])
    return float(hi - lo)


def _db(x: float) -> float:
    return 20.0 * math.log10(x) if x > 0 else NEG_INF


class LoudnessMeter:
    """Streaming M / S / I / LRA / true-peak / crest meter for stereo blocks."""

    def __init__(self, sample_rate: float, channels: int = 2, true_peak: bool = True):
        self.sample_rate = sample_rate
        self.channels = channels
        self.hop = int(round(HOP_S * sample_rate))
        self._kw = KWeighting(sample_rate, channels)
        # True-peak detection is the most expensive part; loudness-only meters
        # (e.g. A/B loudness matching) can skip it.
        self._tp = TruePeakDetector(channels) if true_peak else None
        self.reset()

    def reset(self) -> None:
        self._kw.reset()
        if self._tp is not None:
            self._tp.reset()
        self._acc_z = 0.0  # K-weighted energy in current hop (summed over channels)
        self._acc_sq = 0.0  # unweighted energy (mean over channels) in current hop
        self._acc_pk = 0.0
        self._acc_n = 0
        self._hops: collections.deque[float] = collections.deque(maxlen=SHORT_TERM_HOPS)
        self._hop_sq: collections.deque[float] = collections.deque(maxlen=SHORT_TERM_HOPS)
        self._hop_pk: collections.deque[float] = collections.deque(maxlen=SHORT_TERM_HOPS)
        self._n_hops = 0
        self.block_powers: list[float] = []  # 400 ms gating blocks, one per hop
        self.st_powers: list[float] = []  # 3 s short-term powers, one per hop (LRA)
        self.momentary = NEG_INF
        self.short_term = NEG_INF
        self.max_momentary = NEG_INF
        self.max_short_term = NEG_INF
        self.true_peak = 0.0  # linear, running max
        self.block_true_peak = 0.0  # linear, max over the last process() call
        self.sample_peak = 0.0
        self._total_sq = 0.0
        self._total_n = 0
        self._integrated_cache: tuple[int, float] | None = None
        self._lra_cache: tuple[int, float] | None = None

    # ---- streaming -----------------------------------------------------
    def process(self, x: np.ndarray) -> None:
        n = x.shape[-1]
        if n == 0:
            return
        y = self._kw.process(x)
        z = np.sum(y * y, axis=0)  # channel weights are 1 for L/R
        sq = np.mean(x * x, axis=0)
        ab = np.abs(x).max(axis=0)
        tp_max = float(self._tp.process(x).max()) if self._tp is not None else 0.0
        self.block_true_peak = float(max(tp_max, ab.max()))
        self.true_peak = max(self.true_peak, self.block_true_peak)
        self.sample_peak = max(self.sample_peak, float(ab.max()))
        self._total_sq += float(sq.sum())
        self._total_n += n

        i = 0
        while i < n:
            take = min(n - i, self.hop - self._acc_n)
            self._acc_z += float(z[i : i + take].sum())
            self._acc_sq += float(sq[i : i + take].sum())
            self._acc_pk = max(self._acc_pk, float(ab[i : i + take].max()))
            self._acc_n += take
            i += take
            if self._acc_n == self.hop:
                self._complete_hop()

    def _complete_hop(self) -> None:
        self._hops.append(self._acc_z / self.hop)
        self._hop_sq.append(self._acc_sq / self.hop)
        self._hop_pk.append(self._acc_pk)
        self._acc_z = self._acc_sq = self._acc_pk = 0.0
        self._acc_n = 0
        self._n_hops += 1
        hops = list(self._hops)
        m = sum(hops[-MOMENTARY_HOPS:]) / MOMENTARY_HOPS  # zero-padded before 400 ms
        s = sum(hops) / SHORT_TERM_HOPS  # zero-padded before 3 s
        self.momentary = float(power_to_lufs(m))
        self.short_term = float(power_to_lufs(s))
        if self._n_hops >= MOMENTARY_HOPS:
            self.block_powers.append(m)
            self.max_momentary = max(self.max_momentary, self.momentary)
        if self._n_hops >= SHORT_TERM_HOPS:
            self.st_powers.append(s)
            self.max_short_term = max(self.max_short_term, self.short_term)

    # ---- derived values ------------------------------------------------
    @property
    def integrated(self) -> float:
        n = len(self.block_powers)
        if self._integrated_cache is None or self._integrated_cache[0] != n:
            self._integrated_cache = (n, gated_integrated(np.array(self.block_powers)))
        return self._integrated_cache[1]

    def recent_integrated(self, seconds: float) -> float:
        """Gated loudness of the most recent ``seconds`` (sliding long-term loudness)."""
        n = max(1, int(round(seconds / HOP_S)))
        return gated_integrated(np.array(self.block_powers[-n:]))

    @property
    def lra(self) -> float:
        n = len(self.st_powers)
        if self._lra_cache is None or self._lra_cache[0] != n:
            self._lra_cache = (n, loudness_range(np.array(self.st_powers)))
        return self._lra_cache[1]

    @property
    def true_peak_dbtp(self) -> float:
        return _db(self.true_peak)

    @property
    def sample_peak_dbfs(self) -> float:
        return _db(self.sample_peak)

    @property
    def plr(self) -> float:
        """Peak-to-loudness ratio: true peak (dBTP) - integrated (LUFS)."""
        i = self.integrated
        return self.true_peak_dbtp - i if math.isfinite(i) else float("nan")

    @property
    def crest_db(self) -> float:
        """Whole-programme crest factor: sample peak - RMS (dB), unweighted."""
        if self._total_n == 0 or self._total_sq <= 0:
            return float("nan")
        return self.sample_peak_dbfs - 10 * math.log10(self._total_sq / self._total_n)

    @property
    def short_term_crest_db(self) -> float:
        """Crest factor over the last 3 s (sample peak - RMS)."""
        if not self._hop_sq:
            return float("nan")
        ms = sum(self._hop_sq) / len(self._hop_sq)
        pk = max(self._hop_pk)
        if ms <= 0 or pk <= 0:
            return float("nan")
        return _db(pk) - 10 * math.log10(ms)
