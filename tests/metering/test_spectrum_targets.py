import numpy as np
import pytest

from mastermind.dsp.metering.bank import MeterBank
from mastermind.dsp.metering.spectrum import SpectrumAnalyzer, long_term_spectrum, third_octave_centers
from mastermind.dsp.metering.targets import LOUDNESS_TARGETS, loudness_status, peak_status
from tests.conftest import sine

SR = 48000


def test_third_octave_centres():
    c = third_octave_centers()
    assert len(c) == 31
    assert c[0] == pytest.approx(19.69, abs=0.01) and c[-1] == pytest.approx(20158.7, abs=0.1)
    assert 1000.0 in c


def test_full_scale_sine_reads_zero_db_in_its_band():
    an = SpectrumAnalyzer(SR, grid=third_octave_centers())
    an.push(sine(1000, 0.5, SR))
    db = an.compute()
    k = np.argmin(np.abs(an.grid - 1000))
    assert db[k] == pytest.approx(0.0, abs=0.1)
    assert db[k - 3] < -40 and db[k + 3] < -40


def test_pink_noise_is_flat_on_third_octaves():
    rng = np.random.default_rng(0)
    n = 2**20
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / SR)
    spec[1:] /= np.sqrt(f[1:])
    spec[0] = 0
    pink = np.fft.irfft(spec, n)
    f, db = long_term_spectrum(np.vstack([pink, pink]), SR)
    mid = (f > 50) & (f < 16000)
    assert np.ptp(db[mid]) < 1.5


def test_long_term_spectrum_of_sine():
    f, db = long_term_spectrum(sine(250, 2.0, SR, amp=0.5), SR)
    k = np.argmin(np.abs(f - 250))
    assert db[k] == pytest.approx(20 * np.log10(0.5), abs=0.2)


def test_targets_and_status_colours():
    assert LOUDNESS_TARGETS["Spotify"] == -14 and LOUDNESS_TARGETS["Apple Music"] == -16
    assert LOUDNESS_TARGETS["YouTube"] == -14 and LOUDNESS_TARGETS["Club"] == -8 and LOUDNESS_TARGETS["CD"] == -9
    assert loudness_status(-14.5, -14) == "ok"
    assert loudness_status(-16.5, -14) == "warn"
    assert loudness_status(-10.0, -14) == "bad"
    assert loudness_status(float("-inf"), -14) == "none"
    assert peak_status(-0.9, -1.0) == "bad"
    assert peak_status(-1.0, -1.0) == "ok"


def test_meter_bank_snapshot_contents():
    bank = MeterBank(SR)
    x = sine(1000, 1.0, SR, amp=0.1)
    for i in range(0, x.shape[1], 1024):
        bank.process(x[:, i : i + 1024])
    snap = bank.snapshot()
    assert snap["corr"] == pytest.approx(1.0)
    assert snap["spectrum"] is not None and len(snap["spectrum"]) == len(bank.spectrum.grid)
    assert len(snap["gonio"][0]) <= 512
    assert snap["momentary"] == pytest.approx(-20.0, abs=0.1)
