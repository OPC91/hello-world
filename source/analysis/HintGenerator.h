#pragma once

#include "AnalysisSnapshot.h"

#include <juce_core/juce_core.h>

namespace benchmark::analysis
{
    /**
        Turns analysis results into short plain-language comparisons, e.g.
        "Mix has +2.5 dB more 200-500 Hz (low-mids) than the reference".
        Pure functions of a snapshot; the first line is a summary.
    */
    struct HintGenerator
    {
        static constexpr double minSecondsForHints = 2.0;
        static constexpr float tonalThresholdDb = 1.5f;
        static constexpr float widthThreshold = 0.08f;   // 8 percentage points of side energy
        static constexpr float plrThresholdDb = 1.5f;
        static constexpr float lraThresholdLU = 2.0f;
        static constexpr float crestThresholdDb = 2.0f;

        /** Why there is nothing to compare yet, or empty when both sources have data. */
        static juce::String getWaitingMessage (const AnalysisSnapshot&, bool live);

        static juce::StringArray tonal (const AnalysisSnapshot&, bool live, int maxDetails = 3);
        static juce::StringArray stereo (const AnalysisSnapshot&, bool live);
        static juce::StringArray dynamics (const AnalysisSnapshot&);

        static juce::String formatDb (float db, bool withSign = true);
    };
}
