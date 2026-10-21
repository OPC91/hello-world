"""Audio file loading.

soundfile (libsndfile) handles WAV/AIFF/FLAC/OGG and, with libsndfile >= 1.1,
MP3. If soundfile cannot open a file we fall back to librosa (audioread /
ffmpeg backends). Everything is returned as float64 ``(2, n)`` at the file's
native sample rate: mono is duplicated to both channels, and files with more
than two channels keep the first two.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf

SUPPORTED_EXTENSIONS = (".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg")


@dataclass
class AudioData:
    samples: np.ndarray  # (2, n) float64
    sample_rate: int
    path: Path | None = None
    subtype: str = ""

    @property
    def n_samples(self) -> int:
        return self.samples.shape[-1]

    @property
    def duration(self) -> float:
        return self.n_samples / self.sample_rate


def to_stereo(x: np.ndarray) -> np.ndarray:
    """Coerce ``(n,)``, ``(n, ch)``-ordered-as-``(ch, n)`` arrays to ``(2, n)``."""
    x = np.atleast_2d(np.asarray(x, dtype=np.float64))
    if x.shape[0] == 1:
        x = np.repeat(x, 2, axis=0)
    elif x.shape[0] > 2:
        x = x[:2]
    return np.ascontiguousarray(x)


def load_audio(path: str | Path) -> AudioData:
    path = Path(path)
    try:
        data, sr = sf.read(str(path), dtype="float64", always_2d=True)
        subtype = sf.info(str(path)).subtype
        return AudioData(to_stereo(data.T), int(sr), path, subtype)
    except (sf.LibsndfileError, RuntimeError, TypeError):
        import librosa  # heavy import, only on fallback

        y, sr = librosa.load(str(path), sr=None, mono=False)
        return AudioData(to_stereo(y), int(sr), path, "decoded")
