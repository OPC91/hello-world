#pragma once

#include <juce_core/juce_core.h>

#include "GrooveEngine.h"

namespace bf::UserLibrary
{
    /** Root folder for user content: <user app data>/BreakForge (override for tests). */
    juce::File getRoot();
    void setRootOverride (const juce::File&);

    juce::File getGroovesFolder();  // <root>/Grooves/*.bfgroove
    juce::File getKitsFolder();     // <root>/Kits/<Kit name>/...

    juce::Array<juce::File> listGrooves();
    bool saveGroove (const juce::String& name, const GrooveTemplate&);
    bool loadGroove (const juce::File&, GrooveTemplate& out);
}
