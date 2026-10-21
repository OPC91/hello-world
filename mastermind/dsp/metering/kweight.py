"""K-weighting filter (ITU-R BS.1770-4, Annex 1).

Two biquads: a high-shelf "pre-filter" (+4 dB above ~1.7 kHz, modelling the
head) followed by the RLB high-pass (~38 Hz). BS.1770 only publishes 48 kHz
coefficients; for other rates we re-derive them from the analogue prototype
parameters (as libebur128 / pyloudnorm do), which reproduces the published
48 kHz values to ~1e-8.

JUCE mapping: two ``juce::dsp::IIR::Filter`` with custom coefficients.
"""

from __future__ import annotations

import math

import numpy as np

from mastermind.dsp.primitives.biquad import SOSFilter

# Analogue prototype parameters fitted to the BS.1770 48 kHz coefficients.
SHELF_GAIN_DB = 3.999843853973347
SHELF_Q = 0.7071752369554196
SHELF_FC = 1681.974450955533
HP_Q = 0.5003270373238773
HP_FC = 38.13547087602444

#: Published BS.1770-4 coefficients at 48 kHz, used by the tests.
BS1770_48K = {
    "shelf_b": (1.53512485958697, -2.69169618940638, 1.19839281085285),
    "shelf_a": (1.0, -1.69065929318241, 0.73248077421585),
    "hp_b": (1.0, -2.0, 1.0),
    "hp_a": (1.0, -1.99004745483398, 0.99007225036621),
}


def kweighting_sos(sample_rate: float) -> np.ndarray:
    """K-weighting as two second-order sections for ``sample_rate``."""
    k = math.tan(math.pi * SHELF_FC / sample_rate)
    vh = 10.0 ** (SHELF_GAIN_DB / 20.0)
    vb = vh**0.4996667741545416
    a0 = 1.0 + k / SHELF_Q + k * k
    shelf = [
        (vh + vb * k / SHELF_Q + k * k) / a0,
        2.0 * (k * k - vh) / a0,
        (vh - vb * k / SHELF_Q + k * k) / a0,
        1.0,
        2.0 * (k * k - 1.0) / a0,
        (1.0 - k / SHELF_Q + k * k) / a0,
    ]
    k = math.tan(math.pi * HP_FC / sample_rate)
    a0 = 1.0 + k / HP_Q + k * k
    hp = [1.0, -2.0, 1.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / HP_Q + k * k) / a0]
    return np.array([shelf, hp])


class KWeighting:
    """Stateful K-weighting of a ``(channels, n)`` block."""

    def __init__(self, sample_rate: float, channels: int = 2):
        self._f = SOSFilter(2, channels)
        self._f.set_sos(kweighting_sos(sample_rate))

    def reset(self) -> None:
        self._f.reset()

    def process(self, x: np.ndarray) -> np.ndarray:
        return self._f.process(x)
