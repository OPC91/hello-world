#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Pattern.h"

namespace bf::ui
{

namespace Colours
{
    inline const juce::Colour background   { 0xff101216 };
    inline const juce::Colour panel        { 0xff181b21 };
    inline const juce::Colour panelRaised  { 0xff20242c };
    inline const juce::Colour outline      { 0xff2c313b };
    inline const juce::Colour cellOff      { 0xff262b34 };
    inline const juce::Colour cellOffBeat  { 0xff2e343f };
    inline const juce::Colour text         { 0xffdde2ea };
    inline const juce::Colour textDim      { 0xff7f8796 };
    inline const juce::Colour accent       { 0xffff7a1a }; // forge orange
    inline const juce::Colour accentAlt    { 0xff35c6d6 };
    inline const juce::Colour warning      { 0xffffc23d };
    inline const juce::Colour mute         { 0xffe0563c };
    inline const juce::Colour solo         { 0xffe8c547 };
    inline const juce::Colour playhead     { 0x40ffffff };

    /** One hue per track, so rows are easy to tell apart. */
    juce::Colour track (int index);

    /** Step colour for a velocity: darker for ghosts, bright for full hits. */
    juce::Colour forVelocity (int track, int velocity);
}

juce::Font font (float height, bool bold = false);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
    juce::Font getPopupMenuFont() override { return font (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (14.0f, h * 0.55f), true); }
    juce::Font getLabelFont (juce::Label&) override { return font (13.0f); }
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
};

/** A rotary knob with its name underneath and a value read-out while dragging. */
class Knob final : public juce::Component
{
public:
    explicit Knob (const juce::String& name, juce::Colour colour = Colours::accent);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::String name;
};

} // namespace bf::ui
