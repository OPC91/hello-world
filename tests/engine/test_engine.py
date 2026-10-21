import time

import numpy as np

from mastermind.dsp.chain import MasteringChain
from mastermind.engine.commands import CommandQueue, Reorder, SetBypass, SetParam
from mastermind.engine.realtime import PlaybackEngine
from mastermind.engine.ringbuffer import AudioRingBuffer, SnapshotQueue
from tests.conftest import music_like


def test_ring_buffer_wraps_and_underruns():
    rb = AudioRingBuffer(10)
    a = np.arange(14, dtype=np.float32).reshape(2, 7)
    assert rb.write(a) == 7
    out = np.empty((2, 5), dtype=np.float32)
    assert rb.read_into(out) == 5
    assert rb.write(a) == 7  # wraps around
    assert rb.write(a) == 1  # only one frame of space left
    out = np.empty((2, 12), dtype=np.float32)
    assert rb.read_into(out) == 10
    np.testing.assert_array_equal(out[0, :2], [5, 6])
    np.testing.assert_array_equal(out[:, 10:], 0)  # zero-padded underrun


def test_ring_buffer_flush():
    rb = AudioRingBuffer(16)
    rb.write(np.ones((2, 8), dtype=np.float32))
    rb.request_flush()
    out = np.empty((2, 4), dtype=np.float32)
    assert rb.read_into(out) == 0


def test_snapshot_queue_bounded():
    q = SnapshotQueue(maxlen=3)
    for i in range(5):
        q.push({"i": i})
    assert [s["i"] for s in q.drain()] == [2, 3, 4]
    assert q.drain() == []


def test_command_queue_applies_in_order():
    c = MasteringChain.default()
    q = CommandQueue()
    q.post(SetParam("limiter", "ceiling_db", -2.0))
    q.post(SetBypass("glue", True))
    order = [s.processor.type_id for s in c.slots][::-1]
    q.post(Reorder(tuple(order)))
    assert q.drain_into(c) == 3
    assert c.find("limiter").get_param("ceiling_db") == -2.0
    assert c.slots[c.index_of("glue")].bypass
    assert c.slots[-1].processor.type_id == "dither"


def test_engine_runs_headless_and_applies_commands(sr):
    eng = PlaybackEngine(use_device=False, block_size=512)
    eng.load(music_like(2.0, sr), sr)
    try:
        assert not eng.has_device
        eng.play()
        eng.post(SetParam("limiter", "drive_db", 9.0))
        deadline = time.time() + 5
        snaps = []
        while time.time() < deadline and len(snaps) < 10:
            snaps += eng.meters.drain()
            time.sleep(0.02)
        assert len(snaps) >= 10
        assert "limiter.gr" in snaps[-1]
        snap = eng.snapshot_chain()
        assert snap.find("limiter").get_param("drive_db") == 9.0
        assert snap is not eng.chain
        assert eng.position > 0
        eng.seek(0)
        time.sleep(0.1)
        assert eng.position < sr
    finally:
        eng.stop_engine()


def test_post_without_running_engine_applies_immediately():
    eng = PlaybackEngine(use_device=False)
    eng.post(SetParam("eq", "mode", "linear"))
    assert eng.chain.find("eq").get_param("mode") == "linear"
