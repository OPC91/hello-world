#include "PluginEditor.h"

namespace seam
{
    namespace
    {
        const char* styleTagline (int style)
        {
            switch (style)
            {
                case 0:  return "smooth seams, rolling in.";
                case 1:  return "tear it up at the turn.";
                case 2:  return "chop it. flip it. drop it.";
                default: return "wind it up. let it go.";
            }
        }

        int choiceIndex (juce::AudioProcessorValueTreeState& params, const char* id)
        {
            return juce::roundToInt (params.getRawParameterValue (id)->load());
        }
    }

    SeamEditor::SeamEditor (SeamProcessor& p)
        : exnilo::ScaledEditor (p, baseWidth, baseHeight, p.getEditorScale()),
          seamProcessor (p),
          params (p.getState())
    {
        onScaleChanged = [this] (float s) { seamProcessor.setEditorScale (s); };

        // Header
        wordmark.setTooltip ("SEAM by Exnilo");
        content.addAndMakeVisible (wordmark);

        int itemId = 1;
        for (const auto& theme : exnilo::themes::all())
            themeBox.addItem (juce::String (theme.name).toUpperCase(), itemId++);
        themeBox.getProperties().set ("exnilo.hero", true);
        themeBox.setTooltip ("Colour theme");
        themeBox.onChange = [this]
        {
            const int index = themeBox.getSelectedItemIndex();
            if (index >= 0)
                applyTheme (exnilo::themes::all()[static_cast<size_t> (index)].id);
        };
        content.addAndMakeVisible (themeBox);

        advButton.setClickingTogglesState (true);
        advButton.getProperties().set ("exnilo.radius", 22.0);
        advButton.setTooltip ("Advanced: scale each effect from 0 to 150%");
        content.addAndMakeVisible (advButton);

        // Main row
        energy.overlay = [this] (juce::Graphics& g, juce::Rectangle<float> area, const exnilo::GenreTheme& t) { paintCountdown (g, area, t); };
        content.addAndMakeVisible (energy);

        knob.setDoubleClickReturnValue (true, 0.0);
        knob.setTooltip ("TRANSITION: 0% is a true bypass. Turn up and the filter, reverb throw, stutter, duck and fills move together.");
        content.addAndMakeVisible (knob);
        knobAttachment = std::make_unique<SliderAttachment> (params, ids::transition, knob);

        content.addAndMakeVisible (options);
        options.addAndMakeVisible (phrase);
        phrase.setTooltip ("Phrase length: a seam falls every 8, 16 or 32 bars");
        segmentedAttachments.push_back (std::make_unique<exnilo::SegmentedAttachment> (*params.getParameter (ids::phrase), phrase));

        options.addAndMakeVisible (autoToggle);
        autoToggle.setTooltip ("Ramp TRANSITION up by itself over the last bars of every phrase, and drop to zero on the seam");
        autoAttachment = std::make_unique<ButtonAttachment> (params, ids::autoMode, autoToggle);
        autoBarsBox.addItemList (choices::autoBars, 1);
        autoCurveBox.addItemList (choices::autoCurve, 1);
        autoBarsBox.setTooltip ("How many bars the auto ramp lasts");
        autoCurveBox.setTooltip ("Linear rise, or a late surge that saves the push for the end");
        options.addAndMakeVisible (autoBarsBox);
        options.addAndMakeVisible (autoCurveBox);
        autoBarsAttachment = std::make_unique<ComboAttachment> (params, ids::autoBars, autoBarsBox);
        autoCurveAttachment = std::make_unique<ComboAttachment> (params, ids::autoCurve, autoCurveBox);

        options.addAndMakeVisible (vacuumToggle);
        vacuumToggle.setTooltip ("Pre-drop vacuum: a silent gap just before the seam's downbeat");
        vacuumAttachment = std::make_unique<ButtonAttachment> (params, ids::vacuum, vacuumToggle);
        options.addAndMakeVisible (vacuumSize);
        vacuumSize.setTooltip ("Length of the silent gap");
        segmentedAttachments.push_back (std::make_unique<exnilo::SegmentedAttachment> (*params.getParameter (ids::vacuumSize), vacuumSize));
        options.addAndMakeVisible (tapeStopToggle);
        tapeStopToggle.setTooltip ("Tape-stop the final beat before the gap");
        tapeStopAttachment = std::make_unique<ButtonAttachment> (params, ids::tapeStop, tapeStopToggle);
        options.addAndMakeVisible (meter);

        // Fill row
        content.addAndMakeVisible (fillCard);
        fillCard.addAndMakeVisible (breakZone);
        fillCard.addAndMakeVisible (style);
        style.setTooltip ("Fill style");
        segmentedAttachments.push_back (std::make_unique<exnilo::SegmentedAttachment> (*params.getParameter (ids::style), style));
        style.onChange = [this, previous = style.onChange] (int i) { if (previous) previous (i); updateTagline(); };
        fillCard.addAndMakeVisible (fillLength);
        fillLength.setTooltip ("Fill length: the last 1, 2 or 4 bars before each seam");
        segmentedAttachments.push_back (std::make_unique<exnilo::SegmentedAttachment> (*params.getParameter (ids::fillLength), fillLength));
        fillCard.addAndMakeVisible (fillMode);
        fillMode.setTooltip ("Mix: fill over your drums. Replace: the fill takes over.");
        segmentedAttachments.push_back (std::make_unique<exnilo::SegmentedAttachment> (*params.getParameter (ids::fillMode), fillMode));
        fillCard.addAndMakeVisible (complexity);
        complexity.setTooltip ("Complexity: how dense the fill is");
        complexity.setDoubleClickReturnValue (true, 50.0);
        complexityAttachment = std::make_unique<SliderAttachment> (params, ids::complexity, complexity);
        fillCard.addAndMakeVisible (newFillButton);
        newFillButton.setTooltip ("Roll a new fill");
        newFillButton.onClick = [this] { seamProcessor.newFill(); };
        fillCard.addAndMakeVisible (midiDrag);

        // Footer
        content.addAndMakeVisible (presetBar);
        for (auto* label : { &tagline, &hint })
        {
            label->setJustificationType (juce::Justification::centred);
            label->setInterceptsMouseClicks (false, false);
            content.addAndMakeVisible (*label);
        }
        tagline.setFont (exnilo::fonts::regular (16.0f));
        tagline.setColour (juce::Label::textColourId, exnilo::palette::dim);
        hint.setFont (exnilo::fonts::regular (13.0f));
        hint.setColour (juce::Label::textColourId, exnilo::palette::dim.withAlpha (0.7f));
        hint.setText ("ctrl/cmd-drag: fine  |  double-click: reset  |  drop a break on the fill panel", juce::dontSendNotification);

        applyTheme (seamProcessor.getThemeId());
        updateTagline();
        layout();
        startTimerHz (30);
    }

