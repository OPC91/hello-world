"""Text / JSON loudness, peak and correlation report (CLI)."""

from __future__ import annotations

import math

import numpy as np

from mastermind.dsp.metering.analysis import FileAnalysis, analyze


def _f(v: float, fmt: str = "7.2f") -> str:
    return (
        format(v, fmt)
        if isinstance(v, (int, float)) and math.isfinite(v)
        else "   -inf"
        if v == float("-inf")
        else "    n/a"
    )


def format_report(
    a: FileAnalysis, title: str = "", target_lufs: float | None = None, ceiling: float | None = None
) -> str:
    lines = [title] if title else []
    integ = f"  Integrated loudness : {_f(a.integrated_lufs)} LUFS"
    if target_lufs is not None and math.isfinite(a.integrated_lufs):
        integ += f"   ({a.integrated_lufs - target_lufs:+.1f} LU vs target {target_lufs:g})"
    tp = f"  True peak           : {_f(a.true_peak_dbtp)} dBTP"
    if ceiling is not None and a.true_peak_dbtp > ceiling:
        tp += f"   OVER ceiling {ceiling:g}"
    lines += [
        integ,
        f"  Loudness range      : {_f(a.lra_lu)} LU",
        f"  Max momentary       : {_f(a.max_momentary_lufs)} LUFS",
        f"  Max short-term      : {_f(a.max_short_term_lufs)} LUFS",
        tp,
        f"  Sample peak         : {_f(a.sample_peak_dbfs)} dBFS",
        f"  PLR (TP - I)        : {_f(a.plr_db)} dB",
        f"  Crest factor        : {_f(a.crest_db)} dB",
        f"  Phase correlation   : {_f(a.correlation, '7.3f')}"
        f"   (time below 0: {a.negative_correlation_fraction * 100:.1f} %)",
        "  Band correlation    : "
        + "  ".join(
            f"{lbl} {v:+.2f}{'!' if v < 0 else ''}" for lbl, v in zip(a.band_labels, a.band_correlation, strict=True)
        ),
    ]
    if a.mono_warning:
        lines.append(f"  WARNING: correlation negative for {a.longest_negative_run_s:.1f} s - check mono compatibility")
    lines.append(f"  Duration            : {_f(a.duration_s)} s")
    return "\n".join(lines)


def analyse(x: np.ndarray, sample_rate: float) -> FileAnalysis:
    return analyze(x, sample_rate)
