"""Loudness targets and the colour-status rules used by meter displays."""

from __future__ import annotations

import math
from typing import Literal

#: Integrated-loudness targets in LUFS. ``Custom`` is user-defined.
LOUDNESS_TARGETS: dict[str, float | None] = {
    "Spotify": -14.0,
    "Apple Music": -16.0,
    "YouTube": -14.0,
    "Club": -8.0,
    "CD": -9.0,
    "Custom": None,
}

Status = Literal["ok", "warn", "bad", "none"]


def loudness_status(value: float, target: float, ok_lu: float = 1.0, warn_lu: float = 3.0) -> Status:
    """``ok`` within +-1 LU of target, ``warn`` within +-3 LU, else ``bad``."""
    if not math.isfinite(value):
        return "none"
    d = abs(value - target)
    if d <= ok_lu:
        return "ok"
    if d <= warn_lu:
        return "warn"
    return "bad"


def peak_status(dbtp: float, ceiling: float) -> Status:
    """``bad`` when the true peak exceeds the ceiling."""
    if not math.isfinite(dbtp):
        return "none"
    return "bad" if dbtp > ceiling + 1e-9 else "ok"
