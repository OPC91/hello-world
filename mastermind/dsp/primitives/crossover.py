"""Linkwitz-Riley 4th-order (LR4, 24 dB/oct) crossovers that sum to an allpass.

An LR4 low-pass is two cascaded 2nd-order Butterworth low-passes (Q = 1/sqrt 2);
likewise for the high-pass. ``LP4 + HP4`` equals the 2nd-order allpass with the
same corner frequency and Q = 1/sqrt 2, so the two outputs sum to a flat
magnitude response.

For N bands we split in a tree (lowest crossover first). Each band that does
*not* pass through a later crossover receives that crossover's allpass, so all
bands share the same total phase and their sum is a single cascade of allpasses:
flat magnitude, smooth phase.

    x --LR(f1)--> low ......................... AP(f2) -> AP(f3) -> band 0
               `-> high --LR(f2)--> low ......... AP(f3) -> band 1
                                `-> high --LR(f3)--> low   -> band 2
                                                  `-> high  -> band 3

JUCE mapping: ``juce::dsp::LinkwitzRileyFilter<float>`` (type lowpass/highpass/
allpass) - its ``allpass`` output is exactly the compensation used here.
"""

from __future__ import annotations

import numpy as np

from mastermind.dsp.primitives.biquad import SOSFilter, design

BUTTERWORTH_Q = 1.0 / np.sqrt(2.0)


def lr4_sections(kind: str, freq: float, sample_rate: float) -> np.ndarray:
    """Two identical Butterworth biquads (``kind`` = 'lowpass' | 'highpass' | 'allpass').

    For 'allpass' a single 2nd-order allpass section plus an identity section is
    returned so all three kinds have the same shape.
    """
    if kind == "allpass":
        return np.vstack([design("allpass", freq, sample_rate, BUTTERWORTH_Q), [1, 0, 0, 1, 0, 0]])
    rbj = {"lowpass": "high_cut", "highpass": "low_cut"}[kind]
    s = design(rbj, freq, sample_rate, BUTTERWORTH_Q)
    return np.vstack([s, s])


class LR4Split:
    """One LR4 crossover point producing (low, high) from a stereo block."""

    def __init__(self, freq: float, sample_rate: float, channels: int = 2):
        self.lp = SOSFilter(2, channels)
        self.hp = SOSFilter(2, channels)
        self.set_frequency(freq, sample_rate)

    def set_frequency(self, freq: float, sample_rate: float) -> None:
        self.freq = freq
        self.lp.set_sos(lr4_sections("lowpass", freq, sample_rate))
        self.hp.set_sos(lr4_sections("highpass", freq, sample_rate))

    def reset(self) -> None:
        self.lp.reset()
        self.hp.reset()

    def process(self, x: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        return self.lp.process(x), self.hp.process(x)


class LR4Allpass:
    """Phase-compensation allpass matching an :class:`LR4Split`."""

    def __init__(self, freq: float, sample_rate: float, channels: int = 2):
        self.ap = SOSFilter(2, channels)
        self.set_frequency(freq, sample_rate)

    def set_frequency(self, freq: float, sample_rate: float) -> None:
        self.ap.set_sos(lr4_sections("allpass", freq, sample_rate))

    def reset(self) -> None:
        self.ap.reset()

    def process(self, x: np.ndarray) -> np.ndarray:
        return self.ap.process(x)


class MultiBandSplitter:
    """Split a stereo block into ``len(freqs) + 1`` phase-aligned bands.

    The bands always sum to an allpass response of the input (flat magnitude).
    Crossover frequencies must be strictly increasing; they can be changed on
    the fly (filter memories are kept).
    """

    def __init__(self, freqs: list[float], sample_rate: float, channels: int = 2):
        self.sample_rate = sample_rate
        self.freqs = list(freqs)
        self._check(self.freqs)
        n = len(self.freqs)
        self.splits = [LR4Split(f, sample_rate, channels) for f in self.freqs]
        # comp[b] holds the allpasses for crossovers b+1..n-1 applied to band b.
        self.comp = [[LR4Allpass(self.freqs[k], sample_rate, channels) for k in range(b + 1, n)] for b in range(n)]

    @staticmethod
    def _check(freqs: list[float]) -> None:
        if any(b <= a for a, b in zip(freqs, freqs[1:], strict=False)):
            raise ValueError(f"crossover frequencies must be strictly increasing: {freqs}")

    @property
    def n_bands(self) -> int:
        return len(self.freqs) + 1

    def set_frequencies(self, freqs: list[float]) -> None:
        freqs = list(freqs)
        self._check(freqs)
        if len(freqs) != len(self.freqs):
            raise ValueError("number of crossovers is fixed")
        self.freqs = freqs
        for i, f in enumerate(freqs):
            self.splits[i].set_frequency(f, self.sample_rate)
        for b, aps in enumerate(self.comp):
            for j, ap in enumerate(aps):
                ap.set_frequency(freqs[b + 1 + j], self.sample_rate)

    def reset(self) -> None:
        for s in self.splits:
            s.reset()
        for aps in self.comp:
            for ap in aps:
                ap.reset()

    def process(self, x: np.ndarray) -> list[np.ndarray]:
        bands: list[np.ndarray] = []
        rest = x
        for b, split in enumerate(self.splits):
            low, rest = split.process(rest)
            for ap in self.comp[b]:
                low = ap.process(low)
            bands.append(low)
        bands.append(rest)
        return bands
