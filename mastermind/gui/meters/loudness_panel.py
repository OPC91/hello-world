"""LUFS readouts, bars, secondary stats and the scrolling short-term graph."""

from __future__ import annotations

import collections

import numpy as np
import pyqtgraph as pg
from PySide6.QtCore import Signal
from PySide6.QtWidgets import QComboBox, QDoubleSpinBox, QGridLayout, QHBoxLayout, QPushButton, QVBoxLayout, QWidget

from mastermind.dsp.metering.targets import LOUDNESS_TARGETS, loudness_status, peak_status
from mastermind.gui.meters.widgets import STATUS_COLORS, LevelBar, Readout

LUFS_RANGE = (-40.0, 0.0)
HISTORY_S = 30.0


class LoudnessPanel(QWidget):
    reset_requested = Signal()
    target_changed = Signal(float)

    def __init__(self):
        super().__init__()
        lay = QVBoxLayout(self)

        top = QHBoxLayout()
        self.target_box = QComboBox()
        self.target_box.addItems([f"{k} ({v:g})" if v is not None else k for k, v in LOUDNESS_TARGETS.items()])
        self.custom = QDoubleSpinBox()
        self.custom.setRange(-40, 0)
        self.custom.setValue(-12.0)
        self.custom.setSuffix(" LUFS")
        self.custom.setEnabled(False)
        reset = QPushButton("Reset")
        reset.clicked.connect(self.reset_requested.emit)
        top.addWidget(self.target_box, 1)
        top.addWidget(self.custom)
        top.addWidget(reset)
        lay.addLayout(top)
        self.target_box.currentIndexChanged.connect(self._target_changed)
        self.custom.valueChanged.connect(self._target_changed)

        grid = QGridLayout()
        self.m = Readout("MOMENTARY", "LUFS")
        self.s = Readout("SHORT-TERM", "LUFS")
        self.i = Readout("INTEGRATED", "LUFS", size=26)
        self.lra = Readout("LRA", "LU", size=13)
        self.tp = Readout("TRUE PEAK", "dBTP", size=13)
        self.plr = Readout("PLR", "dB", size=13)
        self.crest = Readout("CREST (3 s)", "dB", size=13)
        for col, w in enumerate((self.m, self.s, self.i)):
            grid.addWidget(w, 0, col)
        for col, w in enumerate((self.lra, self.tp, self.plr, self.crest)):
            grid.addWidget(w, 1 if col < 2 else 2, col % 2)
        bars = QHBoxLayout()
        self.bar_m = LevelBar(*LUFS_RANGE, "M")
        self.bar_s = LevelBar(*LUFS_RANGE, "S")
        self.bar_i = LevelBar(*LUFS_RANGE, "I")
        for b in (self.bar_m, self.bar_s, self.bar_i):
            bars.addWidget(b)
        row = QHBoxLayout()
        row.addLayout(grid, 1)
        row.addLayout(bars)
        lay.addLayout(row)

        self.plot = pg.PlotWidget()
        self.plot.setMinimumHeight(140)
        self.plot.setYRange(-40, 0)
        self.plot.setLabel("left", "Short-term", units="LUFS")
        self.plot.showGrid(y=True, alpha=0.2)
        self.plot.setMouseEnabled(x=False, y=False)
        self.curve = self.plot.plot(pen=pg.mkPen("#58a6ff", width=2))
        self.target_line = pg.InfiniteLine(angle=0, pen=pg.mkPen("#ffffff", style=pg.QtCore.Qt.PenStyle.DashLine))
        self.plot.addItem(self.target_line)
        lay.addWidget(self.plot)
        self._hist: dict[str, collections.deque] = {}
        self._target_changed()

    # ---- target -----------------------------------------------------
    @property
    def target(self) -> float:
        name = list(LOUDNESS_TARGETS)[self.target_box.currentIndex()]
        v = LOUDNESS_TARGETS[name]
        return self.custom.value() if v is None else v

    def _target_changed(self, *_):
        name = list(LOUDNESS_TARGETS)[self.target_box.currentIndex()]
        self.custom.setEnabled(LOUDNESS_TARGETS[name] is None)
        t = self.target
        self.target_line.setPos(t)
        for b in (self.bar_m, self.bar_s, self.bar_i):
            b.marker = t
            b.update()
        self.target_changed.emit(t)

    # ---- data -------------------------------------------------------
    def clear_history(self) -> None:
        self._hist.clear()
        self.curve.setData([], [])

    def push_history(self, source: str, t: float, short_term: float) -> None:
        d = self._hist.setdefault(source, collections.deque(maxlen=4000))
        d.append((t, short_term))

    def update_values(self, m: dict, source: str, ceiling: float) -> None:
        t = self.target
        st = {k: loudness_status(m[k], t) for k in ("momentary", "short_term", "integrated")}
        self.m.set_value(m["momentary"], st["momentary"])
        self.s.set_value(m["short_term"], st["short_term"])
        self.i.set_value(m["integrated"], st["integrated"])
        self.bar_m.set_value(m["momentary"], STATUS_COLORS[st["momentary"]], m["max_momentary"])
        self.bar_s.set_value(m["short_term"], STATUS_COLORS[st["short_term"]], m["max_short_term"])
        self.bar_i.set_value(m["integrated"], STATUS_COLORS[st["integrated"]])
        self.lra.set_value(m["lra"])
        self.tp.set_value(m["true_peak"], peak_status(m["true_peak"], ceiling))
        self.plr.set_value(m["plr"])
        self.crest.set_value(m["crest"])

        d = self._hist.get(source)
        if d:
            arr = np.array(d)
            now = arr[-1, 0]
            sel = arr[:, 0] >= now - HISTORY_S
            y = np.where(np.isfinite(arr[sel, 1]), arr[sel, 1], np.nan)
            self.curve.setData(arr[sel, 0] - now, y, connect="finite")
            self.plot.setXRange(-HISTORY_S, 0, padding=0)

    def readout_status(self) -> dict[str, str]:
        """Current colour status of each readout (used by tests)."""
        return {"M": self.m.status, "S": self.s.status, "I": self.i.status, "TP": self.tp.status}
