"""Full-file analysis built from the streaming meters.

Used by the CLI report, the GUI's analysis panel and (Phase 4) reference
learning. Every number comes from the same code that drives the live meters.
"""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass, field

import numpy as np

from mastermind.dsp.metering.correlation import BAND_LABELS, CorrelationMeter, correlation
from mastermind.dsp.metering.loudness import HOP_S, SHORT_TERM_HOPS, LoudnessMeter, power_to_lufs
from mastermind.dsp.metering.spectrum import long_term_spectrum

ANALYSIS_BLOCK = 4096


@dataclass
class FileAnalysis:
    sample_rate: float
    duration_s: float
    integrated_lufs: float
    lra_lu: float
    max_momentary_lufs: float
    max_short_term_lufs: float
    true_peak_dbtp: float
    sample_peak_dbfs: float
    plr_db: float
    crest_db: float
    correlation: float
    mean_correlation: float
    negative_correlation_fraction: float
    longest_negative_run_s: float
    band_correlation: list[float]
    band_labels: list[str]
    st_times: np.ndarray = field(repr=False)
    st_lufs: np.ndarray = field(repr=False)
    spectrum_freqs: np.ndarray = field(repr=False)
    spectrum_db: np.ndarray = field(repr=False)

    @property
    def mono_warning(self) -> bool:
        return self.longest_negative_run_s > 1.0

    def summary(self) -> dict:
        d = asdict(self)
        for k in ("st_times", "st_lufs", "spectrum_freqs", "spectrum_db"):
            d.pop(k)
        d["mono_warning"] = self.mono_warning
        return {k: (None if isinstance(v, float) and not math.isfinite(v) else v) for k, v in d.items()}


def analyze(x: np.ndarray, sample_rate: float, block: int = ANALYSIS_BLOCK) -> FileAnalysis:
    """Analyse a whole ``(2, n)`` signal."""
    lm = LoudnessMeter(sample_rate)
    cm = CorrelationMeter(sample_rate)
    band_acc = np.zeros(4)
    band_n = 0
    longest = 0.0
    for start in range(0, x.shape[-1], block):
        b = x[:, start : start + block]
        lm.process(b)
        cm.process(b)
        band_acc += np.array(cm.band_values) * b.shape[-1]
        band_n += b.shape[-1]
        longest = max(longest, cm.negative_run_s)

    st = np.array(lm.st_powers)
    st_lufs = power_to_lufs(st) if st.size else np.zeros(0)
    st_times = (SHORT_TERM_HOPS + np.arange(st.size)) * HOP_S
    freqs, spec = long_term_spectrum(x, sample_rate)
    return FileAnalysis(
        sample_rate=sample_rate,
        duration_s=x.shape[-1] / sample_rate,
        integrated_lufs=lm.integrated,
        lra_lu=lm.lra,
        max_momentary_lufs=lm.max_momentary,
        max_short_term_lufs=lm.max_short_term,
        true_peak_dbtp=lm.true_peak_dbtp,
        sample_peak_dbfs=lm.sample_peak_dbfs,
        plr_db=lm.plr,
        crest_db=lm.crest_db,
        correlation=correlation(x),
        mean_correlation=cm.mean,
        negative_correlation_fraction=cm.negative_fraction,
        longest_negative_run_s=longest,
        band_correlation=list(band_acc / max(band_n, 1)),
        band_labels=list(BAND_LABELS),
        st_times=st_times,
        st_lufs=np.asarray(st_lufs),
        spectrum_freqs=freqs,
        spectrum_db=spec,
    )
