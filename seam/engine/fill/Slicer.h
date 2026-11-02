#pragma once

#include "Break.h"

#include <span>
#include <string>

namespace seam::engine
{
    struct SlicerOptions
    {
        double minGapSeconds = 0.045;   ///< two onsets closer than this are one hit
        double sensitivity = 1.0;       ///< higher finds quieter hits
        size_t maxSlices = 96;
        double maxSeconds = 60.0;       ///< only the start of very long files is analysed
    };

    /** Finds the hits in a break and labels them kick, snare, ghost or hat.

        Onsets: spectral flux (log-magnitude STFT, 23 ms Hann window, ~3 ms hop) with an adaptive threshold,
        each refined to the sample by backtracking to the start of the energy rise.
        Labels (attack window of the first 60 ms): kick = mostly below 150 Hz with a low centroid; hat = bright
        centroid and almost no lows; the rest are snare-like, and the quiet ones (under 45% of the loudest
        snare-like hit) are ghosts.

        Runs on a background thread: it allocates freely. */
    struct SliceResult
    {
        std::vector<Slice> slices;
        std::string summary;   ///< one line for the UI
    };

    SliceResult sliceBreak (std::span<const float> mono, double sampleRate, const SlicerOptions& = {});

    /** Builds a ready-to-play kit from stereo audio (right may be empty for mono). */
    BreakKit buildKit (std::vector<float> left, std::vector<float> right, double sampleRate, std::string name,
                       const SlicerOptions& = {});

    /** In-place radix-2 FFT of interleaved re/im pairs; n must be a power of two. Exposed for tests. */
    void fftInPlace (std::vector<float>& re, std::vector<float>& im);
}
