#include "PlaceholderPanel.h"
#include "Theme.h"

namespace benchmark::ui
{
    PlaceholderPanel::PlaceholderPanel (juce::String titleText, juce::String hintText)
        : title (std::move (titleText)), hint (std::move (hintText))
    {
        setTitle (title);
    }

    void PlaceholderPanel::paint (juce::Graphics& g)
    {
        auto& theme = getTheme (*this);
        auto bounds = getLocalBounds().toFloat();
        const auto corner = theme.px (8.0f);

        g.setColour (Colours::panel);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

        auto header = bounds.removeFromTop (theme.px (30.0f)).reduced (theme.px (12.0f), 0.0f);
        g.setColour (Colours::textSecondary);
        g.setFont (theme.font (11.0f, true).withExtraKerningFactor (0.1f));
        g.drawText (title.toUpperCase(), header, juce::Justification::centredLeft);

        g.setColour (Colours::textMuted);
        g.setFont (theme.font (12.0f));
        g.drawFittedText (hint, bounds.reduced (theme.px (16.0f)).toNearestInt(), juce::Justification::centred, 3);
    }
}
