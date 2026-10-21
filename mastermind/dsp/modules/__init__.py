"""User-facing mastering processors.

Importing this package registers every module with
:data:`mastermind.dsp.base.registry` so chains can be (de)serialised by type id.
"""

from mastermind.dsp.modules.dither import Dither
from mastermind.dsp.modules.eq import ParametricEQ
from mastermind.dsp.modules.glue_comp import GlueCompressor
from mastermind.dsp.modules.imager import StereoImager
from mastermind.dsp.modules.input_gain import InputGain
from mastermind.dsp.modules.limiter import TruePeakLimiter
from mastermind.dsp.modules.multiband_comp import MultibandCompressor
from mastermind.dsp.modules.saturation import Saturation

__all__ = [
    "Dither",
    "GlueCompressor",
    "InputGain",
    "MultibandCompressor",
    "ParametricEQ",
    "Saturation",
    "StereoImager",
    "TruePeakLimiter",
]
