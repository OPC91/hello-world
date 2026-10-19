#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "GrooveEngine.h"
#include "Pattern.h"

namespace bf::StateIO
{
    // Patterns are stored compactly: one child per non-default step.
    juce::ValueTree patternToTree (const Pattern&);
    Pattern patternFromTree (const juce::ValueTree&);

    juce::ValueTree bankToTree (const PatternBank&);
    PatternBank bankFromTree (const juce::ValueTree&);

    juce::ValueTree grooveToTree (const GrooveTemplate&);
    GrooveTemplate grooveFromTree (const juce::ValueTree&);

    /** Clipboard / file helpers built on the same format. */
    juce::String patternToString (const Pattern&);
    bool patternFromString (const juce::String&, Pattern& out);
}
