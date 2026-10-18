#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace benchmark
{
    /**
        Peak and RMS per bucket of samples (all channels combined), computed once
        on load for drawing the waveform at any zoom level.
    */
    struct WaveformOverview
    {
        static constexpr int samplesPerBucket = 256;

        double sampleRate = 0.0;
        int64_t numSamples = 0;
        std::vector<float> peak; // max |x| in the bucket
        std::vector<float> rms;

        int getNumBuckets() const noexcept { return (int) peak.size(); }
        double getLengthSeconds() const noexcept { return sampleRate > 0.0 ? (double) numSamples / sampleRate : 0.0; }

        static WaveformOverview compute (const juce::AudioBuffer<float>& samples, double sampleRate)
        {
            WaveformOverview o;
            o.sampleRate = sampleRate;
            o.numSamples = samples.getNumSamples();

            const auto numBuckets = (int) ((o.numSamples + samplesPerBucket - 1) / samplesPerBucket);
            o.peak.assign ((size_t) numBuckets, 0.0f);
            o.rms.assign ((size_t) numBuckets, 0.0f);

            for (int b = 0; b < numBuckets; ++b)
            {
                const auto start = b * samplesPerBucket;
                const auto n = std::min (samplesPerBucket, samples.getNumSamples() - start);
                float peak = 0.0f;
                double sumSquares = 0.0;

                for (int ch = 0; ch < samples.getNumChannels(); ++ch)
                {
                    const auto* x = samples.getReadPointer (ch, start);
                    for (int i = 0; i < n; ++i)
                    {
                        peak = std::max (peak, std::abs (x[i]));
                        sumSquares += (double) x[i] * x[i];
                    }
                }

                o.peak[(size_t) b] = peak;
                o.rms[(size_t) b] = (float) std::sqrt (sumSquares / (double) (n * std::max (1, samples.getNumChannels())));
            }

            return o;
        }
    };
}
