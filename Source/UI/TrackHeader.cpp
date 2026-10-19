#include "TrackHeader.h"
#include "Theme.h"
#include "Core/AudioFileLoader.h"
#include "Plugin/PluginProcessor.h"

namespace bf::ui
{

SampleSlot::SampleSlot (EditorState& s, int t) : state (s), track (t)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    refresh();
}

void SampleSlot::refresh()
{
    auto& mgr = state.processor.getSampleManager();
    juce::String tip = mgr.getRef (track).isFile() ? mgr.getRef (track).file.getFullPathName() : mgr.getRef (track).getDisplayName();

    if (mgr.hasWarning (track))
        tip = "Warning: " + mgr.getWarning (track) + "\nPlaying a built-in fallback sound.";

    setTooltip (tip + "\nClick for options, or drop a WAV/AIFF/FLAC file here.");
    repaint();
}

void SampleSlot::paint (juce::Graphics& g)
{
    auto& mgr = state.processor.getSampleManager();
    const auto& settings = state.processor.getTrackSettings()[(size_t) track];
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (dragOver ? Colours::accent.withAlpha (0.25f) : Colours::background);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (dragOver ? Colours::accent : mgr.hasWarning (track) ? Colours::warning : Colours::outline);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    auto text = getLocalBounds().reduced (5, 0);

    if (mgr.hasWarning (track))
    {
        g.setColour (Colours::warning);
        g.setFont (font (13.0f, true));
        g.drawText ("!", text.removeFromLeft (8), juce::Justification::centred);
    }

    if (settings.sampleLocked)
    {
        g.setColour (Colours::accentAlt);
        g.setFont (font (10.0f, true));
        g.drawText ("L", text.removeFromRight (9), juce::Justification::centred);
    }

    auto label = mgr.getRef (track).getDisplayName();
    if (label.isEmpty()) label = "(empty)";
    if (mgr.isLoading (track)) label = "loading...";

    g.setColour (mgr.hasWarning (track) ? Colours::warning : Colours::text.withAlpha (0.85f));
    g.setFont (font (12.0f));
    g.drawFittedText (label, text, juce::Justification::centredLeft, 1, 0.8f);
}

void SampleSlot::mouseDown (const juce::MouseEvent&)
{
    state.setSelectedTrack (track);
    showMenu();
}

void SampleSlot::showMenu()
{
    auto& proc = state.processor;
    auto& settings = proc.getTrackSettings()[(size_t) track];
    juce::PopupMenu menu, kitMenu;

    // Sounds of the current kit for this track's role.
    juce::Array<SampleRef> kitSounds;

    for (const auto& kit : proc.getKits())
        if (kit.name == proc.getCurrentKit())
            kitSounds = kit.samplesFor (kTrackDefaults[(size_t) track].role);

    const auto current = proc.getSampleManager().getRef (track);

    for (int i = 0; i < kitSounds.size(); ++i)
        kitMenu.addItem (100 + i, kitSounds[i].getDisplayName(), true, kitSounds[i] == current);

    menu.addItem (1, "Load sample...");
    menu.addSubMenu ("Kit sounds (" + proc.getCurrentKit() + ")", kitMenu, ! kitSounds.isEmpty());
    menu.addItem (2, "Randomize from kit", ! kitSounds.isEmpty());
    menu.addItem (3, "Lock sample (keep on kit change / randomize)", true, settings.sampleLocked);
    menu.addSeparator();
    menu.addItem (4, "Audition");

    if (current.isFile())
        menu.addItem (5, "Show file location");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, kitSounds, current] (int result) {
                            if (result == 0)
                                return; // dismissed (also sent if this component was deleted)

                            auto& p = state.processor;
                            if (result == 1) browse();
                            else if (result == 2) p.randomizeSample (track);
                            else if (result == 3) p.getTrackSettings()[(size_t) track].sampleLocked = ! p.getTrackSettings()[(size_t) track].sampleLocked;
                            else if (result == 4) p.getEngine().audition (track, 0.9f);
                            else if (result == 5) current.file.revealToUser();
                            else if (result >= 100 && result - 100 < kitSounds.size()) p.loadSample (track, kitSounds[result - 100]);
                            refresh();
                        });
}

void SampleSlot::browse()
{
    const auto current = state.processor.getSampleManager().getRef (track);
    const auto start = current.isFile() ? current.file.getParentDirectory()
                                        : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Load a sample for " + state.processor.getTrackSettings()[(size_t) track].name,
                                                   start, supportedAudioWildcard());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  state.processor.loadSample (track, SampleRef::fromFile (f));
                              refresh();
                          });
}

