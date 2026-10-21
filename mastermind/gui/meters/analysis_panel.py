"""Full-file analysis: input vs rendered output, with short-term loudness and LTAS plots."""

from __future__ import annotations

import numpy as np
import pyqtgraph as pg
from PySide6.QtCore import Signal
from PySide6.QtGui import QColor
from PySide6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget

from mastermind.dsp.metering.analysis import FileAnalysis
from mastermind.dsp.metering.targets import loudness_status, peak_status
from mastermind.gui.meters.spectrum_panel import FREQ_TICKS, _label
from mastermind.gui.meters.widgets import STATUS_COLORS, fmt

ROWS = [
    ("Integrated (LUFS)", "integrated_lufs"),
    ("Loudness range (LU)", "lra_lu"),
    ("Max momentary (LUFS)", "max_momentary_lufs"),
    ("Max short-term (LUFS)", "max_short_term_lufs"),
    ("True peak (dBTP)", "true_peak_dbtp"),
    ("Sample peak (dBFS)", "sample_peak_dbfs"),
    ("PLR (dB)", "plr_db"),
    ("Crest factor (dB)", "crest_db"),
    ("Correlation", "correlation"),
    ("Time correlation < 0 (%)", "negative_correlation_fraction"),
]


class AnalysisPanel(QWidget):
    analyze_requested = Signal()

    def __init__(self):
        super().__init__()
        lay = QVBoxLayout(self)
        top = QHBoxLayout()
        self.button = QPushButton("Analyze full file (input + rendered output)")
        self.button.clicked.connect(self.analyze_requested.emit)
        self.status = QLabel("")
        top.addWidget(self.button)
        top.addWidget(self.status, 1)
        lay.addLayout(top)

        body = QHBoxLayout()
        self.table = QTableWidget(len(ROWS) + 5, 2)
        self.table.setHorizontalHeaderLabels(["Input", "Output"])
        self.table.setVerticalHeaderLabels(
            [r[0] for r in ROWS] + [f"Corr {b}" for b in ("Low", "Low-Mid", "High-Mid", "High")] + ["Mono warning"]
        )
        self.table.setMinimumWidth(330)
        self.table.setMaximumWidth(380)
        body.addWidget(self.table)

        plots = QVBoxLayout()
        self.st_plot = pg.PlotWidget(title="Short-term loudness")
        self.st_plot.setLabel("left", "LUFS")
        self.st_plot.setLabel("bottom", "Time", units="s")
        self.st_plot.addLegend()
        self.st_in = self.st_plot.plot(pen=pg.mkPen("#8b949e"), name="Input")
        self.st_out = self.st_plot.plot(pen=pg.mkPen("#58a6ff", width=2), name="Output")
        self.st_target = pg.InfiniteLine(angle=0, pen=pg.mkPen("#ffffff", style=pg.QtCore.Qt.PenStyle.DashLine))
        self.st_plot.addItem(self.st_target)
        self.spec_plot = pg.PlotWidget(title="Long-term average spectrum (1/3 oct)")
        self.spec_plot.setLogMode(x=True)
        self.spec_plot.getAxis("bottom").setTicks([[(np.log10(f), _label(f)) for f in FREQ_TICKS]])
        self.spec_in = self.spec_plot.plot(pen=pg.mkPen("#8b949e"), symbol="o", symbolSize=4)
        self.spec_out = self.spec_plot.plot(pen=pg.mkPen("#58a6ff", width=2), symbol="o", symbolSize=4)
        plots.addWidget(self.st_plot)
        plots.addWidget(self.spec_plot)
        body.addLayout(plots, 1)
        lay.addLayout(body, 1)
        self.results: tuple[FileAnalysis, FileAnalysis] | None = None

    def set_busy(self, msg: str) -> None:
        self.button.setEnabled(False)
        self.status.setText(msg)

    def set_error(self, msg: str) -> None:
        self.button.setEnabled(True)
        self.status.setText(msg)

    def set_results(self, a_in: FileAnalysis, a_out: FileAnalysis, target: float, ceiling: float) -> None:
        self.results = (a_in, a_out)
        self.button.setEnabled(True)
        self.status.setText(f"Target {target:g} LUFS, ceiling {ceiling:g} dBTP")
        for col, a in enumerate((a_in, a_out)):
            for row, (_, key) in enumerate(ROWS):
                v = getattr(a, key)
                if key == "negative_correlation_fraction":
                    v *= 100
                dec = 3 if key == "correlation" else 1
                item = QTableWidgetItem(fmt(v, dec))
                status = None
                if key == "integrated_lufs":
                    status = loudness_status(v, target)
                elif key == "true_peak_dbtp":
                    status = peak_status(v, ceiling)
                elif key == "correlation" and v < 0:
                    status = "bad"
                if status:
                    item.setForeground(STATUS_COLORS[status])
                self.table.setItem(row, col, item)
            for b, v in enumerate(a.band_correlation):
                item = QTableWidgetItem(f"{v:+.2f}")
                if v < 0:
                    item.setForeground(STATUS_COLORS["bad"])
                self.table.setItem(len(ROWS) + b, col, item)
            warn = QTableWidgetItem("YES" if a.mono_warning else "no")
            warn.setForeground(STATUS_COLORS["bad"] if a.mono_warning else QColor("#c9d1d9"))
            self.table.setItem(len(ROWS) + 4, col, warn)

        def finite(y: np.ndarray) -> np.ndarray:
            return np.where(np.isfinite(y), y, np.nan)

        self.st_in.setData(a_in.st_times, finite(a_in.st_lufs), connect="finite")
        self.st_out.setData(a_out.st_times, finite(a_out.st_lufs), connect="finite")
        self.st_target.setPos(target)
        self.spec_in.setData(a_in.spectrum_freqs, a_in.spectrum_db)
        self.spec_out.setData(a_out.spectrum_freqs, a_out.spectrum_db)

    def cell(self, row_key: str, col: int) -> str:
        row = [k for _, k in ROWS].index(row_key)
        item = self.table.item(row, col)
        return item.text() if item else ""
