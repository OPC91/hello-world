"""Auto-generated parameter editor built from a processor's ParamSpecs."""

from __future__ import annotations

import math
from collections.abc import Callable
from typing import Any

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QScrollArea,
    QSlider,
    QVBoxLayout,
    QWidget,
)

from mastermind.dsp.base import ParamSpec, Processor

SLIDER_STEPS = 1000

OnChange = Callable[[str, Any], None]


class FloatControl(QWidget):
    """Slider + spin box for a float/int parameter (log-mapped if ``spec.log``)."""

    def __init__(self, spec: ParamSpec, value: float, on_change: OnChange):
        super().__init__()
        self.spec = spec
        self._on_change = on_change
        self.slider = QSlider(Qt.Orientation.Horizontal)
        self.slider.setRange(0, SLIDER_STEPS)
        self.spin = QDoubleSpinBox()
        self.spin.setRange(spec.min, spec.max)
        self.spin.setDecimals(0 if spec.kind == "int" else (1 if spec.max - spec.min > 50 else 2))
        self.spin.setSuffix(f" {spec.unit}" if spec.unit else "")
        self.spin.setKeyboardTracking(False)
        self.spin.setMinimumWidth(100)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(self.slider, 1)
        lay.addWidget(self.spin)
        self.set_value(value)
        self.slider.valueChanged.connect(self._from_slider)
        self.spin.valueChanged.connect(self._from_spin)

    def _to_pos(self, v: float) -> int:
        s = self.spec
        if s.log and s.min > 0:
            frac = math.log(v / s.min) / math.log(s.max / s.min)
        else:
            frac = (v - s.min) / (s.max - s.min)
        return int(round(frac * SLIDER_STEPS))

    def _from_pos(self, pos: int) -> float:
        s = self.spec
        frac = pos / SLIDER_STEPS
        if s.log and s.min > 0:
            return s.min * (s.max / s.min) ** frac
        return s.min + frac * (s.max - s.min)

    def set_value(self, v: float) -> None:
        self.slider.blockSignals(True)
        self.spin.blockSignals(True)
        self.spin.setValue(v)
        self.slider.setValue(self._to_pos(v))
        self.slider.blockSignals(False)
        self.spin.blockSignals(False)

    def _from_slider(self, pos: int) -> None:
        v = self._from_pos(pos)
        self.spin.blockSignals(True)
        self.spin.setValue(v)
        self.spin.blockSignals(False)
        self._on_change(self.spec.id, v)

    def _from_spin(self, v: float) -> None:
        self.slider.blockSignals(True)
        self.slider.setValue(self._to_pos(v))
        self.slider.blockSignals(False)
        self._on_change(self.spec.id, v)


def make_control(spec: ParamSpec, value: Any, on_change: OnChange) -> QWidget:
    if spec.kind == "bool":
        w = QCheckBox()
        w.setChecked(bool(value))
        w.toggled.connect(lambda v, pid=spec.id: on_change(pid, bool(v)))
        return w
    if spec.kind == "choice":
        w = QComboBox()
        w.addItems(list(spec.choices))
        w.setCurrentText(str(value))
        w.currentTextChanged.connect(lambda v, pid=spec.id: on_change(pid, v))
        return w
    return FloatControl(spec, float(value), on_change)


class ParamPanel(QScrollArea):
    """Shows the controls for one processor, grouped by ``ParamSpec.group``."""

    def __init__(self, on_change: Callable[[str, str, Any], None]):
        super().__init__()
        self.setWidgetResizable(True)
        self._on_change = on_change
        self.controls: dict[str, QWidget] = {}
        self.type_id: str | None = None
        self.show_processor(None)

    def show_processor(self, proc: Processor | None) -> None:
        self.controls = {}
        root = QWidget()
        lay = QVBoxLayout(root)
        if proc is None:
            lay.addWidget(QLabel("Select a module in the chain."))
            self.type_id = None
        else:
            self.type_id = proc.type_id
            title = QLabel(f"<b>{proc.display_name}</b>")
            lay.addWidget(title)
            groups: dict[str, QFormLayout] = {}
            for spec in proc.param_specs():
                form = groups.get(spec.group)
                if form is None:
                    box = QGroupBox(spec.group or "General")
                    form = groups[spec.group] = QFormLayout(box)
                    lay.addWidget(box)
                ctrl = make_control(spec, proc.get_param(spec.id), self._changed)
                ctrl.setObjectName(f"param:{spec.id}")
                self.controls[spec.id] = ctrl
                form.addRow(spec.name, ctrl)
        lay.addStretch(1)
        self.setWidget(root)

    def _changed(self, pid: str, value: Any) -> None:
        if self.type_id is not None:
            self._on_change(self.type_id, pid, value)
