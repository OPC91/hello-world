"""Real-time preview engine.

Threads::

    GUI thread --commands--> [CommandQueue] --+
                                              v
    engine thread:  source -> chain(s).process -> AudioRingBuffer -> PortAudio callback
                                    |
                                    +--> SnapshotQueue (meters) --> GUI timer (~30 fps)

Running the DSP on a dedicated engine thread rather than inside the PortAudio
callback keeps the callback trivial (a memcpy), so GIL contention from the GUI
cannot cause dropouts; the cost is ``prebuffer`` worth of added control
latency. The chain objects and the code path are identical to offline
rendering.

A/B comparison
--------------
The engine holds up to two chains, ``"A"`` and ``"B"``, plus the unprocessed
``"orig"`` path. All of them run every block (so switching is instant and their
state is warm) and are delay-aligned to the largest chain latency. One path is
*monitored*: the active chain, or the original when ``monitor_original`` is set.
Switching crossfades over one block.

Loudness matching (``loudness_match``) measures each path's recent gated
loudness (last :data:`MATCH_WINDOW_S` seconds, BS.1770) and turns the
monitored path down to the quietest path, so comparisons aren't biased by
"louder sounds better". The gain only affects what you hear: the output
meters show the unmatched signal and exports never include it.

If no output device is available (CI, headless containers) a :class:`NullOutput`
consumes audio in real time instead, so the engine and meters still run.
"""

from __future__ import annotations

import math
import threading
import time
from typing import Any

import numpy as np

from mastermind.dsp.chain import MasteringChain
from mastermind.dsp.metering.bank import MeterBank
from mastermind.dsp.metering.loudness import LoudnessMeter
from mastermind.dsp.primitives.delay import DelayLine
from mastermind.engine.commands import Call, Command, CommandQueue
from mastermind.engine.ringbuffer import AudioRingBuffer, SnapshotQueue

MATCH_WINDOW_S = 6.0
#: Maximum slew of the loudness-matching gain (dB per second).
MATCH_SLEW_DB_S = 12.0
ORIGINAL = "orig"


class NullOutput:
    """Stand-in for a sounddevice stream that pulls audio at the real-time rate."""

    def __init__(self, sample_rate: float, block: int, callback):
        self._sr = sample_rate
        self._block = block
        self._cb = callback
        self._run = False
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        self._run = True
        self._thread = threading.Thread(target=self._loop, name="null-output", daemon=True)
        self._thread.start()

    def _loop(self) -> None:
        out = np.zeros((self._block, 2), dtype=np.float32)
        period = self._block / self._sr
        nxt = time.perf_counter()
        while self._run:
            self._cb(out, self._block, None, None)
            nxt += period
            time.sleep(max(0.0, nxt - time.perf_counter()))

    def stop(self) -> None:
        self._run = False
        if self._thread:
            self._thread.join(timeout=1.0)

    def close(self) -> None:
        self.stop()


def open_output(sample_rate: float, block: int, callback, prefer_device: bool = True):
    """Open a sounddevice OutputStream, falling back to :class:`NullOutput`."""
    if prefer_device:
        try:
            import sounddevice as sd

            sd.check_output_settings(samplerate=sample_rate, channels=2, dtype="float32")
            return sd.OutputStream(
                samplerate=sample_rate,
                blocksize=block,
                channels=2,
                dtype="float32",
                latency="high",
                callback=callback,
            ), True
        except Exception:  # no PortAudio, no device, unsupported rate...
            pass
    return NullOutput(sample_rate, block, callback), False


