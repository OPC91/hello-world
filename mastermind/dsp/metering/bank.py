"""A bundle of meters fed from one tap point (pre- or post-chain).

The real-time engine owns two banks (input and output) and pushes
:meth:`MeterBank.snapshot` dicts to the GUI. The offline analyser uses the same
classes, so live and full-file numbers come from identical code.
"""

from __future__ import annotations

from typing import Any

import numpy as np

from mastermind.dsp.metering.correlation import CorrelationMeter, goniometer_points
from mastermind.dsp.metering.loudness import LoudnessMeter
from mastermind.dsp.metering.spectrum import SpectrumAnalyzer

GONIO_POINTS = 512


class MeterBank:
    def __init__(self, sample_rate: float, spectrum_fps: float = 30.0):
        self.sample_rate = sample_rate
        self.loudness = LoudnessMeter(sample_rate)
        self.correlation = CorrelationMeter(sample_rate)
        self.spectrum = SpectrumAnalyzer(sample_rate)
        self._spec_interval = int(sample_rate / spectrum_fps)
        self._since_spec = self._spec_interval
        self._last_block = np.zeros((2, 0))
        self._spec_db: np.ndarray | None = None

    def reset(self) -> None:
        self.loudness.reset()
        self.correlation.reset()
        self.spectrum.reset()
        self._spec_db = None

    def process(self, x: np.ndarray) -> None:
        self.loudness.process(x)
        self.correlation.process(x)
        self.spectrum.push(x)
        self._since_spec += x.shape[-1]
        self._last_block = x

    def snapshot(self) -> dict[str, Any]:
        """Current readings; spectrum is recomputed at most ``spectrum_fps`` times per second."""
        if self._since_spec >= self._spec_interval:
            self._spec_db = self.spectrum.compute()
            self._since_spec = 0
        L, C = self.loudness, self.correlation
        gx, gy = goniometer_points(self._last_block, GONIO_POINTS)
        return {
            "momentary": L.momentary,
            "short_term": L.short_term,
            "integrated": L.integrated,
            "lra": L.lra,
            "max_momentary": L.max_momentary,
            "max_short_term": L.max_short_term,
            "true_peak": L.true_peak_dbtp,
            "block_true_peak": 20 * np.log10(max(L.block_true_peak, 1e-12)),
            "sample_peak": L.sample_peak_dbfs,
            "plr": L.plr,
            "crest": L.short_term_crest_db,
            "corr": C.value,
            "corr_hold": C.hold,
            "corr_bands": list(C.band_values),
            "corr_band_flags": C.band_flags,
            "mono_warning": C.mono_warning,
            "spectrum": self._spec_db,
            "gonio": (gx, gy),
        }
