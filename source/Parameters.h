#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace benchmark
{
    // Parameter IDs are part of saved sessions and host automation: never rename them.
    namespace ParamIDs
    {
        inline constexpr auto bypass        = "bypass";
        inline constexpr auto listenRef     = "listenReference"; // false = Mix, true = Reference
        inline constexpr auto loudnessMatch = "loudnessMatch";
        inline constexpr auto syncMode      = "syncMode";        // 0 = Free, 1 = Sync to host transport
    }

    // Bump only when a parameter's meaning changes (see juce::ParameterID).
    inline constexpr int parameterVersion = 1;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
