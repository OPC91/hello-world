import pytest

from mastermind.dsp.metering.analysis import analyze
from mastermind.engine.offline import render
from mastermind.matching.loudness_fit import fit_limiter_drive, probe_segments
from mastermind.presets.store import PresetLibrary
from tests.conftest import music_like

SR = 48000


@pytest.fixture(scope="module")
def mix():
    x = music_like(40.0, SR)
    return x * 10 ** ((-18 - analyze(x, SR).integrated_lufs) / 20)


@pytest.mark.parametrize("pid", ["electronic/house", "other/folk", "other/classical"])
def test_fit_hits_target_on_full_render(tmp_path, mix, pid):
    p = PresetLibrary(tmp_path).get(pid)
    chain = p.build_chain()
    before = chain.find("limiter").get_param("drive_db")
    fit = fit_limiter_drive(chain, mix, SR, p.target.lufs)
    assert chain.find("limiter").get_param("drive_db") == before  # input chain untouched
    assert fit.converged and fit.renders <= 6
    chain.set_param("limiter", "drive_db", fit.drive_db)
    a = analyze(render(chain, mix, SR), SR)
    assert a.integrated_lufs == pytest.approx(p.target.lufs, abs=0.5)  # probe vs whole file
    assert a.true_peak_dbtp <= p.target.true_peak_ceiling


def test_unreachable_target_reports_not_converged(tmp_path, mix):
    chain = PresetLibrary(tmp_path).get("other/jazz").build_chain()
    fit = fit_limiter_drive(chain, mix * 0.01, SR, -3.0)
    assert not fit.converged


def test_probe_segments():
    assert probe_segments(10 * SR, SR) == [(0, 10 * SR, 0)]
    segs = probe_segments(300 * SR, SR, n_segments=6, segment_s=5)
    assert len(segs) == 6 and all(b - a == 6 * SR for a, b, _ in segs) and segs[-1][1] == 300 * SR
