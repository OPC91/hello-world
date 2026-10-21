"""Pure DSP layer (no Qt, no file or device I/O).

All processors operate on stereo float64 blocks shaped ``(2, n_samples)`` and
are block-size invariant: rendering a file with 64-sample or 4096-sample blocks
yields the same output (to floating-point rounding) as long as parameters are
static. This is what lets the real-time preview and the offline render share a
single code path.

Sub-packages:
    primitives/  filters, crossovers, oversampling, detectors (building blocks)
    modules/     the user-facing mastering processors
    chain.py     the reorderable mastering chain
"""
