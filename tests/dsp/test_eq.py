import numpy as np
import pytest

from mastermind.dsp.modules.eq import ParametricEQ
from mastermind.dsp.primitives.biquad import design, sos_response
from tests.conftest import sine


def configured_eq(mode="minimum"):
    eq = ParametricEQ(mode=mode)
    for i, (t, f, g, q) in enumerate(
        [
            ("low_shelf", 80, 3.0, 0.7),
            ("bell", 300, -4.0, 1.5),
            ("bell", 2500, 2.5, 0.8),
            ("high_shelf", 10000, -3.0, 0.7),
        ],
        start=1,
    ):
        eq.params.update({f"b{i}_on": True, f"b{i}_type": t, f"b{i}_freq": f, f"b{i}_gain": g, f"b{i}_q": q})
    return eq


def impulse_magnitude_db(eq, sr, n=1 << 16):
    eq.prepare(sr, n)
    x = np.zeros((2, n))
    lat = eq.latency_samples
    x[:, 0] = 1.0
    y = eq.process(x)
    return np.fft.rfftfreq(n, 1 / sr), 20 * np.log10(np.abs(np.fft.rfft(y[0]))), y, lat


def test_min_phase_matches_design(sr):
    eq = configured_eq()
    f, mag, _, lat = impulse_magnitude_db(eq, sr)
    assert lat == 0
    expected = eq.response_db(f[1:], sr)["mid"]
    np.testing.assert_allclose(mag[1:], expected, atol=1e-6)


def test_linear_phase_matches_min_phase_magnitude(sr):
    eq = configured_eq("linear")
    f, mag, y, lat = impulse_magnitude_db(eq, sr)
    target = configured_eq().response_db(f, sr)["mid"]
    band = (f > 40) & (f < 20000)
    assert np.max(np.abs(mag[band] - target[band])) < 0.25


def test_linear_phase_is_symmetric(sr):
    eq = configured_eq("linear")
    _, _, y, lat = impulse_magnitude_db(eq, sr)
    k = y[0, : 2 * lat + 1]
    np.testing.assert_allclose(k, k[::-1], atol=1e-12)
    assert np.argmax(np.abs(k)) == lat


def test_side_band_leaves_mono_untouched(sr):
    eq = ParametricEQ(b1_on=True, b1_type="bell", b1_freq=1000, b1_gain=12.0, b1_target="side")
    eq.prepare(sr, 4096)
    x = sine(1000, 0.2, sr, amp=0.5)
    np.testing.assert_allclose(eq.process(x.copy()), x, atol=1e-12)


def test_mid_band_boosts_mono(sr):
    eq = ParametricEQ(b1_on=True, b1_type="bell", b1_freq=1000, b1_gain=6.0, b1_q=1.0, b1_target="mid")
    eq.prepare(sr, sr)
    y = eq.process(sine(1000, 1.0, sr, amp=0.1))
    assert 20 * np.log10(np.abs(y[0, sr // 2 :]).max() / 0.1) == pytest.approx(6.0, abs=0.05)


@pytest.mark.parametrize("kind", ["bell", "low_shelf", "high_shelf", "low_cut", "high_cut", "notch", "allpass"])
def test_rbj_designs_are_stable(kind):
    sos = design(kind, 1000, 48000, 0.7, 6.0)
    assert np.all(np.abs(np.roots([1, sos[4], sos[5]])) < 1)
    h = sos_response(sos, np.array([1000.0]), 48000)
    assert np.isfinite(h).all()
