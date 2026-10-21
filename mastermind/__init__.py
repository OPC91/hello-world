"""MasterMind - AI-assisted mastering prototype.

The package is split so that everything under :mod:`mastermind.dsp` is pure
numerical code (numpy / scipy / numba) with no GUI or I/O dependencies. That
layer is what gets ported to C++/JUCE later; everything else (engine, io, gui,
cli) is Python-only glue.
"""

__version__ = "0.1.0"
