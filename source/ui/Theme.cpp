#include "Theme.h"

namespace benchmark::ui
{
    Theme::Theme()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
        setColour (juce::Label::textColourId, Colours::textPrimary);
        setColour (juce::TextButton::buttonColourId, Colours::panelRaised);
        setColour (juce::TextButton::textColourOffId, Colours::textPrimary);
        setColour (juce::ToggleButton::textColourId, Colours::textPrimary);
        setColour (juce::ToggleButton::tickColourId, Colours::mix);
    }

    juce::Font Theme::font (float baseSize, bool bold) const
    {
        return juce::Font (juce::FontOptions (baseSize * scale, bold ? juce::Font::bold : juce::Font::plain));
    }

    void Theme::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
    {
        const auto colour = (isMouseOver || isMouseDragging) ? Colours::textSecondary : Colours::textMuted;
        g.setColour (colour);

        const auto size = (float) juce::jmin (w, h);
        for (int i = 1; i <= 3; ++i)
        {
            const auto offset = size * (float) i / 4.0f;
            g.drawLine ((float) w - offset, (float) h, (float) w, (float) h - offset, 1.2f);
        }
    }
}
