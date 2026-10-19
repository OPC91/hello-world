#pragma once

#include "Core/Engine.h"

namespace bftest
{
    /** A click: one sample at full scale followed by silence, so onsets are exact. */
    inline bf::SampleData::Ptr makeClick (double sampleRate, int length = 64)
    {
        juce::AudioBuffer<float> b (1, length);
        b.clear();
        b.setSample (0, 0, 1.0f);
        return new bf::SampleData (std::move (b), sampleRate, "click");
    }

    /** Finds sample indices where a channel goes from silent to non-silent. */
    inline std::vector<juce::int64> findOnsets (const std::vector<float>& signal)
    {
        std::vector<juce::int64> onsets;
        bool prevSilent = true;

        for (size_t i = 0; i < signal.size(); ++i)
        {
            const bool silent = std::abs (signal[i]) < 1.0e-6f;
            if (prevSilent && ! silent)
                onsets.push_back ((juce::int64) i);
            prevSilent = silent;
        }

        return onsets;
    }

    /** Engine params with everything neutral: no groove, full velocity response off. */
    inline bf::EngineParams neutralParams()
    {
        bf::EngineParams p;
        p.velocityDepth = 0.0f;
        p.accentAmount = 0.0f;
        p.limiterOn = false;
        p.driveOn = false;

        for (auto& t : p.tracks)
            t.chokeGroup = 0;

        return p;
    }
}