class PlaybackEngine:
    """Plays a source through mastering chain(s) with live parameter changes and A/B."""

    def __init__(
        self,
        chain: MasteringChain | None = None,
        block_size: int = 1024,
        prebuffer_blocks: int = 4,
        use_device: bool = True,
    ):
        self.chains: dict[str, MasteringChain] = {"A": chain or MasteringChain.default()}
        self.active = "A"
        self.monitor_original = False
        self.loudness_match = True
        self.block_size = block_size
        self.commands = CommandQueue()
        self.meters = SnapshotQueue()
        self._ring = AudioRingBuffer(block_size * (prebuffer_blocks + 1))
        self._use_device = use_device
        self._source: np.ndarray | None = None
        self._sr = 0.0
        self._pos = 0  # next source frame to process
        self._playing = False
        self._seek_to: int | None = None
        self._run = False
        self._worker: threading.Thread | None = None
        self._stream = None
        self.has_device = False
        self.loop = False
        self.meters_in: MeterBank | None = None
        self.meters_out: MeterBank | None = None
        self._reset_meters = False
        self._latency = 0
        self._delays: dict[str, DelayLine] = {}
        self._path_meters: dict[str, LoudnessMeter] = {}
        self._gain_db = 0.0
        self._last_sel: str | None = None
        self.path_loudness: dict[str, float] = {}
        self.last_error: str | None = None

    # ---- chains / A-B (callable from the GUI thread) --------------------
    @property
    def chain(self) -> MasteringChain:
        """The chain currently being edited and (unless monitoring the original) heard."""
        return self.chains[self.active]

    @property
    def ab_enabled(self) -> bool:
        return "B" in self.chains

    def _prepared(self, chain: MasteringChain) -> MasteringChain:
        if self._sr > 0:
            chain.prepare(self._sr, self.block_size)
        return chain

    def replace_chain(self, chain: MasteringChain, key: str | None = None) -> None:
        """Swap in a new chain (e.g. a preset) for ``key`` (default: active)."""
        self.chains[key or self.active] = self._prepared(chain)

    def enable_b(self) -> None:
        """Create chain B as a copy of A (no-op if it exists)."""
        if not self.ab_enabled:
            self.chains["B"] = self._prepared(self.snapshot_chain("A"))

    def disable_b(self) -> None:
        self.active = "A"
        self.chains.pop("B", None)

    def copy_chain(self, src: str, dst: str) -> None:
        self.chains[dst] = self._prepared(self.snapshot_chain(src))

    def set_active(self, key: str) -> None:
        if key not in self.chains:
            raise KeyError(key)
        self.active = key

    def ab_state(self) -> dict[str, Any]:
        return {
            "active": self.active,
            "monitor": ORIGINAL if self.monitor_original else self.active,
            "paths": [*self.chains, ORIGINAL],
            "loudness": dict(self.path_loudness),
            "match_gain_db": self._gain_db,
            "loudness_match": self.loudness_match,
        }

    # ---- source / transport (callable from any thread) ----------------
    def load(self, audio: np.ndarray, sample_rate: float) -> None:
        self.stop_engine()
        self._source = np.asarray(audio, dtype=np.float64)
        self._sr = float(sample_rate)
        self._pos = 0
        for c in self.chains.values():
            c.prepare(self._sr, self.block_size)
        self.meters_in = MeterBank(self._sr)
        self.meters_out = MeterBank(self._sr)
        self._delays.clear()
        self._path_meters.clear()
        self._gain_db = 0.0
        self.start_engine()

    @property
    def sample_rate(self) -> float:
        return self._sr

    @property
    def duration_samples(self) -> int:
        return 0 if self._source is None else self._source.shape[-1]

    @property
    def playing(self) -> bool:
        return self._playing

    def play(self) -> None:
        self._playing = self._source is not None

    def pause(self) -> None:
        self._playing = False

    def seek(self, frame: int) -> None:
        self._seek_to = max(0, min(int(frame), self.duration_samples))

    @property
    def position(self) -> int:
        """Approximate source frame currently audible."""
        buffered = self._ring.available_read()
        return max(0, self._pos - buffered - self._latency)

    def reset_meters(self) -> None:
        """Clear integrated loudness, LRA, peak holds etc. (applied on the engine thread)."""
        self._reset_meters = True

    def post(self, cmd: Command) -> None:
        """Apply an edit to the active chain: queued if the engine runs, else immediate."""
        key = self.active
        if self._run:
            self.commands.post(cmd, key)
        else:
            cmd.apply(self.chains[key])

    def snapshot_chain(self, key: str | None = None, timeout: float = 1.0) -> MasteringChain:
        """An independent copy of a chain (default: active) including all edits posted so far.

        The copy is taken on the engine thread (after draining pending commands)
        so it never observes a half-applied edit.
        """
        key = key or self.active
        if not self._run:
            return self.chains[key].copy()
        box: list[MasteringChain] = []
        done = threading.Event()

        def take(chain: MasteringChain) -> None:
            box.append(chain.copy())
            done.set()

        self.commands.post(Call(take), key)
        if not done.wait(timeout):
            raise TimeoutError("engine thread did not respond")
        return box[0]

    # ---- engine thread --------------------------------------------------
    def start_engine(self) -> None:
        if self._run or self._source is None:
            return
        self._run = True
        self._stream, self.has_device = open_output(self._sr, self.block_size, self._callback, self._use_device)
        self._worker = threading.Thread(target=self._work, name="mastermind-engine", daemon=True)
        self._worker.start()
        self._stream.start()

    def stop_engine(self) -> None:
        self._run = False
        if self._worker:
            self._worker.join(timeout=2.0)
            self._worker = None
        if self._stream is not None:
            self._stream.stop()
            self._stream.close()
            self._stream = None
        self._playing = False

    def _next_source_block(self) -> np.ndarray:
        n = self.block_size
        src = self._source
        if not self._playing or src is None:
            return np.zeros((2, n))
        block = src[:, self._pos : self._pos + n]
        self._pos += block.shape[-1]
        if block.shape[-1] < n:
            if self.loop:
                self._pos = 0
            else:
                self._playing = False
            block = np.concatenate([block, np.zeros((2, n - block.shape[-1]))], axis=-1)
        return block

    def _apply(self, target: str | None, cmd: Command) -> None:
        chain = self.chains.get(target or self.active)
        if chain is not None:  # B may have been removed since the edit was posted
            cmd.apply(chain)

    def _aligned(self, key: str, x: np.ndarray, delay: int) -> np.ndarray:
        d = self._delays.get(key)
        if d is None or d.delay != delay:
            d = self._delays[key] = DelayLine(delay)
        return d.process(x)

    def _match_gain(self, sel: str, n: int) -> np.ndarray | float:
        """Per-sample monitoring gain (linear) for path ``sel``."""
        target = 0.0
        if self.loudness_match:
            loud = {k: v for k, v in self.path_loudness.items() if math.isfinite(v)}
            if sel in loud and len(loud) > 1:
                target = min(loud.values()) - loud[sel]
        start = self._gain_db
        step = MATCH_SLEW_DB_S * n / self._sr
        self._gain_db = start + float(np.clip(target - start, -step, step))
        if self._gain_db == start:
            return 10.0 ** (start / 20.0)
        return 10.0 ** (np.linspace(start, self._gain_db, n) / 20.0)

    def _process(self, x: np.ndarray, playing: bool) -> tuple[np.ndarray, np.ndarray]:
        """Run every path; return (monitored output with matching gain, unmatched output)."""
        # Snapshot the chain set and monitor choice once per block: the GUI thread
        # may add/remove B or switch paths at any time.
        paths = list(self.chains.items())
        active = self.active if self.active in dict(paths) else paths[0][0]
        sel = ORIGINAL if self.monitor_original else active
        outs = {k: c.process(x) for k, c in paths}
        lat = {k: c.latency_samples for k, c in paths}
        self._latency = max(lat.values())
        aligned = {k: self._aligned(k, outs[k], self._latency - lat[k]) for k in outs}
        aligned[ORIGINAL] = self._aligned(ORIGINAL, x, self._latency)
        for k in list(self._delays):
            if k not in aligned:
                del self._delays[k]

        if playing:  # path loudness is shown in the A/B readout even when not matching
            for k, a in aligned.items():
                m = self._path_meters.get(k)
                if m is None:
                    m = self._path_meters[k] = LoudnessMeter(self._sr, true_peak=False)
                m.process(a)
            self.path_loudness = {k: self._path_meters[k].recent_integrated(MATCH_WINDOW_S) for k in aligned}
        n = x.shape[-1]
        prev_gain = 10.0 ** (self._gain_db / 20.0)
        g = self._match_gain(sel, n)
        y = aligned[sel]
        heard = y * g
        last = self._last_sel
        if last is not None and last != sel and last in aligned:
            fade = np.linspace(0.0, 1.0, n)
            heard = aligned[last] * prev_gain * (1.0 - fade) + heard * fade
        self._last_sel = sel
        return heard, y

    def _work(self) -> None:
        while self._run:
            # Commands are drained every iteration (not only when audio is due) so
            # snapshot requests are answered promptly even with a full buffer.
            self.commands.drain(self._apply)
            if self._seek_to is not None:
                self._pos, self._seek_to = self._seek_to, None
                self._ring.request_flush()
            if self._ring.available_write() < self.block_size:
                time.sleep(self.block_size / self._sr / 4)
                continue
            if self._reset_meters:
                self._reset_meters = False
                self.meters_in.reset()
                self.meters_out.reset()
            was_playing = self._playing
            x = self._next_source_block()
            try:
                heard, y = self._process(x, was_playing)
            except Exception as e:  # never let one bad block kill playback
                self.last_error = repr(e)
                heard = y = np.zeros_like(x)
            self._ring.write(heard.astype(np.float32))
            # Meters only run while audio is playing, so pauses don't count as silence.
            if was_playing:
                self.meters_in.process(x)
                self.meters_out.process(y)
                self.meters.push(
                    {
                        "t": time.monotonic(),
                        "pos": self._pos,
                        "in": self.meters_in.snapshot(),
                        "out": self.meters_out.snapshot(),
                        "gr": self.chain.meter_taps(),
                        "ab": self.ab_state(),
                    }
                )

    def _callback(self, outdata, frames, time_info, status) -> None:  # PortAudio thread
        tmp = np.empty((2, frames), dtype=np.float32)
        self._ring.read_into(tmp)
        outdata[:] = tmp.T
