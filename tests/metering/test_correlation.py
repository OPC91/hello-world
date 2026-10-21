import numpy as np
import pytest

from mastermind.dsp.metering.correlation import CorrelationMeter, correlation, goniometer_points
from tests.conftest import noise

SR = 48000


def run(x, block=1024, **kw):
    m = CorrelationMeter(SR, **kw)
    vals = []
    for i in range(0, x.shape[-1], block):
        m.process(x[:, i : i + block])
        vals.append(m.value)
    return m, np.array(vals)


def test_identical_channels_read_plus_one():
    n = noise(2.0, SR)[0]
    m, v = run(np.vstack([n, n]))
    assert v[-1] == pytest.approx(1.0, abs=1e-9)
    assert all(b == pytest.approx(1.0, abs=1e-6) for b in m.band_values)


def test_inverted_channels_read_minus_one():
    n = noise(2.0, SR)[0]
    m, v = run(np.vstack([n, -n]))
    assert v[-1] == pytest.approx(-1.0, abs=1e-9)
    assert m.band_flags == [True] * 4
    assert m.mono_warning


def test_decorrelated_noise_reads_about_zero():
    rng = np.random.default_rng(7)
    m, v = run(rng.standard_normal((2, 10 * SR)))
    tail = v[len(v) // 4 :]
    assert abs(tail.mean()) < 0.02
    assert np.abs(tail).max() < 0.2  # 300 ms smoothing keeps the needle near 0
    assert not m.mono_warning


def test_silence_reads_zero():
    _, v = run(np.zeros((2, SR)))
    assert v[-1] == 0.0


def test_smoothing_time_constant():
    """Step from +1 to -1 content: after ~tau the meter has moved most of the way."""
    n = noise(4.0, SR)[0]
    x = np.vstack([n, np.concatenate([n[: 2 * SR], -n[2 * SR :]])])
    m = CorrelationMeter(SR, tau_s=0.3)
    m.process(x[:, : 2 * SR])
    m.process(x[:, 2 * SR : 2 * SR + int(0.05 * SR)])
    assert m.value > 0.5  # 50 ms in: still mostly positive
    m.process(x[:, 2 * SR + int(0.05 * SR) : 2 * SR + int(1.0 * SR)])
    assert m.value < -0.9


def test_peak_hold_keeps_minimum():
    n = noise(6.0, SR)[0]
    neg = int(0.5 * SR)
    r = np.concatenate([n[:SR], -n[SR : SR + neg], n[SR + neg :]])
    m = CorrelationMeter(SR, hold_s=10.0)
    for i in range(0, n.size, 1024):
        m.process(np.vstack([n, r])[:, i : i + 1024])
    assert m.value > 0.9
    assert m.hold < -0.5


def test_mono_warning_needs_more_than_one_second():
    n = noise(3.0, SR)[0]
    m = CorrelationMeter(SR)
    m.process(np.vstack([n[: int(0.8 * SR)], -n[: int(0.8 * SR)]]))
    assert not m.mono_warning
    m.process(np.vstack([n[: int(0.6 * SR)], -n[: int(0.6 * SR)]]))
    assert m.mono_warning


def test_per_band_flags_only_affected_band():
    """Invert only content above 8 kHz: only the 'High' band goes negative."""
    from scipy import signal

    n = noise(3.0, SR)[0]
    hp = signal.sosfiltfilt(signal.butter(8, 11000, "hp", fs=SR, output="sos"), n)
    lp = n - hp
    m, _ = run(np.vstack([lp + hp, lp - hp]))
    assert m.band_flags == [False, False, False, True]


def test_goniometer_orientation():
    t = np.arange(1000) / SR
    s = np.sin(2 * np.pi * 100 * t)
    x, y = goniometer_points(np.vstack([s, s]))
    assert np.allclose(x, 0)  # mono -> vertical line
    x, y = goniometer_points(np.vstack([s, np.zeros_like(s)]))
    assert np.allclose(x, -y)  # left only -> up-left diagonal
    assert correlation(np.vstack([s, -s])) == pytest.approx(-1.0)
