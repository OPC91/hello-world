#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace benchmark::ui
{
    // Large two-segment Mix / Reference switch bound to a boolean parameter.
    class ABToggle final : public juce::Component,
                           private juce::Timer
    {
    public:
        explicit ABToggle (juce::RangedAudioParameter& listenReferenceParam);
        ~ABToggle() override;

        void toggle();
        bool isReferenceSelected() const noexcept { return isReference; }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    private:
        void timerCallback() override;
        void parameterChanged (float newValue);
        void select (bool reference);

        juce::RangedAudioParameter& param;
        juce::ParameterAttachment attachment;

        bool isReference = false;
        float highlightPosition = 0.0f; // 0 = Mix, 1 = Reference (animated)

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ABToggle)
    };
}
