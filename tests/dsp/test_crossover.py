"""LR4 crossovers: bands must sum flat (allpass) and split correctly."""

import numpy as np
import pytest

from mastermind.dsp.primitives.crossover import LR4Split, MultiBandSplitter


def impulse_response_sum(splitter: MultiBandSplitter, n: int = 1 << 16) -> np.ndarray:
    x = np.zeros((splitter.splits[0].lp.zi.shape[1], n))
    x[:, 0] = 1.0
    return np.sum(splitter.process(x), axis=0)


@pytest.mark.parametrize("sr", [44100, 48000, 96000])
@pytest.mark.parametrize("freqs", [[120, 1000, 6000], [60, 250, 2500], [300, 3000, 12000]])
def test_bands_sum_flat(sr, freqs):
    s = MultiBandSplitter(freqs, sr)
    h = np.abs(np.fft.rfft(impulse_response_sum(s)[0]))
    dev = 20 * np.log10(h)
    assert np.max(np.abs(dev)) < 0.01, f"summed response deviates {np.max(np.abs(dev)):.4f} dB"


def test_sum_is_allpass_not_delay_free():
    """The sum is an allpass (flat magnitude) - its phase is not zero, by design."""
    s = MultiBandSplitter([120, 1000, 6000], 48000)
    y = impulse_response_sum(s)[0]
    assert abs(y[0] - 1.0) > 1e-3  # phase-shifted, so not a bare impulse
    assert np.isclose(np.sum(y**2), 1.0, atol=1e-6)  # energy preserved


def test_lr4_is_minus_6db_at_crossover():
    sr, fc = 48000, 1000.0
    split = LR4Split(fc, sr, channels=1)
    x = np.zeros((1, 1 << 15))
    x[0, 0] = 1
    lo, hi = split.process(x)
    f = np.fft.rfftfreq(x.shape[1], 1 / sr)
    k = np.argmin(np.abs(f - fc))
    assert 20 * np.log10(abs(np.fft.rfft(lo[0])[k])) == pytest.approx(-6.02, abs=0.1)
    assert 20 * np.log10(abs(np.fft.rfft(hi[0])[k])) == pytest.approx(-6.02, abs=0.1)
    # 24 dB/oct: two octaves above fc the low band is ~48 dB down (asymptotically).
    k4 = np.argmin(np.abs(f - 4 * fc))
    assert 20 * np.log10(abs(np.fft.rfft(lo[0])[k4])) < -40


def test_sum_stays_flat_after_frequency_change():
    s = MultiBandSplitter([120, 1000, 6000], 48000)
    s.set_frequencies([200, 2000, 9000])
    s.reset()
    h = np.abs(np.fft.rfft(impulse_response_sum(s)[0]))
    assert np.max(np.abs(20 * np.log10(h))) < 0.01


def test_rejects_unordered_frequencies():
    with pytest.raises(ValueError):
        MultiBandSplitter([1000, 500], 48000)
