"""Loudness meter validation against EBU Tech 3341 (meter) and Tech 3342 (LRA).

The EBU test files are synthetic 1 kHz tones / tone sequences, regenerated here
from their published descriptions (stereo, 48 kHz, levels in dBFS per channel).
Tolerances are those stated by EBU: +-0.1 LU for M/S/I, +-1 LU for LRA,
+0.2/-0.4 dB for true peak.
"""

import numpy as np
import pyloudnorm
import pytest

from mastermind.dsp.metering.kweight import BS1770_48K, kweighting_sos
from mastermind.dsp.metering.loudness import LoudnessMeter
from tests.conftest import music_like, noise

SR = 48000


def tone(dbfs: float, seconds: float, freq: float = 1000.0, sr: int = SR, phase: float = 0.0) -> np.ndarray:
    t = np.arange(int(round(seconds * sr))) / sr
    return np.tile(10 ** (dbfs / 20) * np.sin(2 * np.pi * freq * t + phase), (2, 1))


def seq(*parts: tuple[float, float]) -> np.ndarray:
    return np.concatenate([tone(db, s) for db, s in parts], axis=-1)


def meter(x: np.ndarray, sr: int = SR, block: int = 1024, track=None) -> LoudnessMeter:
    m = LoudnessMeter(sr)
    for i in range(0, x.shape[-1], block):
        m.process(x[:, i : i + block])
        if track is not None:
            track(m)
    return m


def test_kweighting_matches_bs1770_coefficients():
    s = kweighting_sos(48000)
    np.testing.assert_allclose(s[0, :3], BS1770_48K["shelf_b"], atol=1e-12)
    np.testing.assert_allclose(s[0, 3:], BS1770_48K["shelf_a"], atol=1e-12)
    np.testing.assert_allclose(s[1, :3], BS1770_48K["hp_b"], atol=1e-12)
    np.testing.assert_allclose(s[1, 3:], BS1770_48K["hp_a"], atol=1e-12)


# ---- Tech 3341 "minimum requirements" cases 1-5 (integrated, plus M/S on steady tones) ----


@pytest.mark.parametrize("level", [-23.0, -33.0])
def test_3341_cases_1_2_steady_tone(level):
    m = meter(tone(level, 20.0))
    assert m.momentary == pytest.approx(level, abs=0.1)
    assert m.short_term == pytest.approx(level, abs=0.1)
    assert m.integrated == pytest.approx(level, abs=0.1)


@pytest.mark.parametrize(
    "parts",
    [
        [(-36, 10), (-23, 60), (-36, 10)],  # case 3: relative gate
        [(-72, 10), (-36, 10), (-23, 60), (-36, 10), (-72, 10)],  # case 4: absolute + relative gate
        [(-26, 20), (-20, 20.1), (-26, 20)],  # case 5
    ],
)
def test_3341_cases_3_4_5_gating(parts):
    assert meter(seq(*parts)).integrated == pytest.approx(-23.0, abs=0.1)


def test_3341_case_9_short_term_constant():
    """20 x (1.34 s @ -20 dBFS + 1.66 s @ -30 dBFS): short-term stays at -23 after 3 s."""
    x = seq(*[p for _ in range(20) for p in ((-20, 1.34), (-30, 1.66))])
    readings = []
    meter(x, block=SR // 10, track=lambda m: readings.append(m.short_term))
    steady = np.array(readings[31:])
    assert np.all(np.abs(steady + 23.0) <= 0.1), (steady.min(), steady.max())


def test_3341_case_12_momentary_constant():
    """20 x (0.18 s @ -20 dBFS + 0.22 s @ -30 dBFS): momentary stays at -23 after 0.4 s."""
    x = seq(*[p for _ in range(20) for p in ((-20, 0.18), (-30, 0.22))])
    readings = []
    meter(x, block=SR // 100, track=lambda m: readings.append(m.momentary))
    steady = np.array(readings[50:])
    assert np.all(np.abs(steady + 23.0) <= 0.1), (steady.min(), steady.max())


def test_momentary_max_and_short_term_max():
    m = meter(seq((-30, 5), (-18, 4), (-30, 5)))
    assert m.max_momentary == pytest.approx(-18.0, abs=0.1)
    assert m.max_short_term == pytest.approx(-18.0, abs=0.1)


@pytest.mark.parametrize("sr", [44100, 48000, 96000])
def test_sample_rate_independence(sr):
    assert meter(tone(-23, 10, sr=sr), sr).integrated == pytest.approx(-23.0, abs=0.1)


def test_matches_pyloudnorm_on_programme():
    for x in (music_like(15.0, SR) * 0.5, noise(10.0, SR, 0.1)):
        ref = pyloudnorm.Meter(SR).integrated_loudness(x.T)
        assert meter(x).integrated == pytest.approx(ref, abs=0.1)


def test_block_size_does_not_matter():
    x = music_like(8.0, SR)
    a, b = meter(x, block=97), meter(x, block=8192)
    assert a.integrated == pytest.approx(b.integrated, abs=1e-9)
    assert a.lra == pytest.approx(b.lra, abs=1e-9)


def test_silence_reads_minus_infinity():
    m = meter(np.zeros((2, SR * 2)))
    assert m.integrated == float("-inf")
    assert m.momentary == float("-inf")


# ---- Tech 3342 loudness range ----


@pytest.mark.parametrize(
    "parts,expected",
    [
        ([(-20, 20), (-30, 20)], 10.0),  # case 1
        ([(-20, 20), (-15, 20)], 5.0),  # case 2
        ([(-40, 20), (-20, 20)], 20.0),  # case 3
        ([(-50, 20), (-35, 20), (-20, 20), (-35, 20), (-50, 20)], 15.0),  # case 4
    ],
)
def test_3342_lra(parts, expected):
    assert meter(seq(*parts)).lra == pytest.approx(expected, abs=1.0)


# ---- Tech 3341 true-peak cases 15-18 (48 kHz) ----


@pytest.mark.parametrize(
    "freq,phase_deg",
    [(SR / 4, 0.0), (SR / 4, 45.0), (SR / 6, 60.0), (SR / 8, 67.5)],
)
def test_3341_true_peak_cases(freq, phase_deg):
    """Sines whose *true* peak is -6 dBFS but whose samples may be lower.

    A 20 ms raised-cosine fade-in/out avoids the start/stop discontinuity, whose
    band-limited reconstruction genuinely overshoots (a correct meter reads it).
    """
    x = tone(-6.0, 1.0, freq, phase=np.deg2rad(phase_deg))
    fade = int(0.02 * SR)
    ramp = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, fade))
    x[:, :fade] *= ramp
    x[:, -fade:] *= ramp[::-1]
    m = meter(x)
    assert -6.4 <= m.true_peak_dbtp <= -5.8


def test_true_peak_exceeds_sample_peak_for_intersample_overs():
    x = tone(0.0, 1.0, SR / 4, phase=np.pi / 4)  # samples at +-0.707, true peak 1.0
    m = meter(x)
    assert m.sample_peak_dbfs == pytest.approx(-3.01, abs=0.01)
    assert m.true_peak_dbtp > -0.4


def test_plr_and_crest():
    m = meter(tone(-20, 5))
    # Sine: crest 3.01 dB; PLR = -20 dBTP - (-20 LUFS) = 0 for a 1 kHz stereo tone (approx).
    assert m.crest_db == pytest.approx(3.01, abs=0.05)
    assert m.plr == pytest.approx(0.0, abs=0.15)
    assert m.short_term_crest_db == pytest.approx(3.01, abs=0.05)
