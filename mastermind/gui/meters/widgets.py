"""Small custom-painted meter widgets (bars, readouts, correlation needle)."""

from __future__ import annotations

import math

from PySide6.QtCore import QRectF, QSize, Qt
from PySide6.QtGui import QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import QLabel, QSizePolicy, QVBoxLayout, QWidget

STATUS_COLORS = {
    "ok": QColor("#3fb950"),
    "warn": QColor("#d29922"),
    "bad": QColor("#f85149"),
    "none": QColor("#8b949e"),
}
ACCENT = QColor("#58a6ff")
TRACK = QColor("#21262d")
TEXT = QColor("#c9d1d9")


def fmt(v: float, decimals: int = 1) -> str:
    if v is None or (isinstance(v, float) and math.isnan(v)):
        return "—"
    if math.isinf(v):
        return "-∞"
    return f"{v:.{decimals}f}"


class LevelBar(QWidget):
    """Vertical bar from ``lo`` to ``hi``.

    ``downward=True`` fills from the top (gain-reduction style). An optional
    ``marker`` draws a horizontal line (e.g. the loudness target), and ``peak``
    a small hold tick.
    """

    def __init__(self, lo: float, hi: float, label: str = "", downward: bool = False, width: int = 18):
        super().__init__()
        self.lo, self.hi = lo, hi
        self.label = label
        self.downward = downward
        self.value = lo if not downward else hi
        self.peak: float | None = None
        self.marker: float | None = None
        self.color = ACCENT
        self._w = width
        self.setMinimumHeight(120)
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Expanding)

    def sizeHint(self) -> QSize:
        return QSize(self._w + 4, 200)

    def set_value(self, v: float, color: QColor | None = None, peak: float | None = None) -> None:
        self.value = v if math.isfinite(v) else (self.lo if not self.downward else self.hi)
        if color is not None:
            self.color = color
        self.peak = peak if peak is not None and math.isfinite(peak) else None
        self.update()

    def _y(self, v: float, top: float, h: float) -> float:
        frac = (min(max(v, self.lo), self.hi) - self.lo) / (self.hi - self.lo)
        return top + h * (1.0 - frac)

    def paintEvent(self, _e) -> None:
        p = QPainter(self)
        label_h = 14 if self.label else 0
        r = QRectF(2, 2, self._w, self.height() - 4 - label_h)
        p.fillRect(r, TRACK)
        y = self._y(self.value, r.top(), r.height())
        fill = (
            QRectF(r.left(), r.top(), r.width(), y - r.top())
            if self.downward
            else QRectF(r.left(), y, r.width(), r.bottom() - y)
        )
        p.fillRect(fill, self.color)
        if self.peak is not None:
            py = self._y(self.peak, r.top(), r.height())
            p.setPen(QPen(TEXT, 2))
            p.drawLine(int(r.left()), int(py), int(r.right()), int(py))
        if self.marker is not None:
            my = self._y(self.marker, r.top(), r.height())
            p.setPen(QPen(QColor("#ffffff"), 1, Qt.PenStyle.DashLine))
            p.drawLine(int(r.left()) - 2, int(my), int(r.right()) + 2, int(my))
        if self.label:
            p.setPen(TEXT)
            f = p.font()
            f.setPointSize(7)
            p.setFont(f)
            p.drawText(
                QRectF(0, self.height() - label_h, self._w + 4, label_h), Qt.AlignmentFlag.AlignCenter, self.label
            )


class Readout(QWidget):
    """Caption + large number."""

    def __init__(self, caption: str, unit: str = "", size: int = 22, decimals: int = 1):
        super().__init__()
        self.unit = unit
        self.decimals = decimals
        lay = QVBoxLayout(self)
        lay.setContentsMargins(4, 2, 4, 2)
        lay.setSpacing(0)
        self.caption = QLabel(caption)
        self.caption.setStyleSheet("color:#8b949e; font-size:10px;")
        self.number = QLabel("—")
        f = QFont()
        f.setPointSize(size)
        f.setBold(True)
        self.number.setFont(f)
        lay.addWidget(self.caption)
        lay.addWidget(self.number)
        self.status = "none"

    def set_value(self, v: float, status: str = "none") -> None:
        self.number.setText(f"{fmt(v, self.decimals)} {self.unit}".strip())
        self.status = status
        self.number.setStyleSheet(f"color:{STATUS_COLORS.get(status, TEXT).name()};")


class CorrelationBar(QWidget):
    """Horizontal -1..+1 meter with a needle at the current value and a peak-hold tick."""

    def __init__(self, label: str = "", height: int = 22):
        super().__init__()
        self.label = label
        self.value = 0.0
        self.hold: float | None = None
        self.flag = False
        self.setMinimumHeight(height)
        self.setMinimumWidth(160)

    def set_value(self, v: float, hold: float | None = None, flag: bool | None = None) -> None:
        self.value = max(-1.0, min(1.0, v))
        self.hold = hold
        self.flag = (v < 0) if flag is None else flag
        self.update()

    def paintEvent(self, _e) -> None:
        p = QPainter(self)
        label_w = p.fontMetrics().horizontalAdvance(self.label) + 8 if self.label else 0
        r = QRectF(label_w, 3, self.width() - label_w - 4, self.height() - 6)
        p.fillRect(r, TRACK)
        cx = r.left() + r.width() / 2

        def x(v: float) -> float:
            return cx + v * r.width() / 2

        color = STATUS_COLORS["bad"] if self.value < 0 else STATUS_COLORS["ok"]
        left, right = sorted((cx, x(self.value)))
        p.fillRect(QRectF(left, r.top(), right - left, r.height()), color.darker(150))
        p.setPen(QPen(QColor("#ffffff"), 2))
        p.drawLine(int(x(self.value)), int(r.top()), int(x(self.value)), int(r.bottom()))
        if self.hold is not None:
            p.setPen(QPen(STATUS_COLORS["warn"], 2))
            p.drawLine(int(x(self.hold)), int(r.top()), int(x(self.hold)), int(r.bottom()))
        p.setPen(QPen(TEXT, 1))
        p.drawLine(int(cx), int(r.top()), int(cx), int(r.bottom()))
        if self.label:
            p.setPen(STATUS_COLORS["bad"] if self.flag else TEXT)
            p.drawText(
                QRectF(0, 0, label_w - 4, self.height()),
                Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignRight,
                self.label,
            )
