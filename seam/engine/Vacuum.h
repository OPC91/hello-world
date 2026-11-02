#pragma once

#include "PhraseTracker.h"

#include <vector>

namespace seam::engine
{
    /** The pre-drop vacuum: a silent gap of 1/16, 1/8 or 1/4 that ends exactly on the seam's downbeat, with an
        optional tape-stop that slows the final beat before the gap to a halt.

        Works on the finished output (drums, fill and reverb), so the drop lands in true silence. Recomputed from the
        position every sample, so loops and seeks are followed. Buffers are sized in prepare(). */
    class Vacuum
    {
    public:
        void prepare (double sampleRate);
        void reset() noexcept;

        void process (float* left, float* right, int n, const double* ppq, const float* effective, const PhrasePosition&,
                      double gapQuarters, bool tapeStop) noexcept;

        /** Mid-gap, mid-tape-stop or fading back in. */
        bool isActive() const noexcept { return inTape || gain < 1.0f || fadeInLeft > 0; }

        /** Gap start and tape-stop start for the phrase containing `ppq` (exposed for tests). */
        static double gapStart (double ppq, const PhrasePosition&, double gapQuarters) noexcept;

    private:
        double sampleRate = 44100.0;
        std::vector<float> ringL, ringR;
        size_t writeIndex = 0;
        double readPosition = 0.0;
        bool inTape = false;
        float gain = 1.0f;
        int fadeOutSamples = 64, fadeInSamples = 16, fadeInLeft = 0;
    };
}
