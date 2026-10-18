#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace benchmark
{
    class BenchmarkProcessor;
}

namespace benchmark::ui
{
    /** Band solo selector (Off / Low / Low-mid / High-mid / High) and the linear-phase switch. */
    class BandSoloBar final : public juce::Component,
                              public juce::SettableTooltipClient
    {
    public:
        explicit BandSoloBar (BenchmarkProcessor& processor);
        ~BandSoloBar() override;

        /** Keyboard: 1-4 toggle a band, 0 turns solo off. */
        void toggleBand (int band);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> getSegmentsArea() const;

        juce::RangedAudioParameter& bandParam;
        juce::ParameterAttachment bandAttachment;
        int selected = 0;

        juce::ToggleButton linearToggle { "Linear phase" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linearAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandSoloBar)
    };
}
