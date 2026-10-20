#pragma once

#include "EditorState.h"

namespace bf::ui
{

/** Call-out editor for one step: velocity, probability, ratchet, nudge, pitch and length. */
class StepPopup final : public juce::Component
{
public:
    StepPopup (EditorState&, int track, int step, std::function<void()> onChange);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        juce::Label label;
        juce::Slider slider;
    };

    void addRow (const juce::String& name, double min, double max, double interval, double value,
                 std::function<void (struct Step&, double)> apply, const juce::String& suffix = {});

    void refreshSample();

    EditorState& state;
    int track, step;
    std::function<void()> onChange;
    bool firstEdit = true;
    std::vector<std::unique_ptr<Row>> rows;
    juce::TextButton clearButton { "Clear step" };
    juce::Label sampleLabel, sampleName;
    juce::TextButton loadSample { "Load..." }, useTrackSample { "Use track" };
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace bf::ui
