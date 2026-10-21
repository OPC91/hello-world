"""Versioned preset format.

A preset = chain settings (exactly :meth:`MasteringChain.to_dict`) + a *target
profile* describing the sound it aims for. The target profile is what
reference matching (Phase 4) and diagnostics (Phase 5) compare against.

Versioning: ``format_version`` is bumped on breaking changes. :func:`migrate`
upgrades older files step by step; files newer than this build are rejected.
"""

from __future__ import annotations

import re
from typing import Any

import numpy as np
from pydantic import BaseModel, ConfigDict, Field, field_validator

from mastermind.dsp.chain import MasteringChain
from mastermind.dsp.metering.spectrum import third_octave_centers

PRESET_FORMAT_VERSION = 1

#: Frequencies of :attr:`TargetProfile.spectrum` (31 base-2 third-octave bands, 20 Hz-20 kHz).
SPECTRUM_FREQS: list[float] = [round(float(f), 2) for f in third_octave_centers()]

#: Spectrum values are dB relative to their own power mean between these frequencies,
#: so the curve describes tonal *shape*; absolute level comes from ``lufs``.
SPECTRUM_NORM_RANGE = (100.0, 10000.0)

CATEGORIES = ("Electronic", "Hip-Hop", "Pop", "Rock", "Metal", "Other", "User")


def normalise_spectrum(freqs: np.ndarray, db: np.ndarray) -> np.ndarray:
    """Shift a dB curve so its power mean over :data:`SPECTRUM_NORM_RANGE` is 0 dB."""
    lo, hi = SPECTRUM_NORM_RANGE
    sel = (freqs >= lo) & (freqs <= hi)
    ref = 10 * np.log10(np.mean(10 ** (db[sel] / 10)))
    return db - ref


class TargetProfile(BaseModel):
    model_config = ConfigDict(extra="ignore")

    lufs: float = Field(-14.0, ge=-40, le=0, description="Integrated loudness target (LUFS)")
    true_peak_ceiling: float = Field(-1.0, ge=-6, le=0, description="dBTP")
    crest_db: float = Field(10.0, ge=0, le=40, description="Whole-file sample-peak minus RMS (dB)")
    lra_lu: float | None = Field(None, ge=0, le=40, description="Typical loudness range (LU)")
    spectrum: list[float] = Field(default_factory=lambda: [0.0] * len(SPECTRUM_FREQS))
    band_width: list[float] = Field(default_factory=lambda: [100.0] * 4, description="Stereo width % per band")
    mono_freq: float = Field(0.0, ge=0, le=400, description="Mono below this frequency (0 = off)")

    @field_validator("spectrum")
    @classmethod
    def _spectrum_len(cls, v: list[float]) -> list[float]:
        if len(v) != len(SPECTRUM_FREQS):
            raise ValueError(f"spectrum must have {len(SPECTRUM_FREQS)} third-octave values")
        return v

    @field_validator("band_width")
    @classmethod
    def _width_len(cls, v: list[float]) -> list[float]:
        if len(v) != 4 or any(not 0 <= w <= 200 for w in v):
            raise ValueError("band_width needs 4 values in 0..200 %")
        return v

    def spectrum_array(self) -> tuple[np.ndarray, np.ndarray]:
        return np.array(SPECTRUM_FREQS), np.array(self.spectrum)


class Preset(BaseModel):
    model_config = ConfigDict(extra="ignore")

    format_version: int = PRESET_FORMAT_VERSION
    id: str
    name: str
    category: str = "User"
    description: str = ""
    notes: str = ""
    tags: list[str] = Field(default_factory=list)
    author: str = "user"
    factory: bool = False
    chain: dict[str, Any]
    target: TargetProfile = Field(default_factory=TargetProfile)

    @field_validator("id")
    @classmethod
    def _id_ok(cls, v: str) -> str:
        if not re.fullmatch(r"[a-z0-9][a-z0-9_\-/]*", v):
            raise ValueError(f"invalid preset id {v!r}")
        return v

    @field_validator("chain")
    @classmethod
    def _chain_ok(cls, v: dict[str, Any]) -> dict[str, Any]:
        MasteringChain.from_dict(v)  # raises on unknown modules / newer chain format
        return v

    def build_chain(self) -> MasteringChain:
        return MasteringChain.from_dict(self.chain)

    def to_json(self) -> str:
        return self.model_dump_json(indent=2)


def migrate(data: dict[str, Any]) -> dict[str, Any]:
    """Upgrade a preset dict to :data:`PRESET_FORMAT_VERSION`."""
    version = int(data.get("format_version", 1))
    if version > PRESET_FORMAT_VERSION:
        raise ValueError(f"preset format v{version} is newer than supported v{PRESET_FORMAT_VERSION}")
    # Future: `if version == 1: data = _v1_to_v2(data); version = 2` ...
    data["format_version"] = PRESET_FORMAT_VERSION
    return data


def load_preset(data: dict[str, Any] | str) -> Preset:
    import json

    if isinstance(data, str):
        data = json.loads(data)
    return Preset.model_validate(migrate(dict(data)))


def slugify(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-") or "preset"
