"""Command queue used to mutate the chain from other threads.

The GUI never touches processors directly. It posts commands which the engine
thread executes at the start of the next block, so the DSP objects only ever
see a single thread.

JUCE mapping: parameter changes go through ``AudioProcessorValueTreeState``
atomics; structural edits (reorder, load preset) through a lock-free FIFO of
messages consumed at the top of ``processBlock`` or a ``juce::SpinLock`` swap.
"""

from __future__ import annotations

import queue
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from mastermind.dsp.chain import MasteringChain


@dataclass(frozen=True)
class SetParam:
    type_id: str
    param_id: str
    value: Any

    def apply(self, chain: MasteringChain) -> None:
        chain.set_param(self.type_id, self.param_id, self.value)


@dataclass(frozen=True)
class SetBypass:
    type_id: str
    bypass: bool

    def apply(self, chain: MasteringChain) -> None:
        chain.set_bypass(self.type_id, self.bypass)


@dataclass(frozen=True)
class Reorder:
    type_ids: tuple[str, ...]

    def apply(self, chain: MasteringChain) -> None:
        chain.reorder(list(self.type_ids))


@dataclass(frozen=True)
class Call:
    """Escape hatch: run an arbitrary function on the engine thread."""

    fn: Callable[[MasteringChain], None]

    def apply(self, chain: MasteringChain) -> None:
        self.fn(chain)


Command = SetParam | SetBypass | Reorder | Call


class CommandQueue:
    def __init__(self) -> None:
        self._q: queue.SimpleQueue[Command] = queue.SimpleQueue()

    def post(self, cmd: Command) -> None:
        self._q.put(cmd)

    def drain_into(self, chain: MasteringChain) -> int:
        n = 0
        while True:
            try:
                cmd = self._q.get_nowait()
            except queue.Empty:
                return n
            cmd.apply(chain)
            n += 1
