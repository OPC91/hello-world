#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace benchmark::ui
{
    // Titled panel used for areas whose content arrives in a later phase.
    class PlaceholderPanel final : public juce::Component
    {
    public:
        PlaceholderPanel (juce::String title, juce::String hint);
        void paint (juce::Graphics&) override;

    private:
        juce::String title, hint;
    };
}
