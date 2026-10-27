#include "Theme.h"

namespace benchmark::ui
{
    Theme::Theme()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
        setColour (juce::Label::textColourId, Colours::textPrimary);
        setColour (juce::TextButton::buttonColourId, Colours::panelRaised);
        setColour (juce::TextButton::textColourOffId, Colours::textPrimary);
        setColour (juce::TextButton::textColourOnId, Colours::textPrimary);
        setColour (juce::ToggleButton::textColourId, Colours::textPrimary);
        setColour (juce::ToggleButton::tickColourId, Colours::mix); // Nil2Max: switches light up cyan
        setColour (juce::PopupMenu::backgroundColourId, Colours::panelRaised);
        setColour (juce::PopupMenu::textColourId, Colours::textPrimary);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::outline);
        setColour (juce::PopupMenu::highlightedTextColourId, Colours::textPrimary);
        setColour (juce::TooltipWindow::backgroundColourId, Colours::panelRaised);
        setColour (juce::TooltipWindow::textColourId, Colours::textPrimary);
        setColour (juce::TooltipWindow::outlineColourId, Colours::outline);
    }

    void paintBackground (juce::Graphics& g, juce::Rectangle<int> area, const Theme& theme)
    {
        const auto bounds = area.toFloat();
        g.setGradientFill (juce::ColourGradient (Colours::backgroundTop, bounds.getX(), bounds.getY(),
                                                 Colours::background, bounds.getX(), bounds.getBottom(), false));
        g.fillRect (bounds);

        // Soft violet glow behind the centre, as behind Nil2Max's knob.
        const auto centre = bounds.getCentre().translated (0.0f, -bounds.getHeight() * 0.08f);
        g.setGradientFill (juce::ColourGradient (Colours::violet.withAlpha (0.10f), centre.x, centre.y,
                                                 Colours::violet.withAlpha (0.0f), centre.x + bounds.getWidth() * 0.45f, centre.y, true));
        g.fillRect (bounds);

        // Faint dot grid.
        const auto step = juce::jmax (8.0f, theme.px (14.0f));
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        for (auto y = bounds.getY() + step * 0.5f; y < bounds.getBottom(); y += step)
            for (auto x = bounds.getX() + step * 0.5f; x < bounds.getRight(); x += step)
                g.fillRect (x, y, 1.0f, 1.0f);
    }

    void paintPanel (juce::Graphics& g, juce::Rectangle<float> bounds, const Theme& theme, bool highlighted)
    {
        const auto corner = theme.panelCorner();
        g.setGradientFill (juce::ColourGradient (Colours::panel, bounds.getX(), bounds.getY(),
                                                 Colours::panelLow, bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, corner);

        if (highlighted)
            strokeBrandPill (g, bounds.reduced (1.0f), theme.px (2.0f));
        else
        {
            g.setColour (Colours::outline.withAlpha (0.7f));
            g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
        }
    }

    void strokeBrandPill (juce::Graphics& g, juce::Rectangle<float> bounds, float thickness)
    {
        g.setGradientFill (juce::ColourGradient (Colours::mix, bounds.getX(), bounds.getCentreY(),
                                                 Colours::violet, bounds.getRight(), bounds.getCentreY(), false));
        g.drawRoundedRectangle (bounds, juce::jmin (bounds.getHeight() * 0.5f, 16.0f * thickness), thickness);
    }

    void paintSectionLabel (juce::Graphics& g, const Theme& theme, const juce::String& text, juce::Rectangle<float> area, juce::Colour colour)
    {
        g.setColour (colour);
        g.setFont (theme.font (11.0f, true).withExtraKerningFactor (0.08f));
        g.drawText (text.toUpperCase(), area, juce::Justification::centredLeft, true);
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
        // Nil2Max switch: dark pill with a lavender knob; on, the pill fills with the accent and the knob turns white.
        auto bounds = button.getLocalBounds().toFloat();
        const auto h = juce::jmin (bounds.getHeight(), px (18.0f));
        auto track = bounds.removeFromLeft (h * 1.9f).withSizeKeepingCentre (h * 1.9f, h);

        const auto on = button.getToggleState();
        const auto accent = button.findColour (juce::ToggleButton::tickColourId);

        g.setColour (on ? accent : Colours::background);
        g.fillRoundedRectangle (track, h * 0.5f);
        g.setColour (on ? accent.brighter (0.2f) : (highlighted ? Colours::textMuted : Colours::outline));
        g.drawRoundedRectangle (track.reduced (0.5f), h * 0.5f, 1.0f);

        const auto knobSize = h - px (5.0f);
        const auto knobX = on ? track.getRight() - knobSize - px (2.5f) : track.getX() + px (2.5f);
        g.setColour (on ? juce::Colours::white : Colours::textSecondary);
        g.fillEllipse (knobX, track.getCentreY() - knobSize * 0.5f, knobSize, knobSize);

        bounds.removeFromLeft (px (8.0f));
        g.setColour (! button.isEnabled() ? Colours::textMuted : (on ? Colours::textPrimary : Colours::textSecondary));
        g.setFont (font (12.0f, on));
        g.drawText (button.getButtonText(), bounds, juce::Justification::centredLeft);
    }

    void Theme::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
    {
        // Pill buttons, like Nil2Max's ADV button.
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto corner = bounds.getHeight() * 0.5f;

        auto fill = Colours::panelRaised;
        if (down)              fill = fill.darker (0.2f);
        else if (highlighted)  fill = fill.brighter (0.1f);

        g.setColour (button.isEnabled() ? fill : fill.withAlpha (0.5f));
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (highlighted ? Colours::violet.withAlpha (0.8f) : Colours::outline);
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
