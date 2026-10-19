#include "BuiltInKits.h"
#include "DrumSynth.h"

namespace bf::BuiltInKits
{

juce::StringArray kitNames() { return { "Clean" }; }

juce::StringArray soundsFor (const juce::String& kit, Role)
{
    return { kit + "/Kick A" };
}

juce::AudioBuffer<float> render (const juce::String& id, double sampleRate)
{
    if (id == "Clean/Kick A")
        return DrumSynth::renderKick (sampleRate, {});

    return {};
}

SampleRef defaultFor (Role role, const juce::String& kit)
{
    return SampleRef::builtIn (soundsFor (kit, role)[0]);
}

} // namespace bf::BuiltInKits
