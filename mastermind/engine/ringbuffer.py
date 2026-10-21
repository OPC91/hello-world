"""Single-producer / single-consumer ring buffers.

:class:`AudioRingBuffer` carries rendered audio from the engine thread to the
PortAudio callback. :class:`SnapshotQueue` carries meter snapshots from the
engine thread to the GUI, which polls at ~30 fps.

Only the producer advances ``_write`` and only the consumer advances ``_read``;
each index is a single Python int assignment (atomic under the GIL), so no lock
is needed. A flush is *requested* by anyone but performed by the consumer.

JUCE mapping: ``juce::AbstractFifo`` (+ an ``AudioBuffer``), and a lock-free
FIFO of meter structs read by a ``juce::Timer`` on the message thread.
"""

from __future__ import annotations

import collections
import threading
from typing import Any

import numpy as np


class AudioRingBuffer:
    def __init__(self, capacity: int, channels: int = 2):
        self.capacity = int(capacity)
        self.channels = channels
        self._buf = np.zeros((channels, self.capacity), dtype=np.float32)
        self._write = 0  # total frames written (monotonic)
        self._read = 0  # total frames read (monotonic)
        self._flush = threading.Event()

    def available_read(self) -> int:
        return self._write - self._read

    def available_write(self) -> int:
        return self.capacity - self.available_read()

    def write(self, x: np.ndarray) -> int:
        """Producer side. Writes as many frames as fit and returns that count."""
        n = min(x.shape[-1], self.available_write())
        if n <= 0:
            return 0
        start = self._write % self.capacity
        first = min(n, self.capacity - start)
        self._buf[:, start : start + first] = x[:, :first]
        if n > first:
            self._buf[:, : n - first] = x[:, first:n]
        self._write += n
        return n

    def read_into(self, out: np.ndarray) -> int:
        """Consumer side. Fills ``out`` (channels, n) and zero-pads on underrun.

        Returns the number of real frames delivered.
        """
        if self._flush.is_set():
            self._read = self._write
            self._flush.clear()
        n = min(out.shape[-1], self.available_read())
        start = self._read % self.capacity
        first = min(n, self.capacity - start)
        out[:, :first] = self._buf[:, start : start + first]
        if n > first:
            out[:, first:n] = self._buf[:, : n - first]
        out[:, n:] = 0.0
        self._read += n
        return n

    def request_flush(self) -> None:
        self._flush.set()


class SnapshotQueue:
    """Bounded queue of meter snapshots; oldest entries are dropped when full."""

    def __init__(self, maxlen: int = 256):
        self._q: collections.deque[dict[str, Any]] = collections.deque(maxlen=maxlen)

    def push(self, snapshot: dict[str, Any]) -> None:
        self._q.append(snapshot)  # deque.append is thread-safe

    def drain(self) -> list[dict[str, Any]]:
        out = []
        while True:
            try:
                out.append(self._q.popleft())
            except IndexError:
                return out
