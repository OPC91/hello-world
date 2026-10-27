#include "AnalysisPanel.h"
#include "Theme.h"
#include "PluginProcessor.h"
#include "analysis/HintGenerator.h"

namespace benchmark::ui
{
    using namespace analysis;

    namespace
    {
        constexpr float logMin = 1.30103f;  // log10 (20)
        constexpr float logMax = 4.30103f;  // log10 (20000)

        float frequencyToX (float f, juce::Rectangle<float> plot)
        {
            return plot.getX() + (std::log10 (f) - logMin) / (logMax - logMin) * plot.getWidth();
        }

        float xToFrequency (float x, juce::Rectangle<float> plot)
        {
            return std::pow (10.0f, logMin + (x - plot.getX()) / plot.getWidth() * (logMax - logMin));
        }

        juce::String frequencyLabel (float f)
        {
            if (f >= 1000.0f)
                return juce::String (f / 1000.0f, f >= 10000.0f || std::fmod (f, 1000.0f) < 1.0f ? 0 : 1) + "k";
            return juce::String (juce::roundToInt (f));
        }

        juce::String signedDb (float db, int decimals = 1)
        {
            if (! std::isfinite (db))
                return juce::String::fromUTF8 ("\xe2\x80\x93");
            return (db >= 0.0f ? "+" : juce::String::fromUTF8 ("\xe2\x88\x92")) + juce::String (std::abs (db), decimals);
        }

        juce::String plainDb (float db, int decimals = 1)
        {
            if (! std::isfinite (db))
                return juce::String::fromUTF8 ("\xe2\x80\x93");
            return juce::String (db, decimals).replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92"));
        }

