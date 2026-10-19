#include "PluginEditor.h"

BreakForgeEditor::BreakForgeEditor (BreakForgeProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setResizable (true, true);
    setResizeLimits (900, 560, 2400, 1600);
    setSize (1200, 720);
}

void BreakForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));
    g.setColour (juce::Colours::white);
    g.setFont (28.0f);
    g.drawFittedText ("BreakForge", getLocalBounds(), juce::Justification::centred, 1);
}
