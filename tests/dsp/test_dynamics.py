import numpy as np
import pytest

from mastermind.dsp.modules.glue_comp import GlueCompressor
from mastermind.dsp.modules.multiband_comp import MultibandCompressor
from mastermind.dsp.primitives.dynamics import CompressorCore, static_curve_gr
from tests.conftest import music_like, sine


def test_static_curve():
    lv = np.array([-40.0, -20.0, -10.0, 0.0])
    gr = static_curve_gr(lv, threshold_db=-20.0, ratio=4.0, knee_db=0.0)
    np.testing.assert_allclose(gr, [0, 0, -7.5, -15.0])
    # Soft knee is continuous at its edges and between hard-knee value and zero inside.
    soft = static_curve_gr(np.array([-23.0, -20.0, -17.0]), -20.0, 4.0, 6.0)
    assert soft[0] == 0.0
    assert soft[2] == pytest.approx(-0.75 * 3.0)
    assert -0.75 * 0.0 > soft[1] > -0.75 * 3.0


def test_steady_state_gain_reduction(sr):
    core = CompressorCore(sr)
    core.configure(-20.0, 4.0, 1.0, 50.0, 0.0)
    gr = core.gain_reduction_db(np.full((2, sr // 2), 0.5))
    # Constant -6.02 dBFS, 13.98 dB over a 4:1 threshold -> -10.485 dB once settled.
    assert gr[-1] == pytest.approx(-0.75 * (20 * np.log10(0.5) + 20.0), abs=1e-3)


def test_auto_makeup():
    core = CompressorCore(48000)
    core.configure(-30.0, 3.0, 10, 100, 0.0)
    assert core.auto_makeup_db() == pytest.approx(8.0)


def test_multiband_neutral_is_magnitude_flat(sr):
    mb = MultibandCompressor()  # threshold 0 dB: no compression on a quiet signal
    mb.prepare(sr, 1 << 15)
    x = np.zeros((2, 1 << 15))
    x[:, 0] = 0.01
    y = mb.process(x)
    h = np.abs(np.fft.rfft(y[0] / 0.01))
    assert np.max(np.abs(20 * np.log10(h))) < 0.01


def test_multiband_compresses_only_target_band(sr):
    mb = MultibandCompressor(b1_threshold=-40.0, b1_ratio=10.0, auto_makeup=False)
    mb.prepare(sr, sr)
    low = sine(60, 1.0, sr, 0.5)
    high = sine(3000, 1.0, sr, 0.5)
    y_low = mb.process(low.copy())
    mb.reset()
    y_high = mb.process(high.copy())
    assert np.abs(y_low[0, sr // 2 :]).max() < 0.25
    assert np.abs(y_high[0, sr // 2 :]).max() == pytest.approx(0.5, abs=0.01)
    assert mb.meter_taps()["gr_b3"] == 0.0


def test_glue_mix_zero_is_dry(sr):
    g = GlueCompressor(threshold=-30.0, ratio=4.0, mix=0.0)
    g.prepare(sr, 2048)
    x = music_like(0.2, sr)
    np.testing.assert_allclose(g.process(x.copy()), x, atol=1e-15)


def test_glue_static_level_relationship(sr):
    """Quiet part (below threshold) untouched; loud part reduced by the 4:1 slope."""
    g = GlueCompressor(threshold=-25.0, ratio=4.0, knee=0.0)
    g.prepare(sr, sr)
    rms = lambda s: 20 * np.log10(np.sqrt(np.mean(s**2)))  # noqa: E731
    quiet = sine(1000, 0.5, sr, amp=np.sqrt(2) * 10 ** (-30 / 20))
    loud = sine(1000, 0.5, sr, amp=np.sqrt(2) * 10 ** (-6 / 20))
    y = g.process(np.concatenate([quiet, loud], axis=-1))
    tail = slice(sr // 2 - sr // 10, sr // 2)
    assert rms(y[0, tail]) == pytest.approx(-30.0, abs=0.05)
    assert rms(y[0, sr // 2 :][tail]) == pytest.approx(-25 + 19 / 4, abs=0.3)
    assert g.meter_taps()["gr"] == pytest.approx(-19 * 0.75, abs=0.5)
