#include "StepPopup.h"
#include "Theme.h"
#include "Core/AudioFileLoader.h"
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

    // Which sample this hit plays: the track's, or its own (drop a file on the step, or load one).
    sampleLabel.setText ("Sample", juce::dontSendNotification);
    sampleLabel.setFont (font (13.0f));
    sampleLabel.setColour (juce::Label::textColourId, Colours::textDim);
    sampleName.setFont (font (12.5f));
    sampleName.setMinimumHorizontalScale (0.7f);

    loadSample.setTooltip ("Give this hit its own sample (you can also drop a file onto the step)");
    loadSample.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Sample for this hit", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       supportedAudioWildcard());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<StepPopup> (this)] (const juce::FileChooser& fc) {
                                  if (safe == nullptr || ! fc.getResult().existsAsFile())
                                      return;

                                  if (safe->state.processor.assignHitSample (safe->track, safe->step, SampleRef::fromFile (fc.getResult())) < 0
                                      && safe->state.showStatus)
                                      safe->state.showStatus ("This track already uses 15 different per-hit samples");

                                  safe->refreshSample();
                                  if (safe->onChange) safe->onChange();
                              });
    };

    useTrackSample.setTooltip ("Play the track's sample on this hit again");
    useTrackSample.onClick = [this] {
        state.processor.clearHitSample (track, step);
        refreshSample();
        if (onChange) onChange();
    };

    for (auto* c : std::initializer_list<juce::Component*> { &sampleLabel, &sampleName, &loadSample, &useTrackSample })
        addAndMakeVisible (c);

    refreshSample();
    setSize (300, (int) rows.size() * 28 + 46 + 30);
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

void StepPopup::refreshSample()
{
    const auto& st = state.processor.getModel().bank.patterns[(size_t) state.processor.getEditPattern()]
                         .tracks[(size_t) track].steps[(size_t) step];
    const auto& mgr = state.processor.getSampleManager();

    if (st.sample > 0)
    {
        const bool missing = mgr.hasWarning (track, st.sample);
        sampleName.setText (mgr.getRef (track, st.sample).getDisplayName() + (missing ? " (missing)" : ""), juce::dontSendNotification);
        sampleName.setColour (juce::Label::textColourId, missing ? Colours::warning : Colours::text);
        sampleName.setTooltip (missing ? mgr.getWarning (track, st.sample) + "\nThis hit plays the track's sample instead."
                                       : mgr.getRef (track, st.sample).file.getFullPathName());
    }
    else
    {
        sampleName.setText ("Track: " + mgr.getRef (track).getDisplayName(), juce::dontSendNotification);
        sampleName.setColour (juce::Label::textColourId, Colours::textDim);
        sampleName.setTooltip ({});
    }

    useTrackSample.setEnabled (st.sample > 0);
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

    auto sampleRow = r.removeFromTop (28).reduced (0, 3);
    sampleLabel.setBounds (sampleRow.removeFromLeft (80));
    useTrackSample.setBounds (sampleRow.removeFromRight (64));
    sampleRow.removeFromRight (2);
    loadSample.setBounds (sampleRow.removeFromRight (54));
    sampleName.setBounds (sampleRow);

    clearButton.setBounds (r.removeFromBottom (22).removeFromRight (100));
}

} // namespace bf::ui
