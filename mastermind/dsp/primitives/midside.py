"""Mid/side conversion.

Uses the energy-asymmetric but exactly invertible convention
``M = (L + R) / 2``, ``S = (L - R) / 2``  <=>  ``L = M + S``, ``R = M - S``.
A mono signal therefore has ``M = L`` and ``S = 0``.
"""

from __future__ import annotations

import numpy as np


def lr_to_ms(x: np.ndarray) -> np.ndarray:
    return np.stack([(x[0] + x[1]) * 0.5, (x[0] - x[1]) * 0.5])


def ms_to_lr(x: np.ndarray) -> np.ndarray:
    return np.stack([x[0] + x[1], x[0] - x[1]])
