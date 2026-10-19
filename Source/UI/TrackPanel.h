#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EditorState.h"
#include "Theme.h"

namespace bf::ui
{

/** Sound controls for the selected track: level, tuning, envelope, filters, choke,
    velocity modulation, swing override and MIDI note. */
class TrackPanel final : public juce::Component,
                         private juce::ChangeListener
{
public:
    explicit TrackPanel (EditorState&);
    ~TrackPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { bindToTrack (state.getSelectedTrack()); }
    void bindToTrack (int track);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct KnobDef { const char* suffix; const char* label; };
    static constexpr KnobDef knobDefs[] = {
        { "vol", "Volume" }, { "pan", "Pan" }, { "pitch", "Pitch" }, { "fine", "Fine" }, { "start", "Start" },
        { "atk", "Attack" }, { "dec", "Decay" }, { "sus", "Sustain" }, { "rel", "Release" },
        { "hp", "High-Pass" }, { "lp", "Low-Pass" }, { "res", "Reso" },
        { "velFlt", "Vel>Filter" }, { "velPitch", "Vel>Pitch" }, { "swAmt", "Swing" },
    };

    EditorState& state;
    int boundTrack = -1;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    juce::ComboBox envMode, choke;
    std::unique_ptr<ComboAttachment> envAttachment, chokeAttachment;
    juce::ToggleButton swingOverride { "Own swing" };
    std::unique_ptr<ButtonAttachment> swingAttachment;
    juce::Slider midiNote;
    juce::Label envLabel, noteLabel;
};

} // namespace bf::ui