        void strokeCurve (juce::Graphics& g, const std::vector<juce::Point<float>>& points, juce::Colour colour, float thickness)
        {
            juce::Path path;
            bool started = false;

            for (const auto& p : points)
            {
                if (! std::isfinite (p.y))
                {
                    started = false;
                    continue;
                }

                if (! started) { path.startNewSubPath (p); started = true; }
                else           path.lineTo (p);
            }

            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    //==============================================================================
    /** Small segmented selector (not a parameter). */
    class AnalysisPanel::Selector final : public juce::Component
    {
    public:
        explicit Selector (juce::StringArray labelsIn) : labels (std::move (labelsIn)) {}

        std::function<void (int)> onChange;
        int selected = 0;

        void paint (juce::Graphics& g) override
        {
            auto& theme = getTheme (*this);
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            const auto corner = bounds.getHeight() * 0.5f;

            g.setColour (Colours::panelRaised);
            g.fillRoundedRectangle (bounds, corner);
            g.setColour (Colours::outline);
            g.drawRoundedRectangle (bounds, corner, 1.0f);

            const auto w = bounds.getWidth() / (float) labels.size();
            for (int i = 0; i < labels.size(); ++i)
            {
                const auto segment = bounds.withX (bounds.getX() + w * (float) i).withWidth (w);
                if (i == selected)
                {
                    g.setColour (Colours::violet.withAlpha (0.25f));
                    g.fillRoundedRectangle (segment.reduced (theme.px (2.0f)), corner);
                    strokeBrandPill (g, segment.reduced (theme.px (2.0f)), 1.2f);
                }

                g.setColour (i == selected ? Colours::textPrimary : Colours::textSecondary);
                g.setFont (theme.font (11.0f, i == selected));
                g.drawText (labels[i], segment, juce::Justification::centred);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto index = juce::jlimit (0, labels.size() - 1, (int) (e.position.x / (float) getWidth() * (float) labels.size()));
            if (index != selected)
            {
                selected = index;
                repaint();
                if (onChange)
                    onChange (index);
            }
        }

    private:
        juce::StringArray labels;
    };

    //==============================================================================
    AnalysisPanel::AnalysisPanel (BenchmarkProcessor& p)
        : processor (p)
    {
        setTitle ("Comparison");

        tabSelector = std::make_unique<Selector> (juce::StringArray { "Tonal balance", "Stereo width", "Dynamics" });
        tabSelector->onChange = [this] (int i) { setTab ((Tab) i); };
        addAndMakeVisible (*tabSelector);

        modeSelector = std::make_unique<Selector> (juce::StringArray { "Average", "Live" });
        modeSelector->onChange = [this] (int i) { setLive (i == 1); };
        addAndMakeVisible (*modeSelector);

        resetButton.setWantsKeyboardFocus (false);
        resetButton.setTooltip ("Start the comparison again (both mix and reference)");
        resetButton.onClick = [this] { processor.getAnalysis().requestReset (true, true); };
        addAndMakeVisible (resetButton);

        copyButton.setWantsKeyboardFocus (false);
        copyButton.setTooltip ("Copy all notes as text, to paste into your session notes, a chat or an email.\n"
                               "Right-click the notes to copy just this tab.");
        copyButton.onClick = [this] { copyNotes (true); };
        addAndMakeVisible (copyButton);

        startTimerHz (15);
    }

    AnalysisPanel::~AnalysisPanel()
    {
        stopTimer();
    }

    void AnalysisPanel::setTab (Tab newTab)
    {
        tab = newTab;
        tabSelector->selected = (int) newTab;
        modeSelector->setVisible (tab != Tab::dynamics); // dynamics are always since reset
        tabSelector->repaint();
        repaint();
    }

    void AnalysisPanel::setLive (bool shouldBeLive)
    {
        live = shouldBeLive;
        modeSelector->selected = live ? 1 : 0;
        modeSelector->repaint();
        repaint();
    }

    void AnalysisPanel::timerCallback()
    {
        auto& engine = processor.getAnalysis();

        if (engine.getVersion() != shownVersion)
        {
            engine.getSnapshot (snapshot);
            shownVersion = snapshot.version;
            repaint();
        }
    }

    const SpectralView& AnalysisPanel::mixView() const { return live ? snapshot.mix.live : snapshot.mix.average; }
    const SpectralView& AnalysisPanel::refView() const { return live ? snapshot.reference.live : snapshot.reference.average; }

    juce::StringArray AnalysisPanel::currentHints() const
    {
        return HintGenerator::notesFor (snapshot, (HintGenerator::Section) tab, live);
    }

    void AnalysisPanel::copyNotes (bool allSections)
    {
        const auto* active = processor.getReferenceLibrary().getActive();
        const auto text = HintGenerator::formatReport (snapshot, live, active != nullptr ? active->name : juce::String(),
                                                       allSections ? std::nullopt
                                                                   : std::optional<HintGenerator::Section> ((HintGenerator::Section) tab));
        juce::SystemClipboard::copyTextToClipboard (text);

        if (onMessage)
            onMessage (allSections ? juce::String ("All notes copied: paste them anywhere")
                                   : HintGenerator::sectionTitle ((HintGenerator::Section) tab) + " notes copied");
    }

    void AnalysisPanel::mouseDown (const juce::MouseEvent& e)
    {
        if (! e.mods.isPopupMenu() || ! hintsArea.contains (e.getPosition()))
            return;

        juce::PopupMenu menu;
        menu.addItem (1, "Copy " + HintGenerator::sectionTitle ((HintGenerator::Section) tab).toLowerCase() + " notes");
        menu.addItem (2, "Copy all notes");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [safeThis = juce::Component::SafePointer<AnalysisPanel> (this)] (int result)
                            {
                                if (safeThis != nullptr && result > 0)
                                    safeThis->copyNotes (result == 2);
                            });
    }

    //==============================================================================
    void AnalysisPanel::resized()
    {
        auto& theme = getTheme (*this);
        const auto px = [&theme] (float v) { return juce::roundToInt (theme.px (v)); };

        auto area = getLocalBounds();
        headerArea = area.removeFromTop (px (34.0f)).reduced (px (12.0f), 0);

        auto header = headerArea;
        header.removeFromLeft (px (100.0f)); // title
        tabSelector->setBounds (header.removeFromLeft (px (270.0f)).withSizeKeepingCentre (px (270.0f), px (22.0f)));
        resetButton.setBounds (header.removeFromRight (px (50.0f)).withSizeKeepingCentre (px (50.0f), px (22.0f)));
        header.removeFromRight (px (8.0f));
        modeSelector->setBounds (header.removeFromRight (px (120.0f)).withSizeKeepingCentre (px (120.0f), px (22.0f)));

        area.reduce (px (12.0f), 0);
        area.removeFromBottom (px (10.0f));
        hintsArea = area.removeFromRight (juce::roundToInt ((float) area.getWidth() * 0.32f));
        copyButton.setBounds (juce::Rectangle<int> (px (46.0f), px (18.0f))
                                  .withPosition (hintsArea.getRight() - px (54.0f), hintsArea.getY() + px (6.0f)));
        area.removeFromRight (px (14.0f));
        chartArea = area;
    }

    void AnalysisPanel::paint (juce::Graphics& g)
    {
        auto& theme = getTheme (*this);
        const auto bounds = getLocalBounds().toFloat();
        paintPanel (g, bounds, theme);

        g.setColour (Colours::textSecondary);
        g.setFont (theme.font (11.0f, true).withExtraKerningFactor (0.1f));
        g.drawText ("COMPARISON", headerArea, juce::Justification::centredLeft);

        auto chart = chartArea.toFloat();
        auto status = chart.removeFromBottom (theme.px (16.0f));

        switch (tab)
        {
            case Tab::tonal:    paintTonal (g, chart); break;
            case Tab::stereo:   paintStereo (g, chart); break;
            case Tab::dynamics: paintDynamics (g, chart); break;
        }

        // How much audio the comparison is based on.
        const auto describe = [] (double seconds)
        {
            const auto s = juce::roundToInt (seconds);
            return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
        };

        g.setColour (Colours::textMuted);
        g.setFont (theme.font (10.0f));
        const auto window = (live && tab != Tab::dynamics) ? juce::String ("Live (last few seconds)") : juce::String ("Average since reset");
        g.drawText (window + ":  mix " + describe (snapshot.mix.secondsAnalysed) + "  |  reference "
                        + describe (snapshot.reference.secondsAnalysed) + "  (silence ignored)",
                    status, juce::Justification::bottomLeft, true);

        paintHints (g, hintsArea.toFloat(), currentHints());
    }

    void AnalysisPanel::paintLegend (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto& theme = getTheme (*this);
        g.setFont (theme.font (10.5f));

        for (const auto& [label, colour] : { std::pair { "Mix", Colours::mixChart }, { "Reference", Colours::referenceChart } })
        {
            const auto swatch = area.removeFromLeft (theme.px (12.0f)).withSizeKeepingCentre (theme.px (12.0f), theme.px (3.0f));
            g.setColour (colour);
            g.fillRoundedRectangle (swatch, theme.px (1.5f));
            area.removeFromLeft (theme.px (5.0f));

            const auto w = juce::GlyphArrangement::getStringWidth (theme.font (10.5f), label);
            g.setColour (Colours::textSecondary);
            g.drawText (label, area.removeFromLeft (w + 2.0f), juce::Justification::centredLeft);
            area.removeFromLeft (theme.px (14.0f));
        }
    }

    //==============================================================================
    void AnalysisPanel::paintTonal (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto& theme = getTheme (*this);
        const auto& mix = mixView();
        const auto& ref = refView();

        paintLegend (g, area.removeFromTop (theme.px (16.0f)).withTrimmedLeft (theme.px (32.0f)));
        area.removeFromTop (theme.px (4.0f));

        const auto axisWidth = theme.px (32.0f);
        auto plots = area.withTrimmedLeft (axisWidth);
        auto xAxis = plots.removeFromBottom (theme.px (14.0f));
        spectrumPlot = plots.removeFromTop (plots.getHeight() * 0.62f);
        plots.removeFromTop (theme.px (6.0f));
        differencePlot = plots;

        // Spectrum y range: 36 dB below the highest point of either curve.
        float top = -1000.0f;
        for (const auto* v : { &mix, &ref })
            if (v->valid)
                for (const auto value : v->curve)
                    if (std::isfinite (value))
                        top = std::max (top, value);

        top = top > -1000.0f ? std::ceil (top / 6.0f) * 6.0f : 0.0f;
        const auto bottom = top - 36.0f;
        const auto yFor = [&] (float db) { return juce::jmap (juce::jlimit (bottom, top, db), bottom, top, spectrumPlot.getBottom(), spectrumPlot.getY()); };

        // Difference range: at least +/- 6 dB, growing in 3 dB steps up to 18.
        float maxDiff = 6.0f;
        if (mix.valid && ref.valid)
            for (int i = 0; i < numCurvePoints; ++i)
                if (std::isfinite (mix.curve[(size_t) i]) && std::isfinite (ref.curve[(size_t) i]))
                    maxDiff = std::max (maxDiff, std::abs (mix.curve[(size_t) i] - ref.curve[(size_t) i]));
        const auto diffRange = std::min (18.0f, std::ceil (maxDiff / 3.0f) * 3.0f);
        const auto yDiff = [&] (float db) { return juce::jmap (juce::jlimit (-diffRange, diffRange, db), -diffRange, diffRange, differencePlot.getBottom(), differencePlot.getY()); };

        // Grid.
        g.setFont (theme.font (9.5f));
        for (const auto f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const auto x = frequencyToX (f, spectrumPlot);
            g.setColour (Colours::grid);
            g.fillRect (x, spectrumPlot.getY(), 1.0f, spectrumPlot.getHeight());
            g.fillRect (x, differencePlot.getY(), 1.0f, differencePlot.getHeight());
            g.setColour (Colours::textMuted);
            g.drawText (frequencyLabel (f), juce::Rectangle<float> (x - 20.0f, xAxis.getY(), 40.0f, xAxis.getHeight()), juce::Justification::centred);
        }

        for (float db = bottom; db <= top + 0.1f; db += 12.0f)
        {
            const auto y = yFor (db);
            g.setColour (Colours::grid);
            g.fillRect (spectrumPlot.getX(), y, spectrumPlot.getWidth(), 1.0f);

            // The bottom label would collide with the difference plot's.
            if (db > bottom + 0.1f)
            {
                g.setColour (Colours::textMuted);
                g.drawText (juce::exactlyEqual (db, top) ? juce::String ("0 dB") : plainDb (db - top, 0),
                            juce::Rectangle<float> (area.getX(), y - 6.0f, axisWidth - 4.0f, 12.0f), juce::Justification::centredRight);
            }
        }

        // Difference plot frame and labels.
        for (const auto db : { -diffRange, 0.0f, diffRange })
        {
            const auto y = yDiff (db);
            g.setColour (juce::exactlyEqual (db, 0.0f) ? Colours::textMuted : Colours::grid);
            g.fillRect (differencePlot.getX(), y, differencePlot.getWidth(), 1.0f);
            g.setColour (Colours::textMuted);
            g.drawText (juce::exactlyEqual (db, 0.0f) ? juce::String ("0") : signedDb (db, 0),
                        juce::Rectangle<float> (area.getX(), y - 6.0f, axisWidth - 4.0f, 12.0f), juce::Justification::centredRight);
        }

        if (! mix.valid && ! ref.valid)
        {
            g.setColour (Colours::textMuted);
            g.setFont (theme.font (12.0f));
            g.drawText ("Waiting for audio", spectrumPlot, juce::Justification::centred);
            return;
        }

        // Curves: reference first, mix on top.
        std::vector<juce::Point<float>> points ((size_t) numCurvePoints);
        for (const auto* v : { &ref, &mix })
        {
            if (! v->valid)
                continue;

            for (int i = 0; i < numCurvePoints; ++i)
                points[(size_t) i] = { frequencyToX (curvePointFrequency (i), spectrumPlot),
                                       std::isfinite (v->curve[(size_t) i]) ? yFor (v->curve[(size_t) i]) : std::numeric_limits<float>::quiet_NaN() };

            strokeCurve (g, points, v == &mix ? Colours::mixChart : Colours::referenceChart, theme.px (2.0f));
        }

        // Difference: filled above zero in the mix colour, below in the reference colour.
        if (mix.valid && ref.valid)
        {
            const auto zero = yDiff (0.0f);
            juce::Path above, below, line;
            above.startNewSubPath (differencePlot.getX(), zero);
            below.startNewSubPath (differencePlot.getX(), zero);
            bool started = false;

            for (int i = 0; i < numCurvePoints; ++i)
            {
                const auto m = mix.curve[(size_t) i], r = ref.curve[(size_t) i];
                if (! std::isfinite (m) || ! std::isfinite (r))
                    continue;

                const auto x = frequencyToX (curvePointFrequency (i), differencePlot);
                const auto y = yDiff (m - r);
                above.lineTo (x, std::min (y, zero));
                below.lineTo (x, std::max (y, zero));

                if (! started) { line.startNewSubPath (x, y); started = true; }
                else           line.lineTo (x, y);
            }

            above.lineTo (differencePlot.getRight(), zero);
            below.lineTo (differencePlot.getRight(), zero);
            above.closeSubPath();
            below.closeSubPath();

            g.setColour (Colours::mixChart.withAlpha (0.35f));
            g.fillPath (above);
            g.setColour (Colours::referenceChart.withAlpha (0.35f));
            g.fillPath (below);
            g.setColour (Colours::textSecondary);
            g.strokePath (line, juce::PathStrokeType (theme.px (1.5f)));
        }

        // Direction labels on small backgrounds so the fill never hides them.
        g.setFont (theme.font (9.5f));
        for (const auto& [label, atTop] : { std::pair { "mix has more", true }, { "mix has less", false } })
        {
            const auto w = juce::GlyphArrangement::getStringWidth (theme.font (9.5f), label) + theme.px (8.0f);
            const auto h = theme.px (13.0f);
            const auto box = juce::Rectangle<float> (differencePlot.getRight() - w - theme.px (2.0f),
                                                     atTop ? differencePlot.getY() + theme.px (2.0f) : differencePlot.getBottom() - h - theme.px (2.0f), w, h);
            g.setColour (Colours::panel.withAlpha (0.85f));
            g.fillRoundedRectangle (box, theme.px (3.0f));
            g.setColour (Colours::textMuted);
            g.drawText (label, box, juce::Justification::centred);
        }

        // Hover readout.
        const auto plotSpan = spectrumPlot.getUnion (differencePlot);
        if (plotSpan.contains (mouse))
        {
            const auto f = xToFrequency (mouse.x, spectrumPlot);
            const auto index = juce::jlimit (0, numCurvePoints - 1,
                                             juce::roundToInt (std::log (f / minFrequency) / std::log (maxFrequency / minFrequency) * (numCurvePoints - 1)));
            const auto m = mix.valid ? mix.curve[(size_t) index] - top : noValue;
            const auto r = ref.valid ? ref.curve[(size_t) index] - top : noValue;

            g.setColour (Colours::textSecondary.withAlpha (0.6f));
            g.fillRect (mouse.x, plotSpan.getY(), 1.0f, plotSpan.getHeight());

            for (const auto& [value, colour] : { std::pair { m, Colours::mixChart }, { r, Colours::referenceChart } })
            {
                if (! std::isfinite (value))
                    continue;
                const auto c = juce::Point<float> (mouse.x, yFor (value + top));
                g.setColour (Colours::panel);
                g.fillEllipse (juce::Rectangle<float> (theme.px (10.0f), theme.px (10.0f)).withCentre (c));
                g.setColour (colour);
                g.fillEllipse (juce::Rectangle<float> (theme.px (7.0f), theme.px (7.0f)).withCentre (c));
            }

            auto text = frequencyLabel (f) + " Hz   Mix " + plainDb (m) + "   Ref " + plainDb (r);
            if (std::isfinite (m) && std::isfinite (r))
                text << "   " << juce::String::fromUTF8 ("\xce\x94 ") << signedDb (m - r) << " dB";

            const auto font = theme.font (11.0f, true);
            const auto w = juce::GlyphArrangement::getStringWidth (font, text) + theme.px (16.0f);
            auto box = juce::Rectangle<float> (w, theme.px (20.0f))
                           .withPosition (mouse.x + theme.px (10.0f), spectrumPlot.getY() + theme.px (2.0f));
            if (box.getRight() > plotSpan.getRight())
                box.setX (mouse.x - w - theme.px (10.0f));

            g.setColour (Colours::panelRaised);
            g.fillRoundedRectangle (box, theme.px (4.0f));
            g.setColour (Colours::outline);
            g.drawRoundedRectangle (box, theme.px (4.0f), 1.0f);
            g.setColour (Colours::textPrimary);
            g.setFont (font);
            g.drawText (text, box, juce::Justification::centred);
        }
    }

    //==============================================================================
    void AnalysisPanel::paintStereo (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto& theme = getTheme (*this);
        const auto& mix = mixView();
        const auto& ref = refView();

        paintLegend (g, area.removeFromTop (theme.px (16.0f)).withTrimmedLeft (theme.px (110.0f)));
        area.removeFromTop (theme.px (4.0f));

        auto axis = area.removeFromBottom (theme.px (14.0f));
        const auto labelWidth = theme.px (110.0f);
        const auto valueWidth = theme.px (120.0f);
        auto bars = area.withTrimmedLeft (labelWidth).withTrimmedRight (valueWidth);
        const auto xFor = [&] (float width) { return bars.getX() + juce::jlimit (0.0f, 1.0f, width) * bars.getWidth(); };

        // Scale: side energy share. 0 = mono, 50 % = uncorrelated, 100 % = out of phase.
        g.setFont (theme.font (9.5f));
        for (const auto& [value, label] : { std::pair { 0.0f, "mono" }, { 0.25f, "25%" }, { 0.5f, "uncorrelated" }, { 0.75f, "75%" }, { 1.0f, "out of phase" } })
        {
            const auto x = xFor (value);
            g.setColour (Colours::grid);
            g.fillRect (x, bars.getY(), 1.0f, bars.getHeight());
            g.setColour (Colours::textMuted);
            g.drawText (label, juce::Rectangle<float> (x - 40.0f, axis.getY(), 80.0f, axis.getHeight()), juce::Justification::centred);
        }

        const auto rowHeight = bars.getHeight() / (float) numStereoBands;

        for (int b = 0; b < numStereoBands; ++b)
        {
            const auto row = juce::Rectangle<float> (area.getX(), bars.getY() + rowHeight * (float) b, area.getWidth(), rowHeight);
            const auto barHeight = juce::jmin (theme.px (8.0f), rowHeight * 0.28f);
            const auto gap = theme.px (2.0f);

            g.setColour (Colours::textPrimary);
            g.setFont (theme.font (11.5f, true));
            g.drawText (stereoBandNames[(size_t) b], row.withWidth (labelWidth).withTrimmedBottom (rowHeight * 0.45f), juce::Justification::bottomLeft);
            g.setColour (Colours::textMuted);
            g.setFont (theme.font (9.5f));
            g.drawText (frequencyLabel (stereoBandEdges[(size_t) b]) + juce::String::fromUTF8 ("\xe2\x80\x93") + frequencyLabel (stereoBandEdges[(size_t) b + 1]) + " Hz",
                        row.withWidth (labelWidth).withTrimmedTop (rowHeight * 0.55f), juce::Justification::topLeft);

            const auto centreY = row.getCentreY();
            const auto drawBar = [&] (const SpectralView& v, float y, juce::Colour colour)
            {
                if (! v.valid)
                    return;
                const auto& sb = v.stereo[(size_t) b];
                const auto bar = juce::Rectangle<float> (bars.getX(), y, juce::jmax (theme.px (2.0f), xFor (sb.width) - bars.getX()), barHeight);
                g.setColour (colour);
                g.fillRoundedRectangle (bar, juce::jmin (theme.px (4.0f), barHeight * 0.5f));
            };

            drawBar (mix, centreY - barHeight - gap * 0.5f, Colours::mixChart);
            drawBar (ref, centreY + gap * 0.5f, Colours::referenceChart);

            // Values: width and correlation for each source.
            auto values = row.withLeft (bars.getRight() + theme.px (8.0f));
            g.setFont (theme.font (10.0f));
            for (const auto* v : { &mix, &ref })
            {
                const auto line = (v == &mix) ? values.withBottom (centreY) : values.withTop (centreY);
                g.setColour (Colours::textSecondary);
                if (v->valid)
                    g.drawText (juce::String (juce::roundToInt (v->stereo[(size_t) b].width * 100.0f)) + "%   corr "
                                    + signedDb (v->stereo[(size_t) b].correlation, 2),
                                line, v == &mix ? juce::Justification::bottomLeft : juce::Justification::topLeft);
            }
        }
    }

    //==============================================================================
    void AnalysisPanel::paintDynamics (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto& theme = getTheme (*this);
        const auto& mix = snapshot.mix.dynamics;
        const auto& ref = snapshot.reference.dynamics;

        paintLegend (g, area.removeFromTop (theme.px (16.0f)).withTrimmedLeft (theme.px (4.0f)));
        area.removeFromTop (theme.px (6.0f));

        auto metrics = area.removeFromLeft (area.getWidth() * 0.44f);
        area.removeFromLeft (theme.px (18.0f));
        auto history = area;

        // Paired bars for each metric.
        struct Metric { const char* name; const char* unit; float mixValue, refValue, scaleMax; };
        const Metric list[] = {
            { "PLR", "dB", mix.plr, ref.plr, 20.0f },
            { "Crest factor", "dB", mix.crestDb, ref.crestDb, 24.0f },
            { "Loudness range", "LU", mix.loudnessRange, ref.loudnessRange, 20.0f },
        };

        const auto rowHeight = metrics.getHeight() / 3.0f;
        const auto labelWidth = theme.px (92.0f);
        const auto valueWidth = theme.px (52.0f);

        for (int i = 0; i < 3; ++i)
        {
            const auto& m = list[i];
            const auto row = metrics.withY (metrics.getY() + rowHeight * (float) i).withHeight (rowHeight);
            const auto bars = row.withTrimmedLeft (labelWidth).withTrimmedRight (valueWidth);
            const auto barHeight = juce::jmin (theme.px (8.0f), rowHeight * 0.25f);

            g.setColour (Colours::textPrimary);
            g.setFont (theme.font (11.0f, true));
            g.drawText (m.name, row.withWidth (labelWidth), juce::Justification::centredLeft);

            g.setColour (Colours::grid);
            g.fillRect (bars.getX(), row.getY() + rowHeight * 0.2f, 1.0f, rowHeight * 0.6f);

            const auto drawBar = [&] (float value, float y, juce::Colour colour)
            {
                const auto valid = std::isfinite (value);
                if (valid)
                {
                    const auto w = juce::jlimit (theme.px (2.0f), bars.getWidth(), value / m.scaleMax * bars.getWidth());
                    g.setColour (colour);
                    g.fillRoundedRectangle (juce::Rectangle<float> (bars.getX(), y, w, barHeight), juce::jmin (theme.px (4.0f), barHeight * 0.5f));
                }

                g.setColour (Colours::textSecondary);
                g.setFont (theme.font (10.0f));
                g.drawText (valid ? juce::String (value, 1) + " " + m.unit : juce::String::fromUTF8 ("\xe2\x80\x93"),
                            juce::Rectangle<float> (bars.getRight() + theme.px (6.0f), y - theme.px (3.0f), valueWidth, barHeight + theme.px (6.0f)),
                            juce::Justification::centredLeft);
            };

            drawBar (m.mixValue, row.getCentreY() - barHeight - theme.px (1.0f), Colours::mixChart);
            drawBar (m.refValue, row.getCentreY() + theme.px (1.0f), Colours::referenceChart);
        }

        // Short-term loudness over the last 60 s.
        auto xAxis = history.removeFromBottom (theme.px (14.0f));
        const auto axisWidth = theme.px (34.0f);
        auto plot = history.withTrimmedLeft (axisWidth);

        float hi = -1000.0f, lo = 1000.0f;
        for (const auto* d : { &mix, &ref })
            for (int i = 0; i < d->historyCount; ++i)
                if (std::isfinite (d->history[(size_t) i]) && d->history[(size_t) i] > -70.0f)
                {
                    hi = std::max (hi, d->history[(size_t) i]);
                    lo = std::min (lo, d->history[(size_t) i]);
                }

        if (hi < lo) { hi = -6.0f; lo = -30.0f; }
        hi = std::ceil (hi / 3.0f) * 3.0f;
        lo = std::min (hi - 12.0f, std::floor (lo / 3.0f) * 3.0f);
        const auto yFor = [&] (float v) { return juce::jmap (juce::jlimit (lo, hi, v), lo, hi, plot.getBottom(), plot.getY()); };
        const auto step = (hi - lo) > 24.0f ? 12.0f : 6.0f;

        g.setFont (theme.font (9.5f));
        for (float v = hi; v >= lo - 0.1f; v -= step)
        {
            g.setColour (Colours::grid);
            g.fillRect (plot.getX(), yFor (v), plot.getWidth(), 1.0f);
            g.setColour (Colours::textMuted);
            g.drawText (plainDb (v, 0), juce::Rectangle<float> (history.getX(), yFor (v) - 6.0f, axisWidth - 4.0f, 12.0f), juce::Justification::centredRight);
        }

        g.setColour (Colours::textMuted);
        g.drawText ("short-term LUFS, last 60 s", xAxis.withTrimmedLeft (axisWidth), juce::Justification::centredLeft);
        g.drawText ("now", xAxis, juce::Justification::centredRight);

        std::vector<juce::Point<float>> points;
        for (const auto* d : { &ref, &mix })
        {
            points.clear();
            for (int i = 0; i < d->historyCount; ++i)
            {
                // Newest value at the right edge.
                const auto age = (float) (d->historyCount - 1 - i);
                const auto x = plot.getRight() - age / (float) (historyLength - 1) * plot.getWidth();
                const auto v = d->history[(size_t) i];
                points.push_back ({ x, std::isfinite (v) && v > -70.0f ? yFor (v) : std::numeric_limits<float>::quiet_NaN() });
            }

            strokeCurve (g, points, d == &mix ? Colours::mixChart : Colours::referenceChart, theme.px (2.0f));
        }
    }

    //==============================================================================
    void AnalysisPanel::paintHints (juce::Graphics& g, juce::Rectangle<float> area, const juce::StringArray& hints)
    {
        auto& theme = getTheme (*this);

        g.setColour (Colours::background.withAlpha (0.5f));
        g.fillRoundedRectangle (area, theme.px (6.0f));
        area = area.reduced (theme.px (10.0f), theme.px (8.0f));

        g.setColour (Colours::textMuted);
        g.setFont (theme.font (10.0f, true).withExtraKerningFactor (0.1f));
        g.drawText ("WHAT'S DIFFERENT", area.removeFromTop (theme.px (16.0f)), juce::Justification::centredLeft);
        area.removeFromTop (theme.px (4.0f));

        for (int i = 0; i < hints.size() && area.getHeight() > theme.px (12.0f); ++i)
        {
            juce::AttributedString text;
            text.append (hints[i], theme.font (11.5f, i == 0 && hints.size() > 1 && hints[i].startsWith ("Overall")),
                         Colours::textPrimary);
            text.setWordWrap (juce::AttributedString::byWord);

            juce::TextLayout layout;
            const auto bulletWidth = theme.px (12.0f);
            layout.createLayout (text, area.getWidth() - bulletWidth);
            const auto h = std::min (layout.getHeight(), area.getHeight());

            g.setColour (Colours::textMuted);
            g.fillEllipse (area.getX() + theme.px (1.0f), area.getY() + theme.px (5.5f), theme.px (4.0f), theme.px (4.0f));
            layout.draw (g, area.withTrimmedLeft (bulletWidth).withHeight (h));
            area.removeFromTop (h + theme.px (7.0f));
        }
    }

    //==============================================================================
    void AnalysisPanel::mouseMove (const juce::MouseEvent& e)
    {
        mouse = e.position;
        if (tab == Tab::tonal)
            repaint (chartArea);
    }

    void AnalysisPanel::mouseExit (const juce::MouseEvent&)
    {
        mouse = { -1.0f, -1.0f };
        repaint (chartArea);
    }
}
