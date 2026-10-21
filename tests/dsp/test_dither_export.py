import numpy as np
import pytest
import soundfile as sf

from mastermind.dsp.chain import MasteringChain
from mastermind.dsp.modules.dither import Dither
from mastermind.engine.offline import render_to_file
from mastermind.io.audio_file import load_audio
from mastermind.io.export import write_wav
from tests.conftest import music_like, sine


def test_unarmed_dither_is_passthrough():
    d = Dither()
    d.prepare(48000, 512)
    x = np.random.default_rng(0).standard_normal((2, 512)) * 0.1
    np.testing.assert_array_equal(d.process(x.copy()), x)


@pytest.mark.parametrize("bits", [16, 24])
def test_dither_output_on_grid_with_tpdf_statistics(bits):
    d = Dither()
    d.prepare(48000, 1 << 16)
    d.arm(bits, seed=1)
    lsb = 2.0 ** -(bits - 1)
    x = np.random.default_rng(2).uniform(-0.5, 0.5, (2, 1 << 16))
    y = d.process(x.copy())
    k = y / lsb
    np.testing.assert_allclose(k, np.round(k), atol=1e-6)
    err = (y - x) / lsb
    assert abs(err.mean()) < 0.01
    # Total error variance for TPDF dither + rounding is LSB^2/4.
    assert err.var() == pytest.approx(0.25, rel=0.03)


def test_noise_shaping_tilts_spectrum_up():
    d = Dither(noise_shaping=True)
    d.prepare(48000, 1 << 16)
    d.arm(16, seed=0)
    x = np.zeros((2, 1 << 16))
    e = d.process(x.copy())[0]
    spec = np.abs(np.fft.rfft(e)) ** 2
    n = spec.size
    assert spec[: n // 8].mean() < spec[-n // 8 :].mean() / 10


@pytest.mark.parametrize("depth,subtype", [("16", "PCM_16"), ("24", "PCM_24"), ("32f", "FLOAT")])
def test_write_wav_round_trip(tmp_path, depth, subtype):
    x = sine(440, 0.1, 48000, amp=0.5)
    path = tmp_path / "x.wav"
    write_wav(path, x, 48000, depth)
    assert sf.info(str(path)).subtype == subtype
    y = load_audio(path).samples
    tol = {"16": 2**-15, "24": 2**-23, "32f": 1e-7}[depth]
    np.testing.assert_allclose(y, x, atol=tol)


def test_dithered_export_values_are_exact(tmp_path, sr):
    x = music_like(0.5, sr) * 0.3
    path = tmp_path / "m.wav"
    y = render_to_file(MasteringChain.default(), x, sr, path, "16", dither=True)
    ints, _ = sf.read(str(path), dtype="int16")
    np.testing.assert_array_equal(ints.T.astype(np.float64) / 32768.0, y)
