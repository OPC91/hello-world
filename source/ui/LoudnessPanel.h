#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace benchmark
{
    class BenchmarkProcessor;
}

namespace benchmark::ui
{
    /** Mix / reference loudness readouts and the loudness-match offset. */
    class LoudnessPanel final : public juce::Component,
                                private juce::Timer
    {
    public:
        explicit LoudnessPanel (BenchmarkProcessor& processor);
        ~LoudnessPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;

        BenchmarkProcessor& processor;

        juce::ToggleButton matchToggle { "Match" };
        juce::TextButton resetButton { "Reset" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> matchAttachment;

        struct Snapshot
        {
            float mixShort, mixIntegrated, refShort, refIntegrated, mixGain, refGain;
            bool referenceAvailable, matchOn, fromLoop;
            bool operator== (const Snapshot&) const = default;
        };

        Snapshot shown {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessPanel)
    };
}
