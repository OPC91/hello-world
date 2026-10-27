#include "LoudnessPanel.h"
#include "Theme.h"
#include "PluginProcessor.h"
#include "Parameters.h"

namespace benchmark::ui
{
    LoudnessPanel::LoudnessPanel (BenchmarkProcessor& p)
        : processor (p)
    {
        setTitle ("Loudness");

        matchToggle.setWantsKeyboardFocus (false);
        matchToggle.setTooltip ("Loudness match: turns the louder of Mix and Reference down so they play at the same integrated loudness.");
        matchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            p.getValueTreeState(), ParamIDs::loudnessMatch, matchToggle);
        addAndMakeVisible (matchToggle);

        resetButton.setWantsKeyboardFocus (false);
        resetButton.setTooltip ("Restart the mix's integrated loudness measurement (e.g. after changing your mix or song section).");
        resetButton.onClick = [this] { processor.resetMixLoudness(); };
        addAndMakeVisible (resetButton);

        startTimerHz (15);
    }

    LoudnessPanel::~LoudnessPanel()
    {
        stopTimer();
    }

    void LoudnessPanel::timerCallback()
    {
        const auto& r = processor.getReadings();
        const auto rounded = [] (float v) { return std::isfinite (v) ? std::round (v * 10.0f) / 10.0f : v; };

        const Snapshot now { rounded (r.mixShortTerm.load()), rounded (r.mixIntegrated.load()),
                             rounded (r.referenceShortTerm.load()), rounded (r.referenceIntegrated.load()),
                             rounded (r.mixGainDb.load()), rounded (r.referenceGainDb.load()),
                             r.referenceAvailable.load(), matchToggle.getToggleState(), r.referenceLoudnessFromLoop.load() };

        if (! (now == shown))
        {
            shown = now;
            repaint();
        }
    }

    void LoudnessPanel::resized()
    {
        auto& theme = getTheme (*this);
        auto header = getLocalBounds().reduced (juce::roundToInt (theme.px (10.0f)), 0)
                          .removeFromTop (juce::roundToInt (theme.px (30.0f)));

        resetButton.setBounds (header.removeFromRight (juce::roundToInt (theme.px (46.0f)))
                                   .withSizeKeepingCentre (juce::roundToInt (theme.px (46.0f)), juce::roundToInt (theme.px (20.0f))));
        header.removeFromRight (juce::roundToInt (theme.px (8.0f)));
        matchToggle.setBounds (header.removeFromRight (juce::roundToInt (theme.px (88.0f))));
    }

    void LoudnessPanel::paint (juce::Graphics& g)
    {
        auto& theme = getTheme (*this);
        auto bounds = getLocalBounds().toFloat();
        paintPanel (g, bounds, theme);

        auto header = bounds.removeFromTop (theme.px (30.0f)).reduced (theme.px (12.0f), 0.0f);
        g.setColour (Colours::textSecondary);
        g.setFont (theme.font (11.0f, true).withExtraKerningFactor (0.1f));
        g.drawText ("LOUDNESS", header, juce::Justification::centredLeft);

        auto content = bounds.reduced (theme.px (12.0f), 0.0f);
        auto footer = content.removeFromBottom (theme.px (24.0f));
        auto mixColumn = content.removeFromLeft (content.getWidth() * 0.5f);
        auto refColumn = content;

        const auto drawColumn = [&] (juce::Rectangle<float> area, const juce::String& label, juce::Colour colour,
                                     float mainValue, const juce::String& mainCaption,
                                     float secondValue, const juce::String& secondCaption)
        {
            g.setColour (colour);
            g.setFont (theme.font (10.0f, true).withExtraKerningFactor (0.1f));
            g.drawText (label, area.removeFromTop (theme.px (12.0f)), juce::Justification::centredLeft);

            auto valueRow = area.removeFromTop (area.getHeight() * 0.62f);
            g.setColour (Colours::textPrimary);
            g.setFont (theme.font (20.0f, true));
            const auto valueText = formatLufs (mainValue);
            g.drawText (valueText, valueRow, juce::Justification::bottomLeft);

            const auto valueWidth = juce::GlyphArrangement::getStringWidth (theme.font (20.0f, true), valueText);
            g.setColour (Colours::textMuted);
            g.setFont (theme.font (10.0f));
            g.drawText (mainCaption, valueRow.withTrimmedLeft (valueWidth + theme.px (4.0f)).withTrimmedBottom (theme.px (3.0f)),
                        juce::Justification::bottomLeft);

            g.drawText (secondCaption + " " + formatLufs (secondValue), area, juce::Justification::centredLeft);
        };

        drawColumn (mixColumn, "MIX", Colours::mix, shown.mixIntegrated, "LUFS-I", shown.mixShort, "short-term");
        drawColumn (refColumn, "REFERENCE", Colours::reference,
                    shown.referenceAvailable ? shown.refIntegrated : LiveReadings::noValue, shown.fromLoop ? "LUFS-I loop" : "LUFS-I",
                    shown.refShort, "short-term");

        // Plain-language status of the match.
        juce::String status;
        auto statusColour = Colours::textSecondary;

        if (! shown.matchOn)
            status = "Loudness match is off";
        else if (! shown.referenceAvailable)
            status = "Load a reference to match loudness";
        else if (! std::isfinite (shown.mixIntegrated) || shown.mixIntegrated < -70.0f)
            status = "Play your mix to measure it";
        else if (shown.mixGain < -0.05f)
        {
            status = "Mix turned down " + juce::String (-shown.mixGain, 1) + " dB to match" + (shown.fromLoop ? " (loop)" : "") + "; exports stay untouched";
            statusColour = Colours::mix;
        }
        else if (shown.refGain < -0.05f)
        {
            status = "Reference turned down " + juce::String (-shown.refGain, 1) + " dB to match" + (shown.fromLoop ? " (loop)" : "");
            statusColour = Colours::reference;
        }
        else
            status = "Levels matched";

        g.setColour (statusColour);
        g.setFont (theme.font (11.0f, true));
        g.drawText (status, footer, juce::Justification::centredLeft);
    }
}
