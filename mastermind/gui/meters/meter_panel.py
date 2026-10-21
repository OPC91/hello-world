"""Right-hand meter dock: source toggle, loudness, correlation and gain reduction."""

from __future__ import annotations

from PySide6.QtWidgets import QButtonGroup, QHBoxLayout, QLabel, QRadioButton, QVBoxLayout, QWidget

from mastermind.gui.meters.gr_panel import GainReductionPanel
from mastermind.gui.meters.loudness_panel import LoudnessPanel
from mastermind.gui.meters.stereo_panel import CorrelationSection

SOURCES = ("in", "out")


class MeterPanel(QWidget):
    def __init__(self):
        super().__init__()
        lay = QVBoxLayout(self)
        row = QHBoxLayout()
        row.addWidget(QLabel("Meter source:"))
        self.btn_in = QRadioButton("Input (pre-chain)")
        self.btn_out = QRadioButton("Output (post-chain)")
        self.btn_out.setChecked(True)
        self.group = QButtonGroup(self)
        self.group.addButton(self.btn_in)
        self.group.addButton(self.btn_out)
        row.addWidget(self.btn_in)
        row.addWidget(self.btn_out)
        row.addStretch(1)
        lay.addLayout(row)
        self.loudness = LoudnessPanel()
        self.correlation = CorrelationSection()
        self.gr = GainReductionPanel()
        lay.addWidget(self.loudness, 1)
        lay.addWidget(self.correlation)
        lay.addWidget(self.gr)

    @property
    def source(self) -> str:
        return "in" if self.btn_in.isChecked() else "out"

    def set_source(self, source: str) -> None:
        (self.btn_in if source == "in" else self.btn_out).setChecked(True)