bool SampleSlot::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && isSupportedAudioFile (juce::File (files[0]));
}

void SampleSlot::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;

    if (! files.isEmpty())
        state.processor.loadSample (track, SampleRef::fromFile (juce::File (files[0])));

    state.setSelectedTrack (track);
    refresh();
}

// -----------------------------------------------------------------------------

TrackHeader::TrackHeader (EditorState& s, int t) : state (s), track (t), sample (s, t)
{
    auto& proc = state.processor;

    name.setText (proc.getTrackSettings()[(size_t) track].name, juce::dontSendNotification);
    name.setFont (font (13.0f, true));
    name.setColour (juce::Label::textColourId, Colours::text);
    name.setEditable (false, true, false);
    name.setTooltip ("Double-click to rename");
    name.onTextChange = [this] {
        auto text = name.getText().trim().substring (0, 24);
        if (text.isEmpty()) text = kTrackDefaults[(size_t) track].name;
        state.processor.getTrackSettings()[(size_t) track].name = text;
        name.setText (text, juce::dontSendNotification);
    };
    name.setInterceptsMouseClicks (false, false); // single clicks select the track; double-click still edits
    addAndMakeVisible (name);

    mute.setClickingTogglesState (true);
    solo.setClickingTogglesState (true);
    mute.setColour (juce::TextButton::buttonOnColourId, Colours::mute);
    solo.setColour (juce::TextButton::buttonOnColourId, Colours::solo);
    mute.setTooltip ("Mute");
    solo.setTooltip ("Solo");
    muteAttachment = std::make_unique<ButtonAttachment> (proc.apvts, ParamID::track (track, ParamID::mute), mute);
    soloAttachment = std::make_unique<ButtonAttachment> (proc.apvts, ParamID::track (track, ParamID::solo), solo);

    generate.setTooltip ("Re-roll just this track in the current sub-style");
    generate.onClick = [this] {
        state.processor.generateTrack (track);
        state.setSelectedTrack (track);
    };

    lock.setClickingTogglesState (true);
    lock.setColour (juce::TextButton::buttonOnColourId, Colours::accentAlt);
    lock.setTooltip ("Lock: Generate keeps this track as it is");
    lock.onClick = [this] {
        state.processor.getTrackSettings()[(size_t) track].generatorLocked = lock.getToggleState();
        generate.setEnabled (! lock.getToggleState());
    };

    for (auto* c : std::initializer_list<juce::Component*> { &mute, &solo, &generate, &lock, &sample })
        addAndMakeVisible (c);

    refresh();
}

void TrackHeader::refresh()
{
    const auto& ts = state.processor.getTrackSettings()[(size_t) track];
    name.setText (ts.name, juce::dontSendNotification);
    lock.setToggleState (ts.generatorLocked, juce::dontSendNotification);
    generate.setEnabled (! ts.generatorLocked);
    sample.refresh();
    repaint();
}

void TrackHeader::decay()
{
    if (activity > 0.0f)
    {
        activity = juce::jmax (0.0f, activity - 0.12f);
        repaint (getLocalBounds().removeFromLeft (8));
    }
}

void TrackHeader::paint (juce::Graphics& g)
{
    const bool selected = state.getSelectedTrack() == track;
    g.setColour (selected ? Colours::panelRaised : Colours::panel);
    g.fillRect (getLocalBounds());

    // Colour strip doubles as the trigger LED.
    const auto colour = Colours::track (track);
    g.setColour (colour.withAlpha (0.35f + 0.65f * activity));
    g.fillRect (getLocalBounds().removeFromLeft (4).reduced (0, 3));

    g.setColour (Colours::outline.withAlpha (0.6f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TrackHeader::resized()
{
    auto r = getLocalBounds().reduced (6, 3).withTrimmedLeft (4);
    const int h = r.getHeight();
    name.setBounds (r.removeFromLeft (juce::jmax (70, r.getWidth() / 4)));
    r.removeFromLeft (2);

    const int bw = juce::jmin (h + 4, 26);
    mute.setBounds (r.removeFromLeft (bw));
    r.removeFromLeft (2);
    solo.setBounds (r.removeFromLeft (bw));
    r.removeFromLeft (4);
    generate.setBounds (r.removeFromLeft (38));
    r.removeFromLeft (2);
    lock.setBounds (r.removeFromLeft (40));
    r.removeFromLeft (4);
    sample.setBounds (r);
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    state.setSelectedTrack (track);

    if (e.getNumberOfClicks() == 2 && name.getBounds().contains (e.getPosition()))
        name.showEditor();
}

} // namespace bf::ui
