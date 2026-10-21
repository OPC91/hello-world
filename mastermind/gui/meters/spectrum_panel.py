"""Real-time 1/3-octave-smoothed spectrum: input vs output, plus an optional reference curve."""

from __future__ import annotations

import numpy as np
import pyqtgraph as pg
from PySide6.QtWidgets import QVBoxLayout, QWidget

FREQ_TICKS = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]


def _label(f: float) -> str:
    return f"{f / 1000:g}k" if f >= 1000 else f"{f:g}"


class SpectrumPanel(QWidget):
    def __init__(self):
        super().__init__()
        lay = QVBoxLayout(self)
        self.plot = pg.PlotWidget()
        self.plot.setLogMode(x=True, y=False)
        self.plot.setXRange(np.log10(20), np.log10(20000), padding=0)
        self.plot.setYRange(-90, 0)
        self.plot.showGrid(x=True, y=True, alpha=0.2)
        self.plot.setLabel("left", "Level (1/3 oct)", units="dB")
        self.plot.getAxis("bottom").setTicks([[(np.log10(f), _label(f)) for f in FREQ_TICKS]])
        self.plot.addLegend(offset=(-10, 10))
        self.ref_lo = self.plot.plot(pen=None)
        self.ref_hi = self.plot.plot(pen=None)
        self.ref_band = pg.FillBetweenItem(self.ref_lo, self.ref_hi, brush=pg.mkBrush(210, 153, 34, 50))
        self.plot.addItem(self.ref_band)
        self.ref = self.plot.plot(
            pen=pg.mkPen("#d29922", width=2, style=pg.QtCore.Qt.PenStyle.DashLine), name="Reference"
        )
        self.inp = self.plot.plot(pen=pg.mkPen("#8b949e", width=1), name="Input")
        self.out = self.plot.plot(pen=pg.mkPen("#58a6ff", width=2), name="Output")
        lay.addWidget(self.plot)

    def update_values(self, grid: np.ndarray, in_db: np.ndarray | None, out_db: np.ndarray | None) -> None:
        if in_db is not None:
            self.inp.setData(grid, in_db)
        if out_db is not None:
            self.out.setData(grid, out_db)

    def set_reference(
        self,
        freqs: np.ndarray | None,
        db: np.ndarray | None = None,
        lo: np.ndarray | None = None,
        hi: np.ndarray | None = None,
    ) -> None:
        """Show a reference curve (Phase 4) with an optional variance band; ``None`` clears it."""
        if freqs is None:
            for c in (self.ref, self.ref_lo, self.ref_hi):
                c.setData([], [])
            return
        self.ref.setData(freqs, db)
        if lo is not None and hi is not None:
            self.ref_lo.setData(freqs, lo)
            self.ref_hi.setData(freqs, hi)
