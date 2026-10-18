#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>

namespace benchmark
{
    /**
        Offline, high-quality sample rate converter for loading references.

        Polyphase Kaiser-windowed sinc. For integer sample rates the phase set is
        exact (L/M rational conversion); otherwise 4096 phases are used. When
        downsampling, the cutoff tracks the target Nyquist frequency so there is
        no aliasing. Passband is flat to about 20 kHz at 44.1 kHz output.

        Not real-time safe: allocates. Run it on a background thread.
    */
    class Resampler
    {
    public:
        /** Return false from the callback to cancel; progress is 0..1. */
        using ProgressCallback = std::function<bool (float progress)>;

        static int64_t getOutputLength (int64_t inputLength, double sourceRate, double targetRate);

        /** Returns an empty buffer if cancelled. Equal rates return a copy. */
        static juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input,
                                                 double sourceRate, double targetRate,
                                                 const ProgressCallback& progress = {});

        static constexpr int zeroCrossings = 48; // half-width of the filter, in cutoff periods
        static constexpr double kaiserBeta = 8.0;
        static constexpr double passbandFraction = 0.95; // -6 dB point relative to the lower Nyquist
    };
}
