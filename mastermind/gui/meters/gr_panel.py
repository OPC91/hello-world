"""Gain-reduction meters for the multiband compressor, glue compressor and limiter."""

from __future__ import annotations

from PySide6.QtWidgets import QHBoxLayout, QLabel, QVBoxLayout, QWidget

from mastermind.gui.meters.widgets import STATUS_COLORS, LevelBar, fmt

GR_RANGE = (-24.0, 0.0)
METERS = [
    ("multiband.gr_b1", "Lo"),
    ("multiband.gr_b2", "LM"),
    ("multiband.gr_b3", "HM"),
    ("multiband.gr_b4", "Hi"),
    ("glue.gr", "Glue"),
    ("limiter.gr", "Lim"),
]


class GainReductionPanel(QWidget):
    def __init__(self):
        super().__init__()
        outer = QVBoxLayout(self)
        outer.addWidget(QLabel("Gain reduction (dB)"))
        lay = QHBoxLayout()
        outer.addLayout(lay)
        self.bars: dict[str, LevelBar] = {}
        self.labels: dict[str, QLabel] = {}
        for key, name in METERS:
            col = QVBoxLayout()
            bar = LevelBar(*GR_RANGE, name, downward=True, width=16)
            bar.setMinimumHeight(90)
            bar.setMaximumHeight(130)
            bar.color = STATUS_COLORS["warn"]
            lbl = QLabel("0.0")
            lbl.setStyleSheet("font-size:9px;")
            col.addWidget(bar)
            col.addWidget(lbl)
            lay.addLayout(col)
            self.bars[key] = bar
            self.labels[key] = lbl

    def update_values(self, taps: dict[str, float]) -> None:
        for key, bar in self.bars.items():
            v = taps.get(key, 0.0)
            bar.set_value(v)
            self.labels[key].setText(fmt(v))