    SeamEditor::~SeamEditor()
    {
        stopTimer();
        energy.setAnimating (false);
    }

    void SeamEditor::layout()
    {
        wordmark.setBounds (30, 18, 320, 44);
        themeBox.setBounds (560, 18, 230, 46);
        advButton.setBounds (804, 18, 128, 46);

        energy.setBounds (24, 84, 290, 300);
        knob.setBounds (330, 74, 300, 320);
        options.setBounds (646, 84, 290, 300);

        phrase.setBounds (20, 44, 204, 32);
        autoToggle.setBounds (20, 92, 204, 28);
        autoBarsBox.setBounds (20, 126, 98, 32);
        autoCurveBox.setBounds (124, 126, 100, 32);
        vacuumToggle.setBounds (20, 174, 204, 28);
        vacuumSize.setBounds (20, 208, 204, 32);
        tapeStopToggle.setBounds (20, 252, 204, 28);
        meter.setBounds (242, 44, 30, 236);

        fillCard.setBounds (24, 396, 912, 128);
        breakZone.setBounds (16, 40, 274, 74);
        style.setBounds (306, 40, 290, 34);
        fillLength.setBounds (306, 82, 160, 32);
        fillMode.setBounds (474, 82, 122, 32);
        complexity.setBounds (618, 30, 80, 72);
        newFillButton.setBounds (716, 40, 180, 34);
        midiDrag.setBounds (716, 82, 180, 32);

        presetBar.setBounds (330, 536, 300, 46);
        tagline.setBounds (230, 588, 500, 22);
        hint.setBounds (180, 612, 600, 20);
    }

    void SeamEditor::applyTheme (const juce::String& id)
    {
        const auto& theme = exnilo::themes::byId (id);
        getExniloLookAndFeel().setTheme (theme);
        seamProcessor.setThemeId (theme.id);
        themeBox.setSelectedItemIndex (exnilo::themes::indexOf (theme.id), juce::dontSendNotification);
        sendLookAndFeelChange();
        repaint();
    }

    void SeamEditor::updateTagline()
    {
        tagline.setText (styleTagline (choiceIndex (params, ids::style)), juce::dontSendNotification);
    }

