#pragma once

namespace exnilo
{
    /** The Advanced panel: one 0-150% slider per internal effect (100% = the preset's design), shared by Exnilo
        plugins. Shown over the main row when ADV is on. */
    class AdvancedPanel : public juce::Component
    {
    public:
        struct Item { juce::String parameterId, name, tooltip; };

        AdvancedPanel (juce::AudioProcessorValueTreeState&, std::vector<Item>);

        void paint (juce::Graphics&) override;
        void resized() override;
        std::function<void()> onClose;

    private:
        struct Row
        {
            juce::String name;
            juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
            juce::Label value;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        };
        juce::AudioProcessorValueTreeState& state;
        std::vector<std::unique_ptr<Row>> rows;
        juce::TextButton resetButton { "RESET ALL TO 100%" }, closeButton { "DONE" };
    };
}
