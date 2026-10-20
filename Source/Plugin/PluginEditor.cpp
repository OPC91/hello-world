#include "PluginEditor.h"

using namespace bf::ui;

BreakForgeEditor::BreakForgeEditor (BreakForgeProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    for (int t = 0; t < bf::kNumTracks; ++t)
    {
        headers.push_back (std::make_unique<TrackHeader> (state, t));
        addAndMakeVisible (*headers.back());
    }

    addAndMakeVisible (topBar);
    addAndMakeVisible (patternBar);
    addAndMakeVisible (grid);
    addChildComponent (pianoRoll);
    addAndMakeVisible (trackPanel);

    state.showStatus = [this] (const juce::String& s) { patternBar.setStatus (s); };
    state.addChangeListener (this);
    processor.modelChanged.addChangeListener (this);
    processor.getSampleManager().addChangeListener (this);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (1240, 660, 2800, 1800);
    setSize (1400, 820);
    startTimerHz (30);
}

BreakForgeEditor::~BreakForgeEditor()
{
    stopTimer();
    state.removeChangeListener (this);
    processor.modelChanged.removeChangeListener (this);
    processor.getSampleManager().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void BreakForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // Header for the accent row, aligned with the grid's accent lane.
    g.setColour (Colours::panel);
    g.fillRect (accentHeaderArea);
    g.setColour (Colours::accent);
    g.setFont (font (11.5f, true));
    g.drawText ("ACCENT", accentHeaderArea.reduced (14, 0), juce::Justification::centredLeft);

    g.setColour (Colours::textDim);
    g.setFont (font (11.0f));
    g.drawText ("Drop audio on steps   Right-click: step options",
                accentHeaderArea.reduced (8, 0), juce::Justification::centredRight);
}

void BreakForgeEditor::resized()
{
    auto r = getLocalBounds();
    topBar.setBounds (r.removeFromTop (68));
    patternBar.setBounds (r.removeFromTop (46));
    trackPanel.setBounds (r.removeFromBottom (juce::jlimit (96, 130, getHeight() / 6)));
    trackArea = r.reduced (6, 6);
    layoutTrackArea();
}

void BreakForgeEditor::layoutTrackArea()
{
    auto r = trackArea;
    const auto layout = RowLayout::fit (r.getHeight(), state.getSelectedTrack());
    const int headerW = juce::jlimit (330, 430, r.getWidth() / 3);

    auto headerCol = r.removeFromLeft (headerW);
    accentHeaderArea = headerCol.withHeight (layout.accentHeight);

    for (int t = 0; t < bf::kNumTracks; ++t)
        headers[(size_t) t]->setBounds (layout.row (t, headerW).translated (headerCol.getX(), headerCol.getY()));

    r.removeFromLeft (4);
    const auto viewBounds = r.withHeight (layout.totalHeight());
    grid.setBounds (viewBounds);
    pianoRoll.setBounds (viewBounds);
    grid.setLayout (layout);
    pianoRoll.setLayout (layout);
    repaint (accentHeaderArea);
}

void BreakForgeEditor::refreshAll()
{
    for (auto& h : headers)
        h->refresh();

    patternBar.refresh();
    topBar.refresh();
    grid.repaint();
    pianoRoll.repaint();
    trackPanel.repaint();
}

void BreakForgeEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &state)
    {
        grid.setVisible (! state.isPianoRoll());
        pianoRoll.setVisible (state.isPianoRoll());
        layoutTrackArea();
    }

    refreshAll();
}

void BreakForgeEditor::timerCallback()
{
    auto& engine = processor.getEngine();
    const float step = engine.getPlayheadStep();
    grid.setPlayhead (step);
    pianoRoll.setPlayhead (step);

    // The edit pattern follows what is playing (UI clicks, automation or MIDI pattern notes).
    if (const int edit = processor.getEditPattern(); edit != lastEditPattern)
    {
        lastEditPattern = edit;
        refreshAll();
    }

    const auto triggered = engine.takeTriggeredTracks();

    for (int t = 0; t < bf::kNumTracks; ++t)
    {
        if (triggered & (1u << t))
            headers[(size_t) t]->flash();
        else
            headers[(size_t) t]->decay();
    }

    topBar.refresh();
    patternBar.refresh();
}

bool BreakForgeEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const bool cmd = mods.isCommandDown();
    const int code = key.getKeyCode();

    if (cmd && (code == 'Z' || code == 'z'))
    {
        if (mods.isShiftDown()) processor.redo();
        else                    processor.undo();
        return true;
    }

    if (cmd && (code == 'C' || code == 'c')) { patternBar.copyPattern(); return true; }
    if (cmd && (code == 'V' || code == 'v')) { patternBar.pastePattern(); return true; }

    if (cmd && (code == 'Y' || code == 'y'))
    {
        processor.redo();
        return true;
    }

    // Arrow keys move the selected track.
    if (! cmd && key == juce::KeyPress::upKey)   { state.setSelectedTrack (state.getSelectedTrack() - 1); return true; }
    if (! cmd && key == juce::KeyPress::downKey) { state.setSelectedTrack (state.getSelectedTrack() + 1); return true; }

    return false;
}