    void SeamEditor::paintCountdown (juce::Graphics& g, juce::Rectangle<float> area, const exnilo::GenreTheme& t)
    {
        auto inner = area.reduced (18.0f, 14.0f);
        exnilo::drawCaption (g, "PHRASE  " + juce::String (phraseBars) + " BARS", inner.removeFromTop (18.0f));

        // Phrase strip: one cell per bar. The fill window is outlined in accent B; the auto ramp is tinted.
        auto strip = inner.removeFromBottom (12.0f);
        const int fillBars = choices::fillLengthBars[static_cast<size_t> (juce::jlimit (0, 2, choiceIndex (params, ids::fillLength)))];
        const bool autoOn = params.getRawParameterValue (ids::autoMode)->load() > 0.5f;
        const int autoBars = choices::autoBarsValues[static_cast<size_t> (juce::jlimit (0, 3, choiceIndex (params, ids::autoBars)))];
        const float gap = phraseBars > 16 ? 2.0f : 3.0f;
        const float cellW = (strip.getWidth() - gap * static_cast<float> (phraseBars - 1)) / static_cast<float> (phraseBars);
        for (int i = 0; i < phraseBars; ++i)
        {
            const auto cell = juce::Rectangle<float> (strip.getX() + static_cast<float> (i) * (cellW + gap), strip.getY(), cellW, strip.getHeight());
            const bool inFill = i >= phraseBars - fillBars;
            const bool inAuto = autoOn && i >= phraseBars - autoBars;
            juce::Colour c = t.tint (exnilo::palette::surface2, 0.12f);
            if (inAuto) c = c.interpolatedWith (t.accentA, 0.25f);
            if (playing && i < barInPhrase) c = t.at (static_cast<float> (i) / static_cast<float> (phraseBars)).withAlpha (0.55f);
            if (playing && i == barInPhrase) c = t.at (static_cast<float> (i) / static_cast<float> (phraseBars));
            g.setColour (c);
            g.fillRoundedRectangle (cell, 2.5f);
            if (inFill)
            {
                g.setColour (t.accentB.withAlpha (0.9f));
                g.drawRoundedRectangle (cell.expanded (1.0f), 3.0f, 1.2f);
            }
        }

        // Countdown.
        auto countArea = inner.removeFromBottom (48.0f);
        juce::String number, unit;
        if (! playing)
        {
            number = "--";
            unit = "PLAY TO SYNC";
        }
        else if (barsToSeam > 1.0 + 1.0e-6)
        {
            const int bars = static_cast<int> (std::ceil (barsToSeam - 1.0e-6));
            number = juce::String (bars);
            unit = bars == 1 ? "BAR TO SEAM" : "BARS TO SEAM";
        }
        else
        {
            const double barsPerBeat = juce::jmax (0.01, seamProcessor.getUiState().barsPerBeat.load());
            const int beats = juce::jmax (1, static_cast<int> (std::ceil (barsToSeam / barsPerBeat - 1.0e-6)));
            number = juce::String (beats);
            unit = beats == 1 ? "BEAT TO SEAM" : "BEATS TO SEAM";
        }

        const auto numFont = exnilo::fonts::heavy (40.0f);
        const float numW = juce::GlyphArrangement::getStringWidth (numFont, number) + 10.0f;
        g.setFont (numFont);
        g.setColour (exnilo::palette::ink);
        g.drawText (number, countArea.removeFromLeft (numW), juce::Justification::centredLeft, false);
        exnilo::drawCaption (g, unit, countArea.withTrimmedTop (12.0f), juce::Justification::centredLeft,
                             playing && barsToSeam <= static_cast<double> (fillBars) ? t.accentB : exnilo::palette::dim);
    }

    void SeamEditor::advanceForSnapshot (float seconds)
    {
        timerCallback();
        for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f)
            energy.advance (1.0f / 60.0f);
    }

    void SeamEditor::timerCallback()
    {
        auto& ui = seamProcessor.getUiState();
        playing = ui.playing.load();
        barsToSeam = ui.barsToSeam.load();
        barInPhrase = ui.barInPhrase.load();
        phraseBars = ui.phraseBars.load();

        const float effective = ui.effective.load();
        energy.setEnergy (effective);
        energy.setBeatPhase (playing ? ui.beatPhase.load() : -1.0f);
        knob.setLiveProportion (effective);

        const double now = juce::Time::getMillisecondCounterHiRes();
        const float dt = lastTick > 0.0 ? static_cast<float> (juce::jlimit (0.0, 0.2, (now - lastTick) * 0.001)) : 1.0f / 30.0f;
        lastTick = now;
        float l = 0.0f, r = 0.0f;
        ui.takePeaks (l, r);
        meter.push (l, r, dt);

        vacuumSize.setEnabled (params.getRawParameterValue (ids::vacuum)->load() > 0.5f);
        tapeStopToggle.setEnabled (vacuumSize.isEnabled());
        autoBarsBox.setEnabled (params.getRawParameterValue (ids::autoMode)->load() > 0.5f);
        autoCurveBox.setEnabled (autoBarsBox.isEnabled());
        updateTagline();

        if (const int version = seamProcessor.getFillWorker().getViewVersion(); version != lastViewVersion)
        {
            lastViewVersion = version;
            breakZone.refresh();
            const auto description = seamProcessor.getFillWorker().getView().fillDescription;
            fillCard.description = description;
            newFillButton.setTooltip ("Roll a new fill. Now: " + description);
            fillCard.repaint();
        }
    }
}
