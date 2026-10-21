"""GUI meter widgets: colour coding, source toggle, analysis panel."""

import math

import numpy as np
import pytest

pytestmark = pytest.mark.gui


def snapshot(lufs=-14.0, tp=-1.5, corr=0.8, bands=(0.9, 0.8, 0.5, -0.2), mono=False):
    return {
        "momentary": lufs,
        "short_term": lufs,
        "integrated": lufs,
        "lra": 6.0,
        "max_momentary": lufs + 1,
        "max_short_term": lufs + 0.5,
        "true_peak": tp,
        "block_true_peak": tp,
        "sample_peak": tp,
        "plr": tp - lufs,
        "crest": 9.0,
        "corr": corr,
        "corr_hold": min(corr, 0.1),
        "corr_bands": list(bands),
        "corr_band_flags": [b < 0 for b in bands],
        "mono_warning": mono,
        "spectrum": None,
        "gonio": (np.zeros(4), np.zeros(4)),
    }


@pytest.mark.parametrize(
    "lufs,expected",
    [(-14.5, "ok"), (-16.0, "warn"), (-11.5, "warn"), (-20.0, "bad"), (-8.0, "bad")],
)
def test_loudness_colour_coding(qtbot, lufs, expected):
    from mastermind.gui.meters.loudness_panel import LoudnessPanel

    p = LoudnessPanel()
    qtbot.addWidget(p)
    p.target_box.setCurrentIndex(0)  # Spotify -14
    p.update_values(snapshot(lufs), "out", ceiling=-1.0)
    assert p.readout_status()["I"] == expected


def test_true_peak_red_above_ceiling(qtbot):
    from mastermind.gui.meters.loudness_panel import LoudnessPanel

    p = LoudnessPanel()
    qtbot.addWidget(p)
    p.update_values(snapshot(tp=-0.5), "out", ceiling=-1.0)
    assert p.readout_status()["TP"] == "bad"
    p.update_values(snapshot(tp=-1.2), "out", ceiling=-1.0)
    assert p.readout_status()["TP"] == "ok"


def test_custom_target(qtbot):
    from mastermind.dsp.metering.targets import LOUDNESS_TARGETS
    from mastermind.gui.meters.loudness_panel import LoudnessPanel

    p = LoudnessPanel()
    qtbot.addWidget(p)
    p.target_box.setCurrentIndex(list(LOUDNESS_TARGETS).index("Custom"))
    assert p.custom.isEnabled()
    p.custom.setValue(-11.0)
    assert p.target == -11.0
    assert p.target_line.value() == -11.0


def test_stereo_panel_flags_negative_band_and_mono_warning(qtbot):
    from mastermind.gui.meters.stereo_panel import StereoPanel

    p = StereoPanel()
    qtbot.addWidget(p)
    p.update_values(snapshot(mono=True))
    assert "High" in p.band_warning.text()
    assert not p.correlation.warning.isHidden()
    p.update_values(snapshot(bands=(1, 1, 1, 1)))
    assert p.band_warning.text() == ""
    assert p.correlation.warning.isHidden()


def test_window_meters_follow_source_toggle(qtbot, wav_file):
    from mastermind.engine.commands import SetParam
    from mastermind.gui.main_window import MainWindow

    w = MainWindow(use_device=False)
    qtbot.addWidget(w)
    w.engine.post(SetParam("input_gain", "gain_db", -12.0))
    w.open_file(wav_file)
    w.toggle_play()
    qtbot.waitUntil(lambda: math.isfinite(_momentary(w)), timeout=8000)
    w.meters.set_source("out")
    qtbot.wait(300)
    out = _momentary(w)
    w.meters.set_source("in")
    qtbot.wait(300)
    inp = _momentary(w)
    assert inp - out == pytest.approx(12.0, abs=1.5)
    w.close()


def _momentary(w) -> float:
    txt = w.meters.loudness.m.number.text().split()[0].replace("−", "-")
    return float("-inf") if txt in ("-∞", "—") else float(txt)


def test_analysis_panel_runs(qtbot, wav_file):
    from mastermind.gui.main_window import MainWindow

    w = MainWindow(use_device=False)
    qtbot.addWidget(w)
    w.open_file(wav_file)
    th = w.run_analysis()
    qtbot.waitUntil(lambda: w.analysis.results is not None, timeout=30000)
    th.wait(5000)
    a_in, a_out = w.analysis.results
    assert w.analysis.cell("integrated_lufs", 0) == f"{a_in.integrated_lufs:.1f}"
    assert a_out.true_peak_dbtp <= w.ceiling
    w.close()
