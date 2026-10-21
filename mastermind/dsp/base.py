"""Core processor and parameter abstractions.

JUCE mapping:
    Processor  -> juce::dsp::ProcessorBase (prepare / process / reset)
    ParamSpec  -> juce::RangedAudioParameter (+ NormalisableRange)
    ParamSet   -> juce::AudioProcessorValueTreeState, with SmoothedValue for
                  parameters flagged ``smooth=True``

Threading contract: a Processor is owned by exactly one thread (the engine
thread in real-time playback, or the caller in offline rendering). Parameter
changes from other threads must be marshalled through
:class:`mastermind.engine.commands.CommandQueue`, never written directly.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any, ClassVar, Literal

import numpy as np

ParamKind = Literal["float", "int", "bool", "choice"]

#: Default ramp time for smoothed parameters.
DEFAULT_RAMP_MS = 20.0


@dataclass(frozen=True)
class ParamSpec:
    """Static description of one parameter.

    Attributes:
        id: Stable identifier used in presets and commands (snake_case).
        name: Human-readable label.
        default: Default value (for ``choice`` this is one of ``choices``).
        min / max: Range for numeric parameters.
        unit: Display unit, e.g. ``"dB"``, ``"Hz"``, ``"ms"``, ``"%"``.
        kind: ``float`` | ``int`` | ``bool`` | ``choice``.
        choices: Allowed values for ``choice`` parameters.
        log: Hint for UIs that the control should be log-scaled (frequencies, times).
        smooth: Ramp per sample when changed (gains, mixes) instead of jumping.
        group: Optional grouping label for UIs (e.g. ``"Band 3"``).
        structural: Changing it alters latency or internal buffer sizes.
    """

    id: str
    name: str
    default: Any
    min: float = 0.0
    max: float = 1.0
    unit: str = ""
    kind: ParamKind = "float"
    choices: tuple[str, ...] = ()
    log: bool = False
    smooth: bool = False
    group: str = ""
    structural: bool = False

    def coerce(self, value: Any) -> Any:
        """Validate and clamp ``value`` into the legal domain of this parameter."""
        if self.kind == "bool":
            return bool(value)
        if self.kind == "choice":
            if value not in self.choices:
                raise ValueError(f"{self.id}: {value!r} not in {self.choices}")
            return value
        v = float(value)
        if not np.isfinite(v):
            raise ValueError(f"{self.id}: non-finite value {value!r}")
        v = min(max(v, self.min), self.max)
        return int(round(v)) if self.kind == "int" else v


class ParamSet:
    """Holds the live values of a processor's parameters.

    Non-smoothed parameters take effect immediately and bump :attr:`version`
    so processors can lazily recompute coefficients. Smoothed parameters ramp
    linearly over ``ramp_ms``; processors read them per sample with
    :meth:`ramp`, which keeps the result independent of block size.
    """

    def __init__(self, specs: list[ParamSpec], ramp_ms: float = DEFAULT_RAMP_MS):
        self.specs: dict[str, ParamSpec] = {s.id: s for s in specs}
        if len(self.specs) != len(specs):
            raise ValueError("duplicate parameter ids")
        self._target: dict[str, Any] = {s.id: s.default for s in specs}
        self._current: dict[str, Any] = dict(self._target)
        self._step: dict[str, float] = {}
        self._ramp_ms = ramp_ms
        self._ramp_samples = 1
        self._changed: set[str] = set(self.specs)
        self.version = 0

    def prepare(self, sample_rate: float) -> None:
        self._ramp_samples = max(1, int(round(self._ramp_ms * 1e-3 * sample_rate)))
        self.snap()

    def snap(self) -> None:
        """Jump every smoothed parameter to its target (used on prepare/reset)."""
        self._current.update(self._target)
        self._step.clear()

    def set(self, pid: str, value: Any) -> None:
        spec = self.specs[pid]
        value = spec.coerce(value)
        if value == self._target[pid]:
            return
        self._target[pid] = value
        if spec.smooth and spec.kind == "float":
            self._step[pid] = (value - self._current[pid]) / self._ramp_samples
        else:
            self._current[pid] = value
        self._changed.add(pid)
        self.version += 1

    def get(self, pid: str) -> Any:
        """Current (possibly mid-ramp) value."""
        return self._current[pid]

    def target(self, pid: str) -> Any:
        return self._target[pid]

    def ramp(self, pid: str, n: int) -> np.ndarray | float:
        """Per-sample values for the next ``n`` samples, advancing the ramp.

        Returns a plain float when the parameter is not ramping, which lets
        callers broadcast cheaply.
        """
        step = self._step.get(pid)
        cur = self._current[pid]
        if step is None:
            return cur
        tgt = self._target[pid]
        vals = cur + step * np.arange(1, n + 1)
        vals = np.minimum(vals, tgt) if step > 0 else np.maximum(vals, tgt)
        self._current[pid] = float(vals[-1])
        if vals[-1] == tgt:
            del self._step[pid]
        return vals

    def consume_changes(self) -> set[str]:
        """Return ids changed since the last call, then clear the set."""
        changed, self._changed = self._changed, set()
        return changed

    def values(self) -> dict[str, Any]:
        return dict(self._target)

    def update(self, values: dict[str, Any]) -> None:
        for k, v in values.items():
            if k in self.specs:
                self.set(k, v)


class Processor(ABC):
    """Base class for every block-based stereo processor.

    Subclasses declare ``type_id`` / ``display_name`` and implement
    :meth:`param_specs`, :meth:`_prepare` and :meth:`process`.
    """

    type_id: ClassVar[str] = ""
    display_name: ClassVar[str] = ""
    #: Processors that must stay last in the chain (e.g. dither).
    pinned_last: ClassVar[bool] = False

    def __init__(self, **params: Any):
        self.params = ParamSet(self.param_specs())
        self.params.update(params)
        self.sample_rate: float = 0.0
        self.max_block: int = 0
        self.channels: int = 2
        self._meters: dict[str, float] = {}

    # ---- declaration -------------------------------------------------
    @classmethod
    @abstractmethod
    def param_specs(cls) -> list[ParamSpec]: ...

    # ---- lifecycle ---------------------------------------------------
    def prepare(self, sample_rate: float, max_block: int, channels: int = 2) -> None:
        if channels != 2:
            raise ValueError("MasterMind processors are stereo-only")
        self.sample_rate = float(sample_rate)
        self.max_block = int(max_block)
        self.channels = channels
        self.params.prepare(sample_rate)
        self._prepare()
        self.params.consume_changes()

    @abstractmethod
    def _prepare(self) -> None:
        """Allocate state and compute coefficients for the current sample rate."""

    @abstractmethod
    def process(self, block: np.ndarray) -> np.ndarray:
        """Process a ``(2, n)`` float64 block and return the output block.

        Implementations may modify ``block`` in place and return it.
        """

    def reset(self) -> None:
        """Clear internal state (filter memories, envelopes) without reallocating."""
        self._prepare()

    @property
    def latency_samples(self) -> int:
        return 0

    @property
    def is_prepared(self) -> bool:
        return self.sample_rate > 0

    # ---- parameters --------------------------------------------------
    def set_param(self, pid: str, value: Any) -> None:
        self.params.set(pid, value)

    def get_param(self, pid: str) -> Any:
        return self.params.target(pid)

    def meter_taps(self) -> dict[str, float]:
        """Latest meter readings (e.g. gain reduction in dB), updated per block."""
        return dict(self._meters)

    # ---- serialisation ----------------------------------------------
    def to_dict(self) -> dict[str, Any]:
        return {"type": self.type_id, "params": self.params.values()}


@dataclass
class ProcessorRegistry:
    """Maps ``type_id`` strings to Processor classes for (de)serialisation."""

    classes: dict[str, type[Processor]] = field(default_factory=dict)

    def register(self, cls: type[Processor]) -> type[Processor]:
        if not cls.type_id:
            raise ValueError(f"{cls.__name__} has no type_id")
        self.classes[cls.type_id] = cls
        return cls

    def create(self, data: dict[str, Any]) -> Processor:
        try:
            cls = self.classes[data["type"]]
        except KeyError:
            raise ValueError(f"unknown processor type {data.get('type')!r}") from None
        return cls(**data.get("params", {}))


registry = ProcessorRegistry()


def db_to_gain(db: np.ndarray | float) -> np.ndarray | float:
    return np.power(10.0, np.asarray(db) / 20.0) if isinstance(db, np.ndarray) else 10.0 ** (db / 20.0)


def gain_to_db(g: np.ndarray | float, floor_db: float = -200.0) -> np.ndarray | float:
    out = 20.0 * np.log10(np.maximum(np.abs(g), 10.0 ** (floor_db / 20.0)))
    return out if isinstance(g, np.ndarray) else float(out)
