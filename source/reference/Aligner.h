#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <functional>

namespace benchmark
{
    /**
        Finds where a captured excerpt of the mix occurs in a reference, so a
        reference that is a version of the same song can follow the host
        transport in sync.

        Two stages: a coarse search over the whole reference with phase-only
        cross-correlation (GCC-PHAT) at ~4 kHz, which is robust to EQ and mix
        differences between versions, then a sample-accurate refinement at the
        full rate with normalised correlation, which also gives the confidence.

        Background thread only (allocates).
    */
    struct Aligner
    {
        struct Result
        {
            bool found = false;
            int64_t referencePosition = 0; // reference sample where the excerpt starts
            float confidence = 0.0f;       // normalised correlation at the match, 0..1
            juce::String message;
        };

        static constexpr float minimumConfidence = 0.5f;
        static constexpr double coarseRate = 4000.0;

        /** reference: 1-2 channels; excerpt: mono, same sample rate. */
        static Result findExcerpt (const juce::AudioBuffer<float>& reference, const float* excerpt, int excerptLength,
                                   double sampleRate, const std::atomic<bool>* cancel = nullptr);
    };
}
