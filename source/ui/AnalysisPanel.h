#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "analysis/AnalysisSnapshot.h"

namespace benchmark
{
    class BenchmarkProcessor;
}

namespace benchmark::ui
{
    /**
        Comparison displays: tonal balance (spectra + difference), stereo width
        per band and dynamics, each with plain-language hints.
    */
    class AnalysisPanel final : public juce::Component,
                                private juce::Timer
    {
    public:
        explicit AnalysisPanel (BenchmarkProcessor& processor);
        ~AnalysisPanel() override;

        enum class Tab { tonal, stereo, dynamics };
        void setTab (Tab newTab);
        void setLive (bool shouldBeLive);

        /** Copies the notes as plain text: the current tab only, or all three. */
        void copyNotes (bool allSections);

        /** Plain-language feedback for the editor's message area. */
        std::function<void (const juce::String&)> onMessage;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        class Selector;

        void timerCallback() override;

        void paintTonal (juce::Graphics&, juce::Rectangle<float> area);
        void paintStereo (juce::Graphics&, juce::Rectangle<float> area);
        void paintDynamics (juce::Graphics&, juce::Rectangle<float> area);
        void paintHints (juce::Graphics&, juce::Rectangle<float> area, const juce::StringArray& hints);
        void paintLegend (juce::Graphics&, juce::Rectangle<float> area);

        juce::StringArray currentHints() const;
        const analysis::SpectralView& mixView() const;
        const analysis::SpectralView& refView() const;

        BenchmarkProcessor& processor;
        analysis::AnalysisSnapshot snapshot;
        uint32_t shownVersion = 0;

        std::unique_ptr<Selector> tabSelector, modeSelector;
        juce::TextButton resetButton { "Reset" };
        juce::TextButton copyButton { "Copy" };

        Tab tab = Tab::tonal;
        bool live = false;

        juce::Rectangle<int> headerArea, chartArea, hintsArea;
        juce::Rectangle<float> spectrumPlot, differencePlot;
        juce::Point<float> mouse { -1.0f, -1.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisPanel)
    };
}
