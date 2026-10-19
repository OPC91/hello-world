#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

class BreakForgeEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BreakForgeEditor (BreakForgeProcessor&);

    void paint (juce::Graphics&) override;

private:
    BreakForgeProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakForgeEditor)
};
