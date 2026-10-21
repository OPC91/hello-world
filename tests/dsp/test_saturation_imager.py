import numpy as np
import pytest

from mastermind.dsp.modules.imager import StereoImager
from mastermind.dsp.modules.saturation import Saturation
from mastermind.dsp.primitives.oversampling import Oversampler
from tests.conftest import noise, sine


def harmonic_db(y, sr, f):
    spec = np.abs(np.fft.rfft(y * np.hanning(y.size)))
    freqs = np.fft.rfftfreq(y.size, 1 / sr)
    k = np.argmin(np.abs(freqs - f))
    return 20 * np.log10(spec[k - 2 : k + 3].max() / spec.max())


def run(proc, x, sr):
    proc.prepare(sr, x.shape[1])
    lat = proc.latency_samples
    y = proc.process(x.copy())
    return y[:, lat:], x[:, : x.shape[1] - lat]


@pytest.mark.parametrize("mode", ["tape", "tube", "soft_clip"])
def test_small_signal_unity(sr, mode):
    y, x = run(Saturation(mode=mode, drive_db=12.0), sine(1000, 0.5, sr, amp=1e-4), sr)
    seg = slice(sr // 10, None)
    assert np.abs(y[0, seg]).max() / 1e-4 == pytest.approx(1.0, abs=0.01)


def test_tube_adds_even_harmonics_tape_mostly_odd(sr):
    x = sine(1000, 1.0, sr, amp=0.5)
    tube, _ = run(Saturation(mode="tube", drive_db=12.0, asymmetry=1.0), x, sr)
    clip, _ = run(Saturation(mode="soft_clip", drive_db=12.0), x, sr)
    assert harmonic_db(tube[0], sr, 2000) > -40
    assert harmonic_db(clip[0], sr, 2000) < -80
    assert harmonic_db(clip[0], sr, 3000) > -40


def test_oversampling_reduces_aliasing():
    sr = 44100
    x = sine(15000, 1.0, sr, amp=0.9)

    def alias_energy(factor):
        y, _ = run(Saturation(mode="soft_clip", drive_db=18.0, oversampling=factor), x, sr)
        # 3rd harmonic 45 kHz aliases to 44.1 - 45 -> 0.9 kHz region (|45000 - 44100| = 900 Hz).
        return harmonic_db(y[0], sr, 900)

    assert alias_energy("4") < alias_energy("1") - 20


def test_oversampler_round_trip(sr):
    os = Oversampler(4)
    x = sine(5000, 0.2, sr, amp=0.5)
    y = os.down(os.up(x))
    L = os.latency
    np.testing.assert_allclose(y[:, L + 500 :], x[:, 500:-L], atol=1e-3)


def test_mix_zero_is_delayed_dry(sr):
    x = noise(0.2, sr)
    y, xd = run(Saturation(mode="tape", drive_db=20.0, mix=0.0), x, sr)
    np.testing.assert_allclose(y, xd, atol=1e-15)


def test_imager_unity_width_is_magnitude_flat(sr):
    im = StereoImager()
    im.prepare(sr, 1 << 15)
    x = np.zeros((2, 1 << 15))
    x[0, 0] = 1.0  # hard-left impulse (mid and side)
    y = im.process(x)
    for ch in range(2):
        h = np.abs(np.fft.rfft(y[ch])[1:])
        target = np.abs(np.fft.rfft(x[ch])[1:])
        if target.max() > 0:
            assert np.max(np.abs(20 * np.log10(h / target))) < 0.01


def test_imager_zero_width_is_mono(sr):
    im = StereoImager(**{f"width_b{i}": 0.0 for i in range(1, 5)})
    im.prepare(sr, sr)
    y = im.process(noise(0.5, sr))
    np.testing.assert_allclose(y[0], y[1], atol=1e-12)


def test_mono_maker_removes_low_side_only(sr):
    im = StereoImager(mono_maker=True, mono_freq=120.0)
    im.prepare(sr, sr)
    side_low = np.vstack([sine(40, 1.0, sr)[0], -sine(40, 1.0, sr)[0]]) * 0.5
    y = im.process(side_low)
    assert np.abs(y[:, sr // 2 :]).max() < 0.01  # < -34 dB, LR4 at 1.6 oct below
    im.reset()
    side_high = np.vstack([sine(2000, 1.0, sr)[0], -sine(2000, 1.0, sr)[0]]) * 0.5
    y = im.process(side_high)
    assert np.abs(y[:, sr // 2 :]).max() == pytest.approx(0.5, abs=0.005)
