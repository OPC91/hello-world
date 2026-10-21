"""True-peak limiter: the output must never exceed the ceiling."""

import numpy as np
import pytest
from scipy import signal

from mastermind.dsp.chain import MasteringChain, Slot
from mastermind.dsp.modules.limiter import TruePeakLimiter
from mastermind.dsp.primitives.truepeak import true_peak_dbtp
from mastermind.engine.offline import render
from tests.conftest import band_limit, music_like, noise, sine


def limit(x, sr, **params):
    return render(MasteringChain([Slot(TruePeakLimiter(**params))]), x, sr, block_size=1000)


def independent_tp_db(y: np.ndarray, factor: int) -> float:
    """True peak via scipy's polyphase resampler - a different filter from ours."""
    up = signal.resample_poly(y, factor, 1, axis=-1, window=("kaiser", 10.0))
    return float(20 * np.log10(np.abs(up).max()))


def make_signals(sr: int) -> dict[str, np.ndarray]:
    t = np.arange(int(1.5 * sr)) / sr
    rng = np.random.default_rng(3)
    imp = np.zeros((2, t.size))
    imp[:, :: sr // 10] = 1.0
    return {
        "music": music_like(1.5, sr),
        "noise": noise(1.5, sr, 0.3),
        "sine_997": sine(997, 1.5, sr),
        "sine_17k": sine(17000, 1.5, sr, phase=0.4),
        "square_1k": np.tile(signal.square(2 * np.pi * 1000 * t), (2, 1)) * 0.9,
        "impulses": imp,
        "burst": rng.standard_normal((2, t.size)) * np.where(t < 0.75, 0.05, 1.0),
        "hf_noise": signal.sosfilt(signal.butter(4, 15000, "hp", fs=sr, output="sos"), rng.standard_normal((2, t.size)))
        * 0.5,
    }


CASES = [(sr, ceil, drive) for sr in (44100, 48000, 96000) for ceil in (-1.0, -0.1) for drive in (0.0, 18.0)]


@pytest.mark.parametrize("sr,ceiling,drive", CASES)
def test_never_exceeds_ceiling_bs1770_meter(sr, ceiling, drive):
    """Measured with the BS.1770 4x meter, including full-band pathological signals."""
    for name, x in make_signals(sr).items():
        y = limit(x, sr, ceiling_db=ceiling, drive_db=drive)
        assert true_peak_dbtp(y) <= ceiling, name
        assert np.abs(y).max() <= 10 ** (ceiling / 20) + 1e-12, name


@pytest.mark.parametrize("sr,ceiling,drive", CASES)
def test_never_exceeds_ceiling_independent_meter(sr, ceiling, drive):
    """Programme-like (<= 20 kHz) material, checked with an independent 4x and 8x interpolator."""
    for name, x in make_signals(sr).items():
        y = limit(band_limit(x, sr), sr, ceiling_db=ceiling, drive_db=drive)
        assert independent_tp_db(y, 4) <= ceiling, name
        assert independent_tp_db(y, 8) <= ceiling, name


def test_transparent_below_ceiling(sr):
    x = sine(440, 1.0, sr, amp=0.25)
    y = limit(x, sr, ceiling_db=-1.0)
    np.testing.assert_allclose(y, x, atol=1e-12)


def test_latency_is_reported_and_compensated(sr):
    lim = TruePeakLimiter(lookahead_ms=2.0)
    lim.prepare(sr, 512)
    assert lim.latency_samples == lim._det.delay + lim.lookahead_samples - 1
    x = np.zeros((2, 4096))
    x[:, 1000] = 0.5
    y = limit(x, sr)
    assert np.argmax(np.abs(y[0])) == 1000


def test_gain_reduction_meter(sr):
    lim = TruePeakLimiter(drive_db=12.0, ceiling_db=-1.0)
    lim.prepare(sr, 1024)
    x = sine(1000, 0.5, sr, amp=0.5)
    for i in range(0, x.shape[1], 1024):
        lim.process(x[:, i : i + 1024].copy())
    # 0.5 * 4 = 2.0 (+6 dBFS) into a -1 dBTP ceiling with 0.15 dB margin: ~7.2 dB of GR.
    assert lim.meter_taps()["gr"] == pytest.approx(-(6.02 + 1.0 + 0.15), abs=0.2)


def test_gain_ramps_before_peak(sr):
    """Lookahead: gain reduction begins before the transient, so there is no step."""
    x = np.zeros((2, sr // 2))
    x[:, 10000:] = 2.0 * np.sin(2 * np.pi * 100 * np.arange(x.shape[1] - 10000) / sr)
    y = limit(x, sr, ceiling_db=-1.0)
    assert np.abs(np.diff(y[0])).max() < 0.05
