#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace seam
{
    /** Parameter IDs. These are saved in sessions and presets: never rename one. */
    namespace ids
    {
        inline constexpr const char* transition = "transition";
        inline constexpr const char* style      = "style";
        inline constexpr const char* complexity = "complexity";
        inline constexpr const char* fillLength = "fillLength";
        inline constexpr const char* fillMode   = "fillMode";
        inline constexpr const char* vacuum     = "vacuum";
        inline constexpr const char* vacuumSize = "vacuumSize";
        inline constexpr const char* tapeStop   = "tapeStop";
        inline constexpr const char* autoMode   = "autoMode";
        inline constexpr const char* autoBars   = "autoBars";
        inline constexpr const char* autoCurve  = "autoCurve";
        inline constexpr const char* phrase     = "phrase";
        inline constexpr const char* amtFilter  = "amtFilter";
        inline constexpr const char* amtReverb  = "amtReverb";
        inline constexpr const char* amtStutter = "amtStutter";
        inline constexpr const char* amtDuck    = "amtDuck";
        inline constexpr const char* amtFill    = "amtFill";
    }

    /** Choice lists (index = saved value). */
    namespace choices
    {
        inline const juce::StringArray style      { "Liquid", "Neuro", "Jungle", "Jump-Up" };
        inline const juce::StringArray fillLength { "1 bar", "2 bars", "4 bars" };
        inline const juce::StringArray fillMode   { "Mix", "Replace" };
        inline const juce::StringArray vacuumSize { "1/16", "1/8", "1/4" };
        inline const juce::StringArray autoBars   { "2 bars", "4 bars", "8 bars", "16 bars" };
        inline const juce::StringArray autoCurve  { "Linear", "Late surge" };
        inline const juce::StringArray phrase     { "8 bars", "16 bars", "32 bars" };

        inline constexpr int fillLengthBars[] { 1, 2, 4 };
        inline constexpr double vacuumQuarters[] { 0.25, 0.5, 1.0 };
        inline constexpr int autoBarsValues[] { 2, 4, 8, 16 };
        inline constexpr int phraseBars[] { 8, 16, 32 };
    }

    /** The Advanced panel's per-effect amounts (0-150% of the preset's design). */
    struct AdvancedAmount { const char* id; const char* name; const char* tooltip; };
    inline constexpr AdvancedAmount advancedAmounts[] {
        { ids::amtFilter,  "Filter",  "Drum-bus high-pass sweep" },
        { ids::amtReverb,  "Reverb",  "Reverb throw on the snares" },
        { ids::amtStutter, "Stutter", "Beat-locked repeat" },
        { ids::amtDuck,    "Duck",    "Beat-synced ducking dip" },
        { ids::amtFill,    "Fill",    "Level of the generated fill" },
    };

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
