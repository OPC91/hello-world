#pragma once

#include "WaveformOverview.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>

namespace benchmark
{
    /** Loudness facts about a reference, measured once when it is loaded. */
    struct ReferenceInfo
    {
        float integratedStereo = -std::numeric_limits<float>::infinity(); // as played on a stereo bus
        float integratedMono = -std::numeric_limits<float>::infinity();   // as played on a mono bus
        float loudnessRange = 0.0f;
        float samplePeak = 0.0f;
        double lengthSeconds = 0.0;
        double sourceSampleRate = 0.0;
        int sourceChannels = 0;

        float integratedFor (int outputChannels) const noexcept
        {
            return outputChannels == 1 ? integratedMono : integratedStereo;
        }
    };

    /**
        A fully decoded reference resampled to the host rate. Immutable once
        published to the audio thread (see RealtimeHandoff).
    */
    struct ReferenceAudio
    {
        juce::AudioBuffer<float> samples; // 1 or 2 channels at sampleRate
        double sampleRate = 0.0;
        ReferenceInfo info;
        std::shared_ptr<const WaveformOverview> overview;

        size_t getMemoryBytes() const noexcept
        {
            return (size_t) samples.getNumChannels() * (size_t) samples.getNumSamples() * sizeof (float);
        }
    };
}
