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
        setColour (juce::PopupMenu::backgroundColourId, Colours::panelRaised);
        setColour (juce::PopupMenu::textColourId, Colours::textPrimary);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::outline);
        setColour (juce::PopupMenu::highlightedTextColourId, Colours::textPrimary);
        setColour (juce::TooltipWindow::backgroundColourId, Colours::panelRaised);
        setColour (juce::TooltipWindow::textColourId, Colours::textPrimary);
        setColour (juce::TooltipWindow::outlineColourId, Colours::outline);
    }

    juce::String formatLufs (float lufs)
    {
        if (! std::isfinite (lufs) || lufs < -70.0f)
            return juce::String::fromUTF8 ("\xe2\x80\x93\xe2\x80\x93");

        return juce::String (lufs, 1).replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92"));
    }

    juce::String formatDuration (double seconds)
    {
        const auto total = juce::jmax (0, (int) std::lround (seconds));
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    void Theme::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
    {
        // Pill switch followed by the label.
        auto bounds = button.getLocalBounds().toFloat();
        const auto h = juce::jmin (bounds.getHeight(), px (16.0f));
        auto track = bounds.removeFromLeft (h * 1.8f).withSizeKeepingCentre (h * 1.8f, h);

        const auto on = button.getToggleState();
        const auto accent = button.findColour (juce::ToggleButton::tickColourId);

        g.setColour (on ? accent.withAlpha (0.35f) : Colours::outline);
        g.fillRoundedRectangle (track, h * 0.5f);

        const auto knobSize = h - px (4.0f);
        const auto knobX = on ? track.getRight() - knobSize - px (2.0f) : track.getX() + px (2.0f);
        g.setColour (on ? accent : (highlighted ? Colours::textSecondary : Colours::textMuted));
        g.fillEllipse (knobX, track.getCentreY() - knobSize * 0.5f, knobSize, knobSize);

        bounds.removeFromLeft (px (6.0f));
        g.setColour (button.isEnabled() ? Colours::textPrimary : Colours::textMuted);
        g.setFont (font (11.0f, true));
        g.drawText (button.getButtonText(), bounds, juce::Justification::centredLeft);
    }

    void Theme::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto corner = juce::jmin (px (5.0f), bounds.getHeight() * 0.5f);

        auto fill = Colours::panelRaised;
        if (down)              fill = Colours::outline;
        else if (highlighted)  fill = fill.brighter (0.06f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (bounds, corner, 1.0f);
    }

    juce::Font Theme::getTextButtonFont (juce::TextButton&, int)
    {
        return font (11.5f, true);
    }

    juce::Font Theme::getPopupMenuFont()
    {
        return font (13.0f);
    }

    void Theme::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        const auto bounds = juce::Rectangle<int> (width, height).toFloat();
        g.setColour (Colours::panelRaised);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (Colours::textPrimary);
        g.setFont (font (12.0f));
        g.drawFittedText (text, bounds.reduced (8.0f, 4.0f).toNearestInt(), juce::Justification::centredLeft, 4);
    }

    juce::Rectangle<int> Theme::getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
    {
        const auto f = font (12.0f);
        const auto w = juce::jmin (400, (int) juce::GlyphArrangement::getStringWidth (f, text) + 18);
        const auto h = (int) (f.getHeight() * (w >= 400 ? 3.0f : 1.0f)) + 10;

        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                     w, h)
            .constrainedWithin (parentArea);
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
