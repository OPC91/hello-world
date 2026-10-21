"""Real-time preview engine.

Threads::

    GUI thread --commands--> [CommandQueue] --+
                                              v
    engine thread:  source -> chain.process -> AudioRingBuffer -> PortAudio callback
                                    |
                                    +--> SnapshotQueue (meters) --> GUI timer (~30 fps)

Running the DSP on a dedicated engine thread rather than inside the PortAudio
callback keeps the callback trivial (a memcpy), so GIL contention from the GUI
cannot cause dropouts; the cost is ``prebuffer`` worth of added control
latency. The chain object and the code path are identical to offline
rendering.

If no output device is available (CI, headless containers) a :class:`NullOutput`
consumes audio in real time instead, so the engine and meters still run.
"""

from __future__ import annotations

import threading
import time

import numpy as np

from mastermind.dsp.chain import MasteringChain
from mastermind.dsp.metering.bank import MeterBank
from mastermind.engine.commands import Call, Command, CommandQueue
from mastermind.engine.ringbuffer import AudioRingBuffer, SnapshotQueue


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
    """Plays a source through a :class:`MasteringChain` with live parameter changes."""

    def __init__(
        self,
        chain: MasteringChain | None = None,
        block_size: int = 1024,
        prebuffer_blocks: int = 4,
        use_device: bool = True,
    ):
        self.chain = chain or MasteringChain.default()
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

    # ---- source / transport (callable from any thread) ----------------
    def load(self, audio: np.ndarray, sample_rate: float) -> None:
        self.stop_engine()
        self._source = np.asarray(audio, dtype=np.float64)
        self._sr = float(sample_rate)
        self._pos = 0
        self.chain.prepare(self._sr, self.block_size)
        self.meters_in = MeterBank(self._sr)
        self.meters_out = MeterBank(self._sr)
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
        return max(0, self._pos - buffered - self.chain.latency_samples)

    def reset_meters(self) -> None:
        """Clear integrated loudness, LRA, peak holds etc. (applied on the engine thread)."""
        self._reset_meters = True

    def post(self, cmd: Command) -> None:
        """Apply a chain edit: queued for the engine thread if it runs, else immediately."""
        if self._run:
            self.commands.post(cmd)
        else:
            cmd.apply(self.chain)

    def snapshot_chain(self, timeout: float = 1.0) -> MasteringChain:
        """An independent copy of the chain including all edits posted so far.

        The copy is taken on the engine thread (after draining pending commands)
        so it never observes a half-applied edit.
        """
        if not self._run:
            return self.chain.copy()
        box: list[MasteringChain] = []
        done = threading.Event()

        def take(chain: MasteringChain) -> None:
            box.append(chain.copy())
            done.set()

        self.commands.post(Call(take))
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

    def _work(self) -> None:
        while self._run:
            # Commands are drained every iteration (not only when audio is due) so
            # snapshot requests are answered promptly even with a full buffer.
            self.commands.drain_into(self.chain)
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
            y = self.chain.process(x)
            self._ring.write(y.astype(np.float32))
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
                    }
                )

    def _callback(self, outdata, frames, time_info, status) -> None:  # PortAudio thread
        tmp = np.empty((2, frames), dtype=np.float32)
        self._ring.read_into(tmp)
        outdata[:] = tmp.T
