"""Integer-sample delay line (JUCE: ``juce::dsp::DelayLine`` with no interpolation)."""

from __future__ import annotations

import numpy as np


class DelayLine:
    """Delays a ``(channels, n)`` signal by a fixed number of samples.

    Works for any block length relative to the delay, so it is block-size
    invariant. Changing the delay clears the history. The returned array never
    aliases the input (even for a zero delay), so callers may safely let a
    processor modify the input in place afterwards.
    """

    def __init__(self, delay: int = 0, channels: int = 2):
        self.channels = channels
        self.set_delay(delay)

    @property
    def delay(self) -> int:
        return self._delay

    def set_delay(self, delay: int) -> None:
        self._delay = int(delay)
        self._buf = np.zeros((self.channels, self._delay))

    def reset(self) -> None:
        self._buf[:] = 0.0

    def process(self, x: np.ndarray) -> np.ndarray:
        if self._delay == 0:
            return x.copy()
        n = x.shape[-1]
        joined = np.concatenate([self._buf, x], axis=-1)
        self._buf = joined[:, n:].copy()
        return joined[:, :n]
