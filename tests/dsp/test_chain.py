"""Chain behaviour: block-size invariance, bypass, latency, serialisation."""

import json

import numpy as np
import pytest

from mastermind.dsp.chain import MasteringChain
from mastermind.engine.offline import render
from tests.conftest import music_like


def busy_chain() -> MasteringChain:
    c = MasteringChain.default()
    c.set_param("input_gain", "gain_db", 2.0)
    for k, v in {
        "mode": "linear",
        "b3_on": True,
        "b3_gain": 3.0,
        "b5_on": True,
        "b5_target": "side",
        "b5_gain": -2.0,
    }.items():
        c.set_param("eq", k, v)
    c.set_param("multiband", "b1_threshold", -25.0)
    c.set_param("multiband", "b3_threshold", -30.0)
    c.set_bypass("saturation", False)
    c.set_param("saturation", "drive_db", 6.0)
    c.set_param("saturation", "mode", "tube")
    c.set_param("imager", "width_b4", 140.0)
    c.set_param("imager", "mono_maker", True)
    c.set_param("glue", "threshold", -20.0)
    c.set_param("limiter", "drive_db", 6.0)
    return c


@pytest.mark.parametrize("block", [64, 1000, 4096])
def test_block_size_invariance(sr, block):
    """Real-time (small blocks) and offline (large blocks) must produce the same audio."""
    x = music_like(1.0, sr)
    ref = render(busy_chain(), x, sr, block_size=1024)
    y = render(busy_chain(), x, sr, block_size=block)
    np.testing.assert_allclose(y, ref, atol=1e-9)


def test_all_bypassed_is_identity_after_latency_alignment(sr):
    c = busy_chain()
    for s in c.slots:
        s.set_bypass(True)
    x = music_like(0.5, sr)
    y = render(c, x, sr)
    assert c.latency_samples > 0
    np.testing.assert_array_equal(y, x)  # bit-exact


def test_bypass_crossfade_is_smooth(sr):
    c = MasteringChain.default()
    c.set_param("input_gain", "gain_db", 12.0)
    c.prepare(sr, 256)
    x = np.full((2, 256), 0.1)
    for _ in range(40):  # get past the chain latency and settle
        c.process(x)
    out = [c.process(x)]
    c.set_bypass("input_gain", True)
    for _ in range(8):
        out.append(c.process(x))
    y = np.concatenate(out, axis=-1)[0]
    assert np.abs(np.diff(y)).max() < 0.01  # no step
    assert y[-1] == pytest.approx(0.1, rel=1e-3)  # ends at bypassed level (limiter idle)


def test_latency_constant_under_bypass(sr):
    c = busy_chain()
    c.prepare(sr, 512)
    lat = c.latency_samples
    c.set_bypass("eq", True)
    c.process(np.zeros((2, 512)))
    assert c.latency_samples == lat


def test_serialisation_round_trip(sr):
    c = busy_chain()
    data = json.loads(json.dumps(c.to_dict()))
    c2 = MasteringChain.from_dict(data)
    assert c2.to_dict() == c.to_dict()
    x = music_like(0.5, sr)
    np.testing.assert_allclose(render(c2, x, sr), render(busy_chain(), x, sr), atol=1e-12)


def test_future_version_rejected():
    with pytest.raises(ValueError):
        MasteringChain.from_dict({"version": 999, "slots": []})


def test_reorder_keeps_dither_last():
    c = MasteringChain.default()
    c.move(c.index_of("dither"), 0)
    assert c.slots[-1].processor.type_id == "dither"
    order = [s.processor.type_id for s in c.slots]
    order.remove("glue")
    order.insert(1, "glue")
    c.reorder(order)
    assert [s.processor.type_id for s in c.slots][1] == "glue"
    assert c.slots[-1].processor.type_id == "dither"


def test_reorder_changes_output(sr):
    x = music_like(0.5, sr)
    a = MasteringChain.default()
    a.set_bypass("saturation", False)
    a.set_param("saturation", "drive_db", 12.0)
    a.set_param("input_gain", "gain_db", 6.0)
    b = a.copy()
    order = [s.processor.type_id for s in b.slots]
    order.remove("saturation")
    order.insert(0, "saturation")
    b.reorder(order)
    assert not np.allclose(render(a, x, sr), render(b, x, sr))


def test_meter_taps_are_namespaced(sr):
    c = busy_chain()
    render(c, music_like(0.3, sr), sr)
    taps = c.meter_taps()
    assert {"limiter.gr", "glue.gr", "multiband.gr_b1"} <= set(taps)
    assert taps["limiter.gr"] <= 0
