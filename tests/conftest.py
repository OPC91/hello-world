"""Shared fixtures and signal generators for the test suite."""

from __future__ import annotations

import os

import numpy as np
import pytest
from scipy import signal

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

SR = 48000


def sine(freq: float, dur: float, sr: int = SR, amp: float = 1.0, phase: float = 0.0) -> np.ndarray:
    t = np.arange(int(dur * sr)) / sr
    return np.tile(amp * np.sin(2 * np.pi * freq * t + phase), (2, 1))


def noise(dur: float, sr: int = SR, amp: float = 0.3, seed: int = 0) -> np.ndarray:
    return np.random.default_rng(seed).standard_normal((2, int(dur * sr))) * amp


def music_like(dur: float, sr: int = SR, seed: int = 0) -> np.ndarray:
    """Kick + bass + pink-ish noise + a partly decorrelated pad: a crude stand-in for a mix."""
    rng = np.random.default_rng(seed)
    n = int(dur * sr)
    t = np.arange(n) / sr
    beat = (t % 0.5) / 0.5
    kick = np.sin(2 * np.pi * (50 + 80 * np.exp(-beat * 30)) * t) * np.exp(-beat * 8) * 0.6
    bass = 0.25 * np.sin(2 * np.pi * 55 * t) * (0.6 + 0.4 * np.sin(2 * np.pi * 0.5 * t))
    pink = signal.lfilter(
        [0.049922035, -0.095993537, 0.050612699, -0.004408786],
        [1, -2.494956002, 2.017265875, -0.522189400],
        rng.standard_normal((2, n)),
    )
    pad = 0.1 * np.vstack([np.sin(2 * np.pi * 440 * t), np.sin(2 * np.pi * 440 * t + 0.6)])
    hats = (
        signal.sosfilt(signal.butter(4, 7000, "hp", fs=sr, output="sos"), rng.standard_normal(n))
        * (t % 0.25 < 0.03)
        * 0.3
    )
    x = kick + bass + 0.15 * pink + pad + hats
    return np.asarray(x, dtype=np.float64)


def band_limit(x: np.ndarray, sr: int, f: float = 20000.0) -> np.ndarray:
    return signal.sosfilt(signal.butter(8, f, "lp", fs=sr, output="sos"), x, axis=-1)


def db(x: float) -> float:
    return float(20 * np.log10(max(abs(x), 1e-15)))


@pytest.fixture
def sr() -> int:
    return SR


@pytest.fixture(scope="session")
def wav_file(tmp_path_factory) -> str:
    import soundfile as sf

    path = tmp_path_factory.mktemp("audio") / "mix.wav"
    sf.write(str(path), music_like(4.0).T * 0.5, SR, subtype="PCM_24")
    return str(path)
