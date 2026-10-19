#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "SampleRef.h"
#include "Tracks.h"

namespace bf::BuiltInKits
{
    /** The procedurally synthesized kits. */
    juce::StringArray kitNames();

    /** Built-in sound ids ("<Kit>/<Sound>") usable for a role in a kit, best match first. */
    juce::StringArray soundsFor (const juce::String& kit, Role role);

    /** Renders a built-in sound at the given rate. Deterministic; empty buffer if unknown. */
    juce::AudioBuffer<float> render (const juce::String& id, double sampleRate);

    /** The default sound for a role in a kit. */
    SampleRef defaultFor (Role role, const juce::String& kit = "Clean");
}
