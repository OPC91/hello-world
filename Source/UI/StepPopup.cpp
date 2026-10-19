#include "StepPopup.h"
#include "Theme.h"
#include "Plugin/PluginProcessor.h"

namespace bf::ui
{

StepPopup::StepPopup (EditorState& s, int t, int st, std::function<void()> changed)
    : state (s), track (t), step (st), onChange (std::move (changed))
{
    const auto& cur = state.processor.getModel().bank.patterns[(size_t) state.processor.getEditPattern()]
                          .tracks[(size_t) track].steps[(size_t) step];

    addRow ("Velocity", 1, 127, 1, cur.velocity, [] (Step& x, double v) { x.velocity = (std::uint8_t) v; });
    addRow ("Probability", 0, 100, 1, cur.probability, [] (Step& x, double v) { x.probability = (std::uint8_t) v; }, " %");
    addRow ("Ratchet", 1, 4, 1, cur.ratchet, [] (Step& x, double v) { x.ratchet = (std::uint8_t) v; }, "x");
    addRow ("Nudge", -50, 50, 1, std::round (cur.nudge * 100.0), [] (Step& x, double v) { x.nudge = (float) v * 0.01f; }, " %");
    addRow ("Pitch", -12, 12, 1, cur.pitch, [] (Step& x, double v) { x.pitch = (std::int8_t) v; }, " st");
    addRow ("Length", 0.1, 16, 0.05, cur.length, [] (Step& x, double v) { x.length = (float) v; }, " steps");

    clearButton.onClick = [this] {
        const int slot = state.processor.getEditPattern();
        state.processor.editModel ([&] (SequencerSnapshot& m) { m.bank.patterns[(size_t) slot].tracks[(size_t) track].steps[(size_t) step] = Step {}; });
        if (onChange) onChange();
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };
    addAndMakeVisible (clearButton);

    setSize (300, (int) rows.size() * 28 + 46);
}

void StepPopup::addRow (const juce::String& name, double min, double max, double interval, double value,
                        std::function<void (Step&, double)> apply, const juce::String& suffix)
{
    auto row = std::make_unique<Row>();
    row->label.setText (name, juce::dontSendNotification);
    row->label.setFont (font (13.0f));
    row->label.setColour (juce::Label::textColourId, Colours::textDim);
    row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
    row->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    row->slider.setRange (min, max, interval);
    row->slider.setValue (value, juce::dontSendNotification);
    row->slider.setTextValueSuffix (suffix);
    row->slider.setColour (juce::Slider::trackColourId, Colours::track (track));
    row->slider.setColour (juce::Slider::thumbColourId, Colours::track (track));
    row->slider.onValueChange = [this, apply, s = &row->slider] {
        const int slot = state.processor.getEditPattern();
        const double v = s->getValue();
        state.processor.editModel ([&] (SequencerSnapshot& m) {
            apply (m.bank.patterns[(size_t) slot].tracks[(size_t) track].steps[(size_t) step], v);
        }, firstEdit);
        firstEdit = false;
        if (onChange) onChange();
    };

    addAndMakeVisible (row->label);
    addAndMakeVisible (row->slider);
    rows.push_back (std::move (row));
}

void StepPopup::paint (juce::Graphics& g)
{
    g.setColour (Colours::text);
    g.setFont (font (13.0f, true));
    g.drawText (state.processor.getTrackSettings()[(size_t) track].name + "  -  step " + juce::String (step + 1),
                getLocalBounds().removeFromTop (22).reduced (4, 0), juce::Justification::centredLeft);
}

void StepPopup::resized()
{
    auto r = getLocalBounds().reduced (4);
    r.removeFromTop (20);

    for (auto& row : rows)
    {
        auto line = r.removeFromTop (28);
        row->label.setBounds (line.removeFromLeft (80));
        row->slider.setBounds (line);
    }

    clearButton.setBounds (r.removeFromBottom (22).removeFromRight (100));
}

} // namespace bf::ui
