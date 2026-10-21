"""Loudness / peak report used by the CLI (extended with full metering in Phase 2)."""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np
import pyloudnorm

from mastermind.dsp.primitives.truepeak import true_peak_dbtp


@dataclass
class Report:
    integrated_lufs: float
    sample_peak_dbfs: float
    true_peak_dbtp: float
    plr_db: float
    correlation: float
    duration_s: float

    def as_dict(self) -> dict:
        return asdict(self)

    def format(self, title: str = "") -> str:
        lines = [title] if title else []
        lines += [
            f"  Integrated loudness : {self.integrated_lufs:7.2f} LUFS",
            f"  True peak           : {self.true_peak_dbtp:7.2f} dBTP",
            f"  Sample peak         : {self.sample_peak_dbfs:7.2f} dBFS",
            f"  PLR (TP - LUFS)     : {self.plr_db:7.2f} dB",
            f"  Phase correlation   : {self.correlation:7.3f}",
            f"  Duration            : {self.duration_s:7.2f} s",
        ]
        return "\n".join(lines)


def analyse(x: np.ndarray, sample_rate: float) -> Report:
    meter = pyloudnorm.Meter(sample_rate)
    lufs = float(meter.integrated_loudness(x.T)) if x.shape[-1] > 0.4 * sample_rate else float("-inf")
    peak = float(np.abs(x).max()) if x.size else 0.0
    sp = 20 * np.log10(max(peak, 1e-12))
    tp = true_peak_dbtp(x)
    l, r = x[0], x[1]
    denom = np.sqrt(np.dot(l, l) * np.dot(r, r))
    corr = float(np.dot(l, r) / denom) if denom > 0 else 0.0
    return Report(lufs, sp, tp, tp - lufs, corr, x.shape[-1] / sample_rate)
