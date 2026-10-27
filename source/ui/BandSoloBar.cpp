#include "BandSoloBar.h"
#include "Theme.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "dsp/BandSolo.h"

namespace benchmark::ui
{
    namespace
    {
        const juce::StringArray labels { "Off", "Low", "Low-mid", "High-mid", "High" };
    }

    BandSoloBar::BandSoloBar (BenchmarkProcessor& p)
        : bandParam (*p.getValueTreeState().getParameter (ParamIDs::bandSolo)),
          bandAttachment (bandParam, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
    {
        setTitle ("Band solo");
        setTooltip ("Solo one frequency band on both Mix and Reference, to compare bass with bass and so on.\n"
                    "Bands: Low < 150 Hz, Low-mid 150 Hz-1 kHz, High-mid 1-5 kHz, High > 5 kHz. Keys 1-4, 0 = off.");
        bandAttachment.sendInitialUpdate();

        const auto latencyMs = juce::roundToInt (1000.0 * dsp::BandSolo::firLengthFor (48000.0) / 2 / 48000.0);
        linearToggle.setWantsKeyboardFocus (false);
        linearToggle.setTooltip ("Linear-phase bands: steeper and phase-accurate, but add about " + juce::String (latencyMs)
                                 + " ms of latency (reported to your DAW, which compensates). Off: zero-latency crossovers.");
        linearAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            p.getValueTreeState(), ParamIDs::crossoverMode, linearToggle);
        addAndMakeVisible (linearToggle);
    }

    BandSoloBar::~BandSoloBar() = default;

    void BandSoloBar::toggleBand (int band)
    {
        const auto index = (band <= 0 || band == selected) ? 0 : juce::jlimit (0, labels.size() - 1, band);
        bandAttachment.setValueAsCompleteGesture ((float) index);
    }

    juce::Rectangle<float> BandSoloBar::getSegmentsArea() const
    {
        auto& theme = getTheme (const_cast<BandSoloBar&> (*this));
        auto area = getLocalBounds().toFloat();
        area.removeFromLeft (theme.px (76.0f));
        area.removeFromRight (theme.px (132.0f));
        return area.withSizeKeepingCentre (area.getWidth(), theme.px (24.0f));
    }

    void BandSoloBar::resized()
    {
        auto& theme = getTheme (*this);
        linearToggle.setBounds (getLocalBounds().removeFromRight (juce::roundToInt (theme.px (124.0f))));
    }

    void BandSoloBar::paint (juce::Graphics& g)
    {
        auto& theme = getTheme (*this);
        const auto soloActive = selected > 0;

        g.setColour (soloActive ? Colours::violet : Colours::textSecondary);
        g.setFont (theme.font (10.5f, true).withExtraKerningFactor (0.1f));
        g.drawText ("BAND SOLO", getLocalBounds().toFloat().withWidth (theme.px (72.0f)), juce::Justification::centredLeft);

        const auto area = getSegmentsArea();
        const auto corner = area.getHeight() * 0.5f;
        g.setColour (Colours::panelRaised);
        g.fillRoundedRectangle (area, corner);
        if (soloActive)
            strokeBrandPill (g, area.reduced (0.75f), 1.5f);
        else
        {
            g.setColour (Colours::outline);
            g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.0f);
        }

        const auto w = area.getWidth() / (float) labels.size();
        for (int i = 0; i < labels.size(); ++i)
        {
            const auto segment = area.withX (area.getX() + w * (float) i).withWidth (w);

            if (i == selected)
            {
                g.setColour (i == 0 ? Colours::outline : Colours::violet.withAlpha (0.3f));
                g.fillRoundedRectangle (segment.reduced (theme.px (2.0f)), corner);
            }

            g.setColour (i == selected ? Colours::textPrimary : Colours::textSecondary);
            g.setFont (theme.font (11.0f, i == selected));
            g.drawText (labels[i], segment, juce::Justification::centred);
        }
    }

    void BandSoloBar::mouseDown (const juce::MouseEvent& e)
    {
        const auto area = getSegmentsArea();
        if (! area.contains (e.position))
            return;

        const auto index = juce::jlimit (0, labels.size() - 1, (int) ((e.position.x - area.getX()) / area.getWidth() * (float) labels.size()));
        bandAttachment.setValueAsCompleteGesture ((float) index);
    }
}
