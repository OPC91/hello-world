"""A/B comparison and loudness-matched monitoring in the real-time engine."""

import numpy as np
import pytest

from mastermind.dsp.chain import MasteringChain
from mastermind.engine.commands import SetParam
from mastermind.engine.realtime import ORIGINAL, PlaybackEngine
from tests.conftest import music_like

SR = 48000
BLOCK = 1024


def offline_engine(chain=None) -> PlaybackEngine:
    """Engine driven by hand (no threads): load state without starting the worker."""
    eng = PlaybackEngine(chain, block_size=BLOCK, use_device=False)
    eng._sr = SR
    for c in eng.chains.values():
        c.prepare(SR, BLOCK)
    return eng


def run(eng, x, playing=True):
    heard, raw = [], []
    for i in range(0, x.shape[1] - BLOCK + 1, BLOCK):
        h, y = eng._process(x[:, i : i + BLOCK], playing)
        heard.append(h)
        raw.append(y)
    return np.concatenate(heard, -1), np.concatenate(raw, -1)


def loud_chain(gain_db=8.0) -> MasteringChain:
    c = MasteringChain.default()
    c.set_param("input_gain", "gain_db", gain_db)
    return c


def test_b_is_a_copy_and_independent():
    eng = offline_engine()
    eng.enable_b()
    assert eng.chains["B"].to_dict() == eng.chains["A"].to_dict()
    eng.set_active("B")
    eng.post(SetParam("input_gain", "gain_db", 5.0))  # engine not running -> immediate
    assert eng.chains["B"].find("input_gain").get_param("gain_db") == 5.0
    assert eng.chains["A"].find("input_gain").get_param("gain_db") == 0.0


def test_queued_edits_keep_their_target_after_switch():
    eng = offline_engine()
    eng.enable_b()
    eng._run = True  # pretend the worker runs so commands are queued
    eng.post(SetParam("input_gain", "gain_db", 3.0))  # meant for A
    eng.set_active("B")
    eng.commands.drain(eng._apply)
    assert eng.chains["A"].find("input_gain").get_param("gain_db") == 3.0
    assert eng.chains["B"].find("input_gain").get_param("gain_db") == 0.0


def transparent_chain(eq_mode: str) -> MasteringChain:
    """All modules bypassed except a flat EQ in ``eq_mode`` (only adds latency in linear mode)."""
    c = MasteringChain.default()
    for slot in c.slots:
        slot.set_bypass(slot.processor.type_id != "eq")
    c.set_param("eq", "mode", eq_mode)
    return c


def test_paths_are_latency_aligned():
    """A (linear-phase, high latency), B (minimum-phase) and Original line up sample-exactly."""
    x = music_like(2.0, SR) * 0.3
    outs = {}
    for monitor in ("A", "B", ORIGINAL):
        eng = offline_engine(transparent_chain("linear"))
        eng.enable_b()
        eng.chains["B"] = transparent_chain("minimum")
        eng.chains["B"].prepare(SR, BLOCK)
        eng.loudness_match = False
        if monitor == ORIGINAL:
            eng.monitor_original = True
        else:
            eng.set_active(monitor)
        _, outs[monitor] = run(eng, x)
    assert eng._latency > 1000
    lat = eng._latency
    for k in ("A", "B", ORIGINAL):
        np.testing.assert_allclose(outs[k][:, lat:], x[:, : outs[k].shape[1] - lat], atol=2e-3, err_msg=k)


def test_loudness_match_turns_louder_path_down_to_original():
    eng = offline_engine(loud_chain(8.0))
    x = music_like(12.0, SR) * 0.1
    heard, raw = run(eng, x)
    ab = eng.ab_state()
    assert ab["loudness"]["A"] - ab["loudness"][ORIGINAL] == pytest.approx(8.0, abs=0.3)
    assert eng._gain_db == pytest.approx(-8.0, abs=0.3)
    tail = slice(-3 * SR, None)
    rms = lambda s: 20 * np.log10(np.sqrt(np.mean(s**2)))  # noqa: E731
    assert rms(raw[:, tail]) - rms(heard[:, tail]) == pytest.approx(8.0, abs=0.3)  # meters unmatched


def test_loudness_match_off_and_original_monitoring():
    eng = offline_engine(loud_chain(8.0))
    eng.loudness_match = False
    x = music_like(6.0, SR) * 0.1
    heard, raw = run(eng, x)
    np.testing.assert_array_equal(heard, raw)
    eng.monitor_original = True
    heard, raw = run(eng, x[:, : 4 * BLOCK])
    lat = eng._latency
    np.testing.assert_allclose(raw[:, lat + BLOCK :], x[:, BLOCK : 4 * BLOCK - lat], atol=1e-12)


def test_switch_is_crossfaded():
    eng = offline_engine(loud_chain(12.0))
    eng.loudness_match = False
    x = np.full((2, 20 * BLOCK), 0.05)
    run(eng, x)
    eng.monitor_original = True
    heard, _ = run(eng, x[:, : 2 * BLOCK])
    assert np.abs(np.diff(heard[0])).max() < 0.01  # gradual, not a jump


def test_disable_b_returns_to_a():
    eng = offline_engine()
    eng.enable_b()
    eng.set_active("B")
    eng.disable_b()
    assert eng.active == "A" and not eng.ab_enabled
    with pytest.raises(KeyError):
        eng.set_active("B")
