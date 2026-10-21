"""8-band parametric EQ with minimum-phase (IIR) and linear-phase (FIR) modes.

Each band has a type (bell, shelves, cuts, notch), frequency, gain, Q and a
target: ``stereo`` (L and R), ``mid`` or ``side``.

Minimum-phase mode
    Cascaded RBJ biquads. Bands are grouped by target: the stereo group is applied
    to L/R, then the signal is converted to M/S and the mid and side groups are
    applied. Zero latency.

Linear-phase mode
    The *magnitude* of the same biquad cascades is sampled and turned into two
    symmetric FIR kernels, one for mid (|H_stereo * H_mid|) and one for side
    (|H_stereo * H_side|), applied in the M/S domain by FFT convolution. Because
    the stereo group is identical on L and R it commutes with the M/S matrix, so
    this is exact. Latency = (n_taps - 1) / 2 (~171 ms at 48 kHz). Kernel updates
    are crossfaded over one block.

JUCE mapping: IIR -> ``juce::dsp::IIR::Filter`` cascade; FIR ->
``juce::dsp::Convolution`` loaded with a kernel designed on a background thread
(``Convolution::loadImpulseResponse`` handles the crossfade internally).
"""

from __future__ import annotations

import numpy as np

from mastermind.dsp.base import ParamSpec, Processor, db_to_gain, registry
from mastermind.dsp.primitives.biquad import IDENTITY_SOS, SOSFilter, design, sos_response
from mastermind.dsp.primitives.fir import FIRConvolver, linear_phase_from_magnitude
from mastermind.dsp.primitives.midside import lr_to_ms, ms_to_lr

N_BANDS = 8
BAND_TYPES = ("bell", "low_shelf", "high_shelf", "low_cut", "high_cut", "notch")
TARGETS = ("stereo", "mid", "side")
#: Linear-phase kernel length at 48 kHz (scaled with sample rate, kept odd).
FIR_TAPS_48K = 16383
_DEFAULT_BANDS = [
    (30.0, "low_cut", 0.7071),
    (80.0, "low_shelf", 0.7071),
    (200.0, "bell", 1.0),
    (500.0, "bell", 1.0),
    (1500.0, "bell", 1.0),
    (4000.0, "bell", 1.0),
    (8000.0, "bell", 1.0),
    (14000.0, "high_shelf", 0.7071),
]


@registry.register
class ParametricEQ(Processor):
    type_id = "eq"
    display_name = "Parametric EQ"

    @classmethod
    def param_specs(cls) -> list[ParamSpec]:
        specs = [
            ParamSpec("mode", "Phase Mode", "minimum", kind="choice", choices=("minimum", "linear"), structural=True),
            ParamSpec("output_db", "Output", 0.0, -24.0, 24.0, "dB", smooth=True),
        ]
        for i, (f, kind, q) in enumerate(_DEFAULT_BANDS, start=1):
            g = f"Band {i}"
            specs += [
                ParamSpec(f"b{i}_on", "On", False, kind="bool", group=g),
                ParamSpec(f"b{i}_type", "Type", kind, kind="choice", choices=BAND_TYPES, group=g),
                ParamSpec(f"b{i}_freq", "Freq", f, 20.0, 20000.0, "Hz", log=True, group=g),
                ParamSpec(f"b{i}_gain", "Gain", 0.0, -18.0, 18.0, "dB", group=g),
                ParamSpec(f"b{i}_q", "Q", q, 0.1, 10.0, "", log=True, group=g),
                ParamSpec(f"b{i}_target", "Target", "stereo", kind="choice", choices=TARGETS, group=g),
            ]
        return specs

    # ---- coefficient computation ----------------------------------
    def band_sos(self, i: int, sample_rate: float | None = None) -> np.ndarray:
        p = self.params
        if not p.get(f"b{i}_on"):
            return IDENTITY_SOS.copy()
        return design(
            p.get(f"b{i}_type"),
            p.get(f"b{i}_freq"),
            sample_rate or self.sample_rate,
            p.get(f"b{i}_q"),
            p.get(f"b{i}_gain"),
        )

    def group_sos(self, target: str, sample_rate: float | None = None) -> np.ndarray:
        rows = [
            self.band_sos(i, sample_rate) if self.params.get(f"b{i}_target") == target else IDENTITY_SOS
            for i in range(1, N_BANDS + 1)
        ]
        return np.vstack(rows)

    def response_db(self, freqs: np.ndarray, sample_rate: float = 48000.0) -> dict[str, np.ndarray]:
        """Magnitude response in dB for the mid and side paths (for UI plots and matching)."""
        h = {t: np.abs(sos_response(self.group_sos(t, sample_rate), freqs, sample_rate)) for t in TARGETS}
        out = db_to_gain(self.params.target("output_db"))
        return {
            "mid": 20 * np.log10(np.maximum(h["stereo"] * h["mid"] * out, 1e-12)),
            "side": 20 * np.log10(np.maximum(h["stereo"] * h["side"] * out, 1e-12)),
        }

    def _n_taps(self) -> int:
        n = int(round(FIR_TAPS_48K * self.sample_rate / 48000.0))
        return n if n % 2 else n + 1

    def _update(self, crossfade: bool) -> None:
        sos = {t: self.group_sos(t) for t in TARGETS}
        self._iir["stereo"].set_sos(sos["stereo"])
        self._iir["mid"].set_sos(sos["mid"])
        self._iir["side"].set_sos(sos["side"])
        self._ms_active = any(
            self.params.get(f"b{i}_on") and self.params.get(f"b{i}_target") != "stereo" for i in range(1, N_BANDS + 1)
        )
        if self.params.get("mode") == "linear":
            sr = self.sample_rate

            def mag(target: str):
                return lambda f: np.abs(sos_response(sos["stereo"], f, sr) * sos_response(sos[target], f, sr))

            k_mid = linear_phase_from_magnitude(mag("mid"), self._n_taps(), sr)
            k_side = linear_phase_from_magnitude(mag("side"), self._n_taps(), sr)
            kernels = np.vstack([k_mid, k_side])
            if self._fir is None:
                self._fir = FIRConvolver(kernels)
            else:
                self._fir.set_kernels(kernels, crossfade=crossfade)

    def _prepare(self) -> None:
        self._iir = {"stereo": SOSFilter(N_BANDS, 2), "mid": SOSFilter(N_BANDS, 1), "side": SOSFilter(N_BANDS, 1)}
        self._fir: FIRConvolver | None = None
        self._update(crossfade=False)

    @property
    def latency_samples(self) -> int:
        if self.params.get("mode") == "linear" and self.is_prepared:
            return (self._n_taps() - 1) // 2
        return 0

    def process(self, block: np.ndarray) -> np.ndarray:
        changed = self.params.consume_changes() - {"output_db"}
        if changed:
            if "mode" in changed:
                self._fir = None
                for f in self._iir.values():
                    f.reset()
            self._update(crossfade=True)
        if self.params.get("mode") == "linear":
            y = ms_to_lr(self._fir.process(lr_to_ms(block)))
        else:
            y = self._iir["stereo"].process(block)
            if self._ms_active:
                ms = lr_to_ms(y)
                ms[0:1] = self._iir["mid"].process(ms[0:1])
                ms[1:2] = self._iir["side"].process(ms[1:2])
                y = ms_to_lr(ms)
        y *= db_to_gain(self.params.ramp("output_db", block.shape[-1]))
        return y
