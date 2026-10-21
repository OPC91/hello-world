"""Correlation meter (overall + per band), mono warning and goniometer."""

from __future__ import annotations

import collections

import numpy as np
import pyqtgraph as pg
from PySide6.QtWidgets import QHBoxLayout, QLabel, QVBoxLayout, QWidget

from mastermind.dsp.metering.correlation import BAND_LABELS
from mastermind.gui.meters.widgets import CorrelationBar

GONIO_HISTORY = 2048


class CorrelationSection(QWidget):
    """Main correlation bar with peak hold + mono-compatibility warning."""

    def __init__(self):
        super().__init__()
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        row = QHBoxLayout()
        row.addWidget(QLabel("-1"))
        self.bar = CorrelationBar("Correlation", 26)
        row.addWidget(self.bar, 1)
        row.addWidget(QLabel("+1"))
        lay.addLayout(row)
        self.value_label = QLabel("—")
        self.warning = QLabel("⚠ Mono compatibility: correlation negative for > 1 s")
        self.warning.setStyleSheet("color:#f85149; font-weight:bold;")
        self.warning.setVisible(False)
        lay.addWidget(self.value_label)
        lay.addWidget(self.warning)

    def update_values(self, m: dict) -> None:
        self.bar.set_value(m["corr"], m["corr_hold"])
        self.value_label.setText(f"{m['corr']:+.2f}   (hold {m['corr_hold']:+.2f})")
        self.warning.setVisible(bool(m["mono_warning"]))


class StereoPanel(QWidget):
    def __init__(self):
        super().__init__()
        lay = QHBoxLayout(self)
        left = QVBoxLayout()
        self.correlation = CorrelationSection()
        left.addWidget(self.correlation)
        left.addWidget(QLabel("Per-band correlation"))
        self.bands = [CorrelationBar(lbl, 22) for lbl in BAND_LABELS]
        for b in self.bands:
            left.addWidget(b)
        self.band_warning = QLabel("")
        self.band_warning.setStyleSheet("color:#f85149;")
        left.addWidget(self.band_warning)
        left.addStretch(1)
        lay.addLayout(left, 1)

        self.gonio = pg.PlotWidget(title="Goniometer")
        self.gonio.setAspectLocked(True)
        self.gonio.setXRange(-1, 1)
        self.gonio.setYRange(-1, 1)
        self.gonio.hideAxis("bottom")
        self.gonio.hideAxis("left")
        self.gonio.setMouseEnabled(x=False, y=False)
        guide = pg.mkPen("#30363d")
        for xs, ys in (([-1, 1], [-1, 1]), ([-1, 1], [1, -1]), ([0, 0], [-1, 1]), ([-1, 1], [0, 0])):
            self.gonio.plot(xs, ys, pen=guide)
        for text, pos in (("L", (-0.75, 0.8)), ("R", (0.7, 0.8)), ("M", (0.03, 1.0)), ("S", (0.95, 0.08))):
            t = pg.TextItem(text, color="#8b949e")
            t.setPos(*pos)
            self.gonio.addItem(t)
        self.trace = self.gonio.plot(pen=pg.mkPen((88, 166, 255, 140), width=1))
        self._pts: collections.deque = collections.deque(maxlen=GONIO_HISTORY)
        self.gonio.setMinimumSize(260, 260)
        lay.addWidget(self.gonio, 1)

    def clear(self) -> None:
        self._pts.clear()
        self.trace.setData([], [])

    def push_gonio(self, x: np.ndarray, y: np.ndarray) -> None:
        self._pts.extend(zip(x.tolist(), y.tolist(), strict=True))

    def update_values(self, m: dict) -> None:
        self.correlation.update_values(m)
        for bar, v, flag in zip(self.bands, m["corr_bands"], m["corr_band_flags"], strict=True):
            bar.set_value(v, flag=flag)
        flagged = [lbl for lbl, f in zip(BAND_LABELS, m["corr_band_flags"], strict=True) if f]
        self.band_warning.setText(("Negative correlation in: " + ", ".join(flagged)) if flagged else "")
        if self._pts:
            arr = np.array(self._pts)
            peak = max(np.abs(arr).max(), 1e-6)
            scale = 1.0 / peak if peak < 1.0 else 1.0  # auto-gain so quiet material is visible
            self.trace.setData(arr[:, 0] * scale, arr[:, 1] * scale)
