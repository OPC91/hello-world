"""Input gain (trim) stage.

JUCE mapping: ``juce::dsp::Gain<float>`` (with ramp duration = 20 ms).
"""

from __future__ import annotations

import numpy as np

from mastermind.dsp.base import ParamSpec, Processor, db_to_gain, registry


@registry.register
class InputGain(Processor):
    type_id = "input_gain"
    display_name = "Input Gain"

    @classmethod
    def param_specs(cls) -> list[ParamSpec]:
        return [
            ParamSpec("gain_db", "Gain", 0.0, -24.0, 24.0, "dB", smooth=True),
            ParamSpec("balance", "Balance", 0.0, -1.0, 1.0, "", smooth=True),
        ]

    def _prepare(self) -> None:
        pass

    def process(self, block: np.ndarray) -> np.ndarray:
        n = block.shape[-1]
        g = db_to_gain(self.params.ramp("gain_db", n))
        bal = self.params.ramp("balance", n)
        # Linear balance law: centre = unity on both sides, full right mutes left.
        gl = g * np.minimum(1.0, 1.0 - bal)
        gr = g * np.minimum(1.0, 1.0 + bal)
        block[0] *= gl
        block[1] *= gr
        return block
