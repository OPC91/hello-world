#include "TrackPanel.h"
#include "Plugin/PluginProcessor.h"

namespace bf::ui
{

TrackPanel::TrackPanel (EditorState& s) : state (s)
{
    for (const auto& def : knobDefs)
    {
        knobs.push_back (std::make_unique<Knob> (def.label));
        addAndMakeVisible (*knobs.back());
    }

    envMode.addItemList ({ "One-Shot", "ADSR" }, 1);
    choke.addItemList ({ "Choke: Off", "Choke: 1", "Choke: 2", "Choke: 3", "Choke: 4" }, 1);
    envMode.setTooltip ("One-Shot plays the whole sample (Attack/Decay shape it). ADSR follows the step length.");
    choke.setTooltip ("Tracks in the same choke group cut each other off (closed hat chokes open hat).");
    swingOverride.setTooltip ("Give this track its own swing amount instead of the global swing");

    midiNote.setSliderStyle (juce::Slider::IncDecButtons);
    midiNote.setRange (0, 127, 1);
    midiNote.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 20);
    midiNote.setTooltip ("Note sent on MIDI out (and used in MIDI export) for this track");
    midiNote.onValueChange = [this] {
        const int t = boundTrack;
        const int note = (int) midiNote.getValue();
        state.processor.editModel ([t, note] (SequencerSnapshot& m) { m.midiNotes[(size_t) t] = note; }, false);
    };

    for (auto* l : { &envLabel, &chokeLabel, &noteLabel })
    {
        l->setFont (font (11.5f));
        l->setColour (juce::Label::textColourId, Colours::textDim);
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    }

    envLabel.setText ("Envelope", juce::dontSendNotification);
    chokeLabel.setText ("Choke group", juce::dontSendNotification);
    noteLabel.setText ("MIDI note", juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &envMode, &choke, &swingOverride, &midiNote })
        addAndMakeVisible (c);

    state.addChangeListener (this);
    bindToTrack (state.getSelectedTrack());
}

TrackPanel::~TrackPanel()
{
    state.removeChangeListener (this);
}

void TrackPanel::bindToTrack (int track)
{
    if (track == boundTrack)
        return;

    boundTrack = track;
    auto& apvts = state.processor.apvts;
    sliderAttachments.clear();

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto id = ParamID::track (track, knobDefs[i].suffix);
        auto& slider = knobs[i]->slider;
        slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::track (track));
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (apvts, id, slider));

        if (auto* p = apvts.getParameter (id))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    envAttachment = std::make_unique<ComboAttachment> (apvts, ParamID::track (track, ParamID::envMode), envMode);
    chokeAttachment = std::make_unique<ComboAttachment> (apvts, ParamID::track (track, ParamID::choke), choke);
    swingAttachment = std::make_unique<ButtonAttachment> (apvts, ParamID::track (track, ParamID::swingOn), swingOverride);
    midiNote.setValue (state.processor.getModel().midiNotes[(size_t) track], juce::dontSendNotification);
    repaint();
}

void TrackPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    g.setColour (Colours::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    const int t = state.getSelectedTrack();
    auto title = getLocalBounds().removeFromLeft (120).reduced (10, 8);
    g.setColour (Colours::track (t));
    g.fillRoundedRectangle (title.removeFromTop (4).toFloat(), 2.0f);
    g.setColour (Colours::text);
    g.setFont (font (15.0f, true));
    g.drawFittedText (state.processor.getTrackSettings()[(size_t) t].name, title.removeFromTop (24), juce::Justification::centredLeft, 1);
    g.setColour (Colours::textDim);
    g.setFont (font (11.5f));
    g.drawFittedText ("Sound", title.removeFromTop (16), juce::Justification::centredLeft, 1);
}

void TrackPanel::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    r.removeFromLeft (114);

    auto side = r.removeFromRight (130);
    const int lineH = side.getHeight() / 4;
    envLabel.setBounds (side.removeFromTop (lineH / 2 + 4));
    envMode.setBounds (side.removeFromTop (lineH - 6).reduced (2, 0));
    choke.setBounds (side.removeFromTop (lineH).reduced (2, 3));
    swingOverride.setBounds (side.removeFromTop (lineH).reduced (2, 2));

    auto note = r.removeFromRight (96);
    noteLabel.setBounds (note.removeFromTop (note.getHeight() / 2).withTrimmedTop (8));
    midiNote.setBounds (note.removeFromTop (24).reduced (4, 0));
    chokeLabel.setVisible (false);

    const int knobW = r.getWidth() / (int) knobs.size();

    for (auto& k : knobs)
        k->setBounds (r.removeFromLeft (knobW).reduced (2, 0));
}

} // namespace bf::ui
