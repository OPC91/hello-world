#include "PluginEditor.h"
#include "Parameters.h"

namespace benchmark
{
    BenchmarkEditor::BenchmarkEditor (BenchmarkProcessor& p)
        : AudioProcessorEditor (p),
          processor (p),
          abToggle (*p.getValueTreeState().getParameter (ParamIDs::listenRef))
    {
        setLookAndFeel (&theme);

        for (auto* c : std::initializer_list<juce::Component*> { &abToggle, &referenceList, &waveform, &loudness, &analysis })
            addAndMakeVisible (c);

        setWantsKeyboardFocus (true);
        addMouseListener (this, true); // clicks anywhere give the editor key focus for the space bar

        setResizable (true, true);
        if (auto* sizeLimits = getConstrainer())
        {
            sizeLimits->setMinimumSize (ui::baseWidth * 4 / 5, ui::baseHeight * 4 / 5);
            sizeLimits->setMaximumSize (ui::baseWidth * 5 / 2, ui::baseHeight * 5 / 2);
        }

        const auto saved = processor.getSavedEditorSize();
        if (saved.x > 0 && saved.y > 0)
            setSize (saved.x, saved.y);
        else
            setSize (ui::baseWidth, ui::baseHeight);
    }

    BenchmarkEditor::~BenchmarkEditor()
    {
        setLookAndFeel (nullptr);
    }

    void BenchmarkEditor::paint (juce::Graphics& g)
    {
        g.fillAll (ui::Colours::background);

        auto header = headerArea.toFloat().reduced (theme.px (16.0f), 0.0f);

        // Wordmark: a small two-tone mark followed by the product name.
        const auto markSize = theme.px (14.0f);
        auto mark = header.removeFromLeft (markSize * 2.0f).withSizeKeepingCentre (markSize * 2.0f, markSize);
        g.setColour (ui::Colours::mix);
        g.fillRoundedRectangle (mark.removeFromLeft (markSize).reduced (theme.px (1.5f)), theme.px (2.0f));
        g.setColour (ui::Colours::reference);
        g.fillRoundedRectangle (mark.reduced (theme.px (1.5f)), theme.px (2.0f));

        header.removeFromLeft (theme.px (10.0f));
        g.setColour (ui::Colours::textPrimary);
        g.setFont (theme.font (16.0f, true).withExtraKerningFactor (0.25f));
        g.drawText ("BENCHMARK", header, juce::Justification::centredLeft);

        g.setColour (ui::Colours::textMuted);
        g.setFont (theme.font (11.0f));
        g.drawText ("v" BENCHMARK_VERSION_STRING "  |  space: Mix / Reference", header, juce::Justification::centredRight);
    }

    void BenchmarkEditor::resized()
    {
        theme.setScale (juce::jmin ((float) getWidth() / (float) ui::baseWidth,
                                    (float) getHeight() / (float) ui::baseHeight));

        const auto gap = juce::roundToInt (theme.px (12.0f));
        auto area = getLocalBounds();

        headerArea = area.removeFromTop (juce::roundToInt (theme.px (48.0f)));
        area.reduce (gap, 0);
        area.removeFromBottom (gap);

        referenceList.setBounds (area.removeFromLeft (juce::roundToInt ((float) area.getWidth() * 0.24f)));
        area.removeFromLeft (gap);

        waveform.setBounds (area.removeFromTop (juce::roundToInt ((float) area.getHeight() * 0.28f)));
        area.removeFromTop (gap);

        auto controls = area.removeFromTop (juce::roundToInt (theme.px (96.0f)));
        abToggle.setBounds (controls.removeFromLeft (juce::roundToInt ((float) controls.getWidth() * 0.58f)));
        controls.removeFromLeft (gap);
        loudness.setBounds (controls);
        area.removeFromTop (gap);

        analysis.setBounds (area);

        processor.setSavedEditorSize ({ getWidth(), getHeight() });
        repaint();
    }

    bool BenchmarkEditor::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::spaceKey)
        {
            abToggle.toggle();
            return true;
        }

        return false;
    }

    void BenchmarkEditor::visibilityChanged()
    {
        if (isShowing())
            grabKeyboardFocus();
    }

    void BenchmarkEditor::mouseDown (const juce::MouseEvent&)
    {
        grabKeyboardFocus();
    }
}
