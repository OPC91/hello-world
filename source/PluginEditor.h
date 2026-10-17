#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/ABToggle.h"
#include "ui/PlaceholderPanel.h"
#include "ui/Theme.h"

namespace benchmark
{
    class BenchmarkEditor final : public juce::AudioProcessorEditor
    {
    public:
        explicit BenchmarkEditor (BenchmarkProcessor&);
        ~BenchmarkEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void visibilityChanged() override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        BenchmarkProcessor& processor;
        ui::Theme theme;

        ui::ABToggle abToggle;
        ui::PlaceholderPanel referenceList { "References", "Drop audio files here (coming in Phase 2)" };
        ui::PlaceholderPanel waveform { "Reference Transport", "Waveform, playhead and loops (Phase 3)" };
        ui::PlaceholderPanel loudness { "Loudness", "LUFS readouts and match offset (Phase 2)" };
        ui::PlaceholderPanel analysis { "Comparison", "Tonal balance, stereo width and dynamics (Phase 4)" };

        juce::Rectangle<int> headerArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BenchmarkEditor)
    };
}
