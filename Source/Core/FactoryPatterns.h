#pragma once

#include <juce_core/juce_core.h>

#include "Pattern.h"

namespace bf::FactoryPatterns
{
    struct Entry
    {
        juce::String name;
        Pattern pattern;
    };

    /** The starter patterns, in slot order. */
    const std::vector<Entry>& all();

    /** A bank with the factory patterns in the first slots and the rest empty. */
    PatternBank makeBank();

    /** Step notation used to author the patterns:
        '.' rest, '1'..'9' a hit with velocity ~14 * digit, 'a'..'i' the same with a 2x ratchet,
        'A'..'I' with a 3x ratchet. Spaces are ignored. */
    void writeTrack (Pattern&, int track, const char* steps, float nudge = 0.0f);
}
