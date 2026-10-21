import numpy as np
import pytest

from mastermind.dsp.base import ParamSet, ParamSpec


def make():
    ps = ParamSet(
        [
            ParamSpec("g", "Gain", 0.0, -10, 10, smooth=True),
            ParamSpec("f", "Freq", 100.0, 20, 20000, log=True),
            ParamSpec("m", "Mode", "a", kind="choice", choices=("a", "b")),
            ParamSpec("n", "N", 2, 1, 8, kind="int"),
        ],
        ramp_ms=10,
    )
    ps.prepare(1000)  # 10-sample ramp
    return ps


def test_clamp_and_coerce():
    ps = make()
    ps.set("f", 1e9)
    assert ps.get("f") == 20000
    ps.set("n", 3.6)
    assert ps.get("n") == 4
    with pytest.raises(ValueError):
        ps.set("m", "zzz")
    with pytest.raises(ValueError):
        ps.set("f", float("nan"))


def test_ramp_is_block_size_invariant():
    a, b = make(), make()
    a.set("g", 5.0)
    b.set("g", 5.0)
    ra = np.concatenate([np.broadcast_to(a.ramp("g", 3), 3) for _ in range(5)])
    rb = np.concatenate([np.broadcast_to(b.ramp("g", 15), 15)])
    np.testing.assert_allclose(ra, rb)
    assert ra[-1] == 5.0 and ra[9] == 5.0 and ra[0] == pytest.approx(0.5)


def test_non_smoothed_change_is_tracked():
    ps = make()
    ps.consume_changes()
    ps.set("f", 200)
    assert ps.consume_changes() == {"f"}
    assert ps.consume_changes() == set()
