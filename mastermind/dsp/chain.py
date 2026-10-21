"""The reorderable mastering chain.

Each :class:`Slot` wraps a processor plus a bypass switch. Design decisions:

* **Processors always run**, even when bypassed, so their state (filter memories,
  FIR history, envelopes) is warm and un-bypassing is instant and click-free.
* **Bypass is latency-compensated**: the dry path of each slot goes through a
  delay line equal to that processor's latency, so toggling bypass never shifts
  timing and the chain's total latency is constant.
* **Bypass crossfades** linearly over ``FADE_MS``. Once the fade completes the
  output is *exactly* the delayed input (bit-exact bypass).
* Processors flagged ``pinned_last`` (dither) always stay at the end.

JUCE mapping: a dynamic ``juce::AudioProcessorGraph`` or a custom vector of
``std::unique_ptr<ProcessorBase>`` with per-slot ``juce::dsp::DryWetMixer``
(which already provides latency-compensated dry paths).
"""

from __future__ import annotations

from typing import Any

import numpy as np

from mastermind.dsp import modules  # noqa: F401  (registers processor types)
from mastermind.dsp.base import Processor, registry
from mastermind.dsp.primitives.delay import DelayLine

CHAIN_FORMAT_VERSION = 1

#: Default order and initial bypass state of a new chain.
DEFAULT_LAYOUT: list[tuple[str, bool]] = [
    ("input_gain", False),
    ("eq", False),
    ("multiband", False),
    ("saturation", True),
    ("imager", False),
    ("glue", False),
    ("limiter", False),
    ("dither", False),
]


class Slot:
    FADE_MS = 20.0

    def __init__(self, processor: Processor, bypass: bool = False):
        self.processor = processor
        self.bypass = bypass
        self._wet = 0.0 if bypass else 1.0
        self._step = 0.0
        self._dry = DelayLine(0)

    def prepare(self, sample_rate: float, max_block: int) -> None:
        self.processor.prepare(sample_rate, max_block)
        self._dry.set_delay(self.processor.latency_samples)
        self._step = 1.0 / max(1, int(round(self.FADE_MS * 1e-3 * sample_rate)))
        self._wet = 0.0 if self.bypass else 1.0

    def set_bypass(self, bypass: bool) -> None:
        self.bypass = bool(bypass)

    def process(self, x: np.ndarray) -> np.ndarray:
        p = self.processor
        dry = self._dry.process(x)  # never aliases x (processors may work in place)
        wet = p.process(x)
        if self._dry.delay != p.latency_samples:
            # Latency changed (e.g. EQ phase mode switched): resync the dry path.
            self._dry.set_delay(p.latency_samples)
        target = 0.0 if self.bypass else 1.0
        if self._wet == target:
            return wet if target else dry
        n = x.shape[-1]
        direction = self._step if target > self._wet else -self._step
        ramp = np.clip(self._wet + direction * np.arange(1, n + 1), 0.0, 1.0)
        self._wet = float(ramp[-1])
        return dry + (wet - dry) * ramp

    def to_dict(self) -> dict[str, Any]:
        return {**self.processor.to_dict(), "bypass": self.bypass}


class MasteringChain:
    """Ordered list of slots processed in series on ``(2, n)`` blocks."""

    def __init__(self, slots: list[Slot] | None = None):
        self.slots: list[Slot] = slots or []
        self.sample_rate = 0.0
        self.max_block = 0
        self._enforce_pinned()

    # ---- construction -----------------------------------------------
    @classmethod
    def default(cls) -> MasteringChain:
        return cls([Slot(registry.create({"type": t}), bypass=b) for t, b in DEFAULT_LAYOUT])

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> MasteringChain:
        version = data.get("version", CHAIN_FORMAT_VERSION)
        if version > CHAIN_FORMAT_VERSION:
            raise ValueError(f"chain format v{version} is newer than supported v{CHAIN_FORMAT_VERSION}")
        return cls([Slot(registry.create(s), bypass=s.get("bypass", False)) for s in data["slots"]])

    def to_dict(self) -> dict[str, Any]:
        return {"version": CHAIN_FORMAT_VERSION, "slots": [s.to_dict() for s in self.slots]}

    def copy(self) -> MasteringChain:
        """Independent chain with the same settings (used for offline renders)."""
        return MasteringChain.from_dict(self.to_dict())

    # ---- lifecycle ----------------------------------------------------
    def prepare(self, sample_rate: float, max_block: int) -> None:
        self.sample_rate = float(sample_rate)
        self.max_block = int(max_block)
        for s in self.slots:
            s.prepare(sample_rate, max_block)

    def reset(self) -> None:
        for s in self.slots:
            s.prepare(self.sample_rate, self.max_block)

    def process(self, block: np.ndarray) -> np.ndarray:
        x = np.array(block, dtype=np.float64, copy=True)
        for s in self.slots:
            x = s.process(x)
        return x

    @property
    def latency_samples(self) -> int:
        return sum(s.processor.latency_samples for s in self.slots)

    # ---- editing ------------------------------------------------------
    def _enforce_pinned(self) -> None:
        self.slots.sort(key=lambda s: s.processor.pinned_last)  # stable sort

    def move(self, src: int, dst: int) -> None:
        """Move slot ``src`` to index ``dst``; pinned slots stay at the end."""
        slot = self.slots.pop(src)
        self.slots.insert(dst, slot)
        self._enforce_pinned()

    def reorder(self, type_ids: list[str]) -> None:
        by_id = {s.processor.type_id: s for s in self.slots}
        if sorted(by_id) != sorted(type_ids):
            raise ValueError("reorder must contain each slot exactly once")
        self.slots = [by_id[t] for t in type_ids]
        self._enforce_pinned()

    def index_of(self, type_id: str) -> int:
        for i, s in enumerate(self.slots):
            if s.processor.type_id == type_id:
                return i
        raise KeyError(type_id)

    def find(self, type_id: str) -> Processor:
        return self.slots[self.index_of(type_id)].processor

    def set_bypass(self, type_id: str, bypass: bool) -> None:
        self.slots[self.index_of(type_id)].set_bypass(bypass)

    def set_param(self, type_id: str, pid: str, value: Any) -> None:
        self.find(type_id).set_param(pid, value)

    def meter_taps(self) -> dict[str, float]:
        out: dict[str, float] = {}
        for s in self.slots:
            for k, v in s.processor.meter_taps().items():
                out[f"{s.processor.type_id}.{k}"] = v
        return out
