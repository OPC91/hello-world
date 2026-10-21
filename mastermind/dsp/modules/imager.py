"""Multiband stereo imager with a mono-maker.

Works in the M/S domain. Only the side channel is split into bands (LR4,
allpass-compensated); each band's side signal is scaled by its width
(0 % = mono, 100 % = unchanged, 200 % = doubled side). The mid channel is passed
through the same allpass cascade as the splitter so M and S stay phase-aligned.

Mono-maker: side is high-passed with an LR4 filter at ``mono_freq`` and mid
gets the matching LR4 allpass. Below ``mono_freq`` the output is mono; above it
the signal is unchanged apart from the allpass phase.

JUCE mapping: ``juce::dsp::LinkwitzRileyFilter`` (lowpass/highpass/allpass) on
the side/mid channels; widths via ``juce::dsp::Gain`` with smoothing.
"""

from __future__ import annotations

import numpy as np

from mastermind.dsp.base import ParamSpec, Processor, registry
from mastermind.dsp.modules.multiband_comp import BAND_NAMES, crossover_specs, ordered_crossovers
from mastermind.dsp.primitives.crossover import LR4Allpass, LR4Split, MultiBandSplitter
from mastermind.dsp.primitives.midside import lr_to_ms, ms_to_lr


@registry.register
class StereoImager(Processor):
    type_id = "imager"
    display_name = "Stereo Imager"

    @classmethod
    def param_specs(cls) -> list[ParamSpec]:
        specs = crossover_specs()
        specs += [
            ParamSpec(f"width_b{i}", f"Width {name}", 100.0, 0.0, 200.0, "%", smooth=True, group="Width")
            for i, name in enumerate(BAND_NAMES, start=1)
        ]
        specs += [
            ParamSpec("mono_maker", "Mono-maker", False, kind="bool", group="Mono-maker"),
            ParamSpec("mono_freq", "Mono Below", 100.0, 20.0, 400.0, "Hz", log=True, group="Mono-maker"),
        ]
        return specs

    def _configure(self) -> None:
        freqs = ordered_crossovers(self.params)
        self._split.set_frequencies(freqs)
        for ap, f in zip(self._mid_ap, freqs, strict=True):
            ap.set_frequency(f, self.sample_rate)
        fm = self.params.get("mono_freq")
        self._mono_split.set_frequency(fm, self.sample_rate)
        self._mono_ap.set_frequency(fm, self.sample_rate)

    def _prepare(self) -> None:
        sr = self.sample_rate
        freqs = ordered_crossovers(self.params)
        self._split = MultiBandSplitter(freqs, sr, channels=1)
        self._mid_ap = [LR4Allpass(f, sr, channels=1) for f in freqs]
        fm = self.params.get("mono_freq")
        self._mono_split = LR4Split(fm, sr, channels=1)
        self._mono_ap = LR4Allpass(fm, sr, channels=1)

    def process(self, block: np.ndarray) -> np.ndarray:
        if self.params.consume_changes() & {"xover1", "xover2", "xover3", "mono_freq"}:
            self._configure()
        n = block.shape[-1]
        ms = lr_to_ms(block)
        mid, side = ms[0:1], ms[1:2]
        for ap in self._mid_ap:
            mid = ap.process(mid)
        bands = self._split.process(side)
        side = np.zeros_like(side)
        for i, b in enumerate(bands, start=1):
            side += b * (np.asarray(self.params.ramp(f"width_b{i}", n)) * 0.01)
        # Run the mono-maker filters every block so their state stays warm.
        mid_mm = self._mono_ap.process(mid)
        _, side_mm = self._mono_split.process(side)
        if self.params.get("mono_maker"):
            mid, side = mid_mm, side_mm
        return ms_to_lr(np.vstack([mid, side]))
