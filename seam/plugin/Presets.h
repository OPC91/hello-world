#pragma once

#include <exnilo_ui/exnilo_ui.h>

namespace seam
{
    /** The parameters a preset stores: everything except TRANSITION (the performance knob) and the phrase length
        (it belongs to the arrangement, not the sound). */
    juce::StringArray presetParameterIds();

    /** 12 factory presets, 3 per style. The first equals the parameter defaults. */
    std::vector<exnilo::Preset> factoryPresets();

    /** One-line description per factory preset (shown as the tagline). */
    juce::String presetTagline (const juce::String& presetName);
}
