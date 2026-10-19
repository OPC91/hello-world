#include "TopBar.h"
#include "Core/PatternTools.h"
#include "Core/StateIO.h"
#include "Core/UserLibrary.h"
#include "Plugin/PluginProcessor.h"

namespace bf::ui
{

namespace
{
    void setDefaultOnDoubleClick (juce::Slider& s, juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        if (auto* p = apvts.getParameter (id))
            s.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }
}

// PatternSelector ----------------------------------------------------------------

PatternSelector::PatternSelector (EditorState& s) : state (s)
{
    setTooltip ("Patterns 1-16 (MIDI notes C1-D#2 switch them). Right-click for slot tools.");
}

juce::Rectangle<float> PatternSelector::cell (int index) const
{
    const float w = (float) getWidth() / 8.0f, h = (float) getHeight() / 2.0f;
    return { (index % 8) * w, (index / 8) * h, w, h };
}

void PatternSelector::paint (juce::Graphics& g)
{
    const int active = state.processor.getEditPattern();
    const bool playing = state.processor.getEngine().getPlayheadStep() >= 0.0f;

    for (int i = 0; i < kNumPatterns; ++i)
    {
        const auto r = cell (i).reduced (1.5f);
        const bool empty = state.processor.getModel().bank.patterns[(size_t) i].isEmpty();
        const bool isActive = i == active;

        g.setColour (isActive ? Colours::accent : empty ? Colours::cellOff : Colours::panelRaised.brighter (0.15f));
        g.fillRoundedRectangle (r, 3.0f);

        if (isActive && playing)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawRoundedRectangle (r, 3.0f, 1.2f);
        }

        g.setColour (isActive ? juce::Colours::black : empty ? Colours::textDim : Colours::text);
        g.setFont (font (11.0f, true));
        g.drawText (juce::String (i + 1), r, juce::Justification::centred);
    }
}

void PatternSelector::mouseDown (const juce::MouseEvent& e)
{
    int index = -1;
    for (int i = 0; i < kNumPatterns; ++i)
        if (cell (i).contains (e.position))
            index = i;

    if (index < 0)
        return;

    if (! e.mods.isPopupMenu())
    {
        state.processor.selectPattern (index);
        repaint();
        return;
    }

    juce::PopupMenu menu, copyTo;
    for (int i = 0; i < kNumPatterns; ++i)
        if (i != index)
            copyTo.addItem (100 + i, "Slot " + juce::String (i + 1));

    menu.addSubMenu ("Copy slot " + juce::String (index + 1) + " to", copyTo);
    menu.addItem (1, "Clear slot " + juce::String (index + 1));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, index] (int r) {
        if (r == 0)
            return;

        if (r == 1)
            state.processor.editModel ([index] (SequencerSnapshot& m) { m.bank.patterns[(size_t) index] = Pattern {}; });
        else if (r >= 100)
            state.processor.editModel ([index, r] (SequencerSnapshot& m) { m.bank.patterns[(size_t) (r - 100)] = m.bank.patterns[(size_t) index]; });
        repaint();
    });
}

// TopBar ------------------------------------------------------------------------

TopBar::TopBar (EditorState& s) : state (s), patterns (s), exportHandle (s)
{
    auto& proc = state.processor;
    auto& apvts = proc.apvts;

    subStyle.addItemList (subStyleNames(), 1);
    subStyle.setTooltip ("DnB sub-style for Generate, per-track Gen and Fill");
    subStyleAttachment = std::make_unique<ComboAttachment> (apvts, ParamID::subStyle, subStyle);

    generate.setColour (juce::TextButton::buttonColourId, Colours::accent);
    generate.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    generate.setTooltip ("Generate a full pattern in the selected sub-style (locked tracks are kept)");
    generate.onClick = [this] { state.processor.generatePattern(); };

    fill.setTooltip ("Write a one-bar fill based on this pattern into the next slot");
    fill.onClick = [this] {
        const int slot = state.processor.generateFill();
        if (state.showStatus)
            state.showStatus ("Fill written to pattern " + juce::String (slot + 1) + " (trigger it with MIDI note " + juce::MidiMessage::getMidiNoteName (36 + slot, true, true, 3) + ")");
    };

    groove.addItemList (grooveNames(), 1);
    groove.setTooltip ("Groove template: sets swing plus per-step timing and velocity feel");
    groove.onChange = [this] { state.processor.applyGrooveTemplate (groove.getSelectedId() - 1); };
    grooveMenu.setTooltip ("Save / load user grooves");
    grooveMenu.onClick = [this] { showGrooveMenu(); };

    reroll.setTooltip ("New random seed for humanize and probability (playback stays repeatable until you re-roll)");
    reroll.onClick = [this] {
        state.processor.rerollHumanize();
        if (state.showStatus) state.showStatus ("Humanize re-rolled");
    };

    kit.setTooltip ("Kit: built-in synthesized kits plus folders in your Kits folder");
    kit.onChange = [this] {
        const auto name = kit.getText();
        if (name.isNotEmpty() && name != state.processor.getCurrentKit())
            state.processor.selectKit (name);
    };
    randomKit.setTooltip ("Pick random sounds from the current kit (sample-locked tracks are kept)");
    randomKit.onClick = [this] { state.processor.randomizeAllSamples(); };
    kitMenu.onClick = [this] { showKitMenu(); };

    driveOn.setClickingTogglesState (true);
    limiter.setClickingTogglesState (true);
    driveOn.setTooltip ("Drive on/off");
    limiter.setTooltip ("Transparent look-ahead limiter on the main output");
    driveOnAttachment = std::make_unique<ButtonAttachment> (apvts, ParamID::driveOn, driveOn);
    limiterAttachment = std::make_unique<ButtonAttachment> (apvts, ParamID::limiterOn, limiter);

    const std::pair<Knob*, const char*> knobs[] = {
        { &variation, ParamID::variation }, { &swing, ParamID::swing }, { &humTime, ParamID::humanizeTime },
        { &humVel, ParamID::humanizeVel }, { &drive, ParamID::drive },
    };

    for (auto& [k, id] : knobs)
    {
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (apvts, id, k->slider));
        setDefaultOnDoubleClick (k->slider, apvts, id);
        addAndMakeVisible (*k);
    }

    variation.slider.setTooltip ("How far Generate strays from the core pattern");
    swing.slider.setTooltip ("Global swing on off-beat 16ths (tempo-relative)");

    for (auto* c : std::initializer_list<juce::Component*> { &patterns, &subStyle, &generate, &fill, &groove, &grooveMenu, &reroll,
                                                             &kit, &randomKit, &kitMenu, &driveOn, &limiter, &exportHandle })
        addAndMakeVisible (c);

    proc.kitsChanged.addChangeListener (this);
    rebuildKitList();
    refresh();
}

TopBar::~TopBar()
{
    state.processor.kitsChanged.removeChangeListener (this);
}

void TopBar::rebuildKitList()
{
    kit.clear (juce::dontSendNotification);
    int id = 1, selected = 0;

    bool separated = false;

    for (const auto& k : state.processor.getKits())
    {
        if (! k.builtIn && ! separated)
        {
            kit.addSeparator(); // user kits below the built-in ones
            separated = true;
        }

        kit.addItem (k.name, id);
        if (k.name == state.processor.getCurrentKit())
            selected = id;
        ++id;
    }

    kit.setSelectedId (selected, juce::dontSendNotification);
}

void TopBar::refresh()
{
    // Follow automation of the groove choice.
    const int g = (int) state.processor.getParameterValue (ParamID::groove) + 1;
    if (groove.getSelectedId() != g)
        groove.setSelectedId (g, juce::dontSendNotification);

    patterns.repaint();
}

void TopBar::showGrooveMenu()
{
    juce::PopupMenu menu, load;
    const auto files = UserLibrary::listGrooves();

    for (int i = 0; i < files.size(); ++i)
        load.addItem (100 + i, files[i].getFileNameWithoutExtension());

    menu.addItem (1, "Save current groove as...");
    menu.addSubMenu ("Load user groove", load, ! files.isEmpty());
    menu.addItem (2, "Load groove file...");
    menu.addItem (3, "Reveal grooves folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&grooveMenu), [this, files] (int r) {
        if (r == 0)
            return;

        auto& proc = state.processor;

        if (r == 1)
        {
            auto* w = new juce::AlertWindow ("Save groove", "Name for this groove (current template + swing):", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", "My Groove");
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<TopBar> (this), w] (int result) {
                if (result == 1 && safe != nullptr)
                {
                    const auto name = w->getTextEditorContents ("name");
                    const bool ok = safe->state.processor.saveUserGroove (name);
                    if (safe->state.showStatus) safe->state.showStatus (ok ? "Groove saved: " + name : "Could not save groove");
                }
            }), true);
        }
        else if (r == 2)
        {
            chooser = std::make_unique<juce::FileChooser> ("Load groove", UserLibrary::getGroovesFolder(), "*.bfgroove");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
                if (fc.getResult().existsAsFile())
                    state.processor.loadUserGroove (fc.getResult());
            });
        }
        else if (r == 3)
        {
            UserLibrary::getGroovesFolder().createDirectory();
            UserLibrary::getGroovesFolder().startAsProcess();
        }
        else if (r >= 100 && r - 100 < files.size())
        {
            proc.loadUserGroove (files[r - 100]);
        }
    });
}

void TopBar::showKitMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Rescan Kits folder");
    menu.addItem (2, "Open Kits folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kitMenu), [this] (int r) {
        if (r == 1)
        {
            state.processor.rescanKits();
            if (state.showStatus) state.showStatus ("Scanning " + UserLibrary::getKitsFolder().getFullPathName());
        }
        else if (r == 2)
        {
            UserLibrary::getKitsFolder().createDirectory();
            UserLibrary::getKitsFolder().startAsProcess();
        }
    });
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    g.setColour (Colours::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    auto logo = getLocalBounds().removeFromLeft (100).reduced (10, 0);
    g.setColour (Colours::accent);
    g.setFont (font (18.0f, true));
    g.drawText ("BREAK", logo.removeFromTop (getHeight() / 2 + 4).withTrimmedTop (8), juce::Justification::bottomLeft);
    g.setColour (Colours::text);
    g.drawText ("FORGE", logo.withTrimmedBottom (8), juce::Justification::topLeft);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    r.removeFromLeft (96);
    const int h = r.getHeight();
    const int comboH = 26;
    const auto vcentre = [] (juce::Rectangle<int> b, int height) { return b.withSizeKeepingCentre (b.getWidth(), height); };

    // Left: patterns and the generator.
    patterns.setBounds (r.removeFromLeft (176));
    r.removeFromLeft (10);
    subStyle.setBounds (vcentre (r.removeFromLeft (104), comboH));
    r.removeFromLeft (6);
    generate.setBounds (vcentre (r.removeFromLeft (100), h - 10));
    r.removeFromLeft (2);
    variation.setBounds (r.removeFromLeft (54));
    fill.setBounds (vcentre (r.removeFromLeft (40), comboH));
    r.removeFromLeft (12);

    // Right: export, master section and kit.
    exportHandle.setBounds (vcentre (r.removeFromRight (112), h - 12));
    r.removeFromRight (8);
    limiter.setBounds (vcentre (r.removeFromRight (46), comboH));
    r.removeFromRight (2);
    driveOn.setBounds (vcentre (r.removeFromRight (34), comboH));
    drive.setBounds (r.removeFromRight (48));
    r.removeFromRight (8);

    auto kitArea = r.removeFromRight (juce::jlimit (150, 220, r.getWidth() / 3));
    kitMenu.setBounds (vcentre (kitArea.removeFromRight (26), comboH));
    kitArea.removeFromRight (2);
    randomKit.setBounds (vcentre (kitArea.removeFromRight (58), comboH));
    kitArea.removeFromRight (2);
    kit.setBounds (vcentre (kitArea, comboH));
    r.removeFromRight (10);

    // Middle: groove, swing and humanize share what is left.
    const int knobW = juce::jlimit (44, 56, (r.getWidth() - 186) / 3);
    reroll.setBounds (vcentre (r.removeFromRight (56), comboH));
    humVel.setBounds (r.removeFromRight (knobW));
    humTime.setBounds (r.removeFromRight (knobW));
    swing.setBounds (r.removeFromRight (knobW));
    r.removeFromRight (4);
    grooveMenu.setBounds (vcentre (r.removeFromRight (26), comboH));
    r.removeFromRight (2);
    groove.setBounds (vcentre (r, comboH));
}

// PatternBar --------------------------------------------------------------------

PatternBar::PatternBar (EditorState& s) : state (s)
{
    auto& proc = state.processor;
    auto& apvts = proc.apvts;

    for (auto* b : { &len16, &len32, &res16, &res32, &gridView, &rollView })
        b->setClickingTogglesState (false);

    len16.setTooltip ("16-step pattern");
    len32.setTooltip ("32-step pattern");
    res16.setTooltip ("Steps are 16th notes");
    res32.setTooltip ("Steps are 32nd notes (fast hat rolls)");

    len16.onClick = [this] { editPattern ([] (Pattern& p) { PatternTools::setLengthAndResolution (p, 16, p.resolution); }); };
    len32.onClick = [this] { editPattern ([] (Pattern& p) { PatternTools::setLengthAndResolution (p, 32, p.resolution); }); };
    // Changing resolution keeps the pattern's musical length where it fits.
    res16.onClick = [this] { editPattern ([] (Pattern& p) {
        if (p.resolution != Resolution::Sixteenth)
            PatternTools::setLengthAndResolution (p, 16, Resolution::Sixteenth);
    }); };
    res32.onClick = [this] { editPattern ([] (Pattern& p) {
        if (p.resolution != Resolution::ThirtySecond)
            PatternTools::setLengthAndResolution (p, 32, Resolution::ThirtySecond);
    }); };

    shiftLeft.setTooltip ("Shift pattern one step left");
    shiftRight.setTooltip ("Shift pattern one step right");
    shiftLeft.onClick = [this] { editPattern ([] (Pattern& p) { PatternTools::shift (p, -1); }); };
    shiftRight.onClick = [this] { editPattern ([] (Pattern& p) { PatternTools::shift (p, 1); }); };

    copy.setTooltip ("Copy pattern (Ctrl/Cmd+C)");
    paste.setTooltip ("Paste pattern (Ctrl/Cmd+V)");
    clear.setTooltip ("Clear pattern");
    copy.onClick = [this] { copyPattern(); };
    paste.onClick = [this] { pastePattern(); };
    clear.onClick = [this] { editPattern ([] (Pattern& p) { p.clear(); }); };

    ghosts.setTooltip ("Add low-velocity ghost snares in classic DnB positions");
    ghosts.onClick = [this] { state.processor.addGhostNotes(); };
    undo.onClick = [this] { state.processor.undo(); };
    redo.onClick = [this] { state.processor.redo(); };
    undo.setTooltip ("Undo (Ctrl/Cmd+Z)");
    redo.setTooltip ("Redo (Ctrl/Cmd+Shift+Z)");

    gridView.onClick = [this] { state.setPianoRoll (false); refresh(); };
    rollView.onClick = [this] { state.setPianoRoll (true); refresh(); };

    for (auto* b : { &swing8, &midiOut, &multiOut })
        b->setClickingTogglesState (true);

    swing8.setTooltip ("Also swing off-beat 8ths");
    midiOut.setTooltip ("Send the sequence on the plugin's MIDI output (to drive other drum plugins)");
    multiOut.setTooltip ("Route each track to its own stereo output bus (enable the buses in your DAW)");
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (apvts, ParamID::swing8, swing8));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (apvts, ParamID::midiOut, midiOut));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (apvts, ParamID::multiOut, multiOut));

    midiMode.addItemList ({ "Pattern MIDI: Latch", "Pattern MIDI: Hold", "Pattern MIDI: Off" }, 1);
    midiMode.setTooltip ("How MIDI notes C1-D#2 select patterns. Hold plays a pattern only while its note is held.");
    midiModeAttachment = std::make_unique<ComboAttachment> (apvts, ParamID::midiPatternMode, midiMode);

    for (auto& [k, id] : { std::pair<Knob*, const char*> { &accent, ParamID::accentAmount }, { &velDepth, ParamID::velocityDepth },
                           { &master, ParamID::masterVolume } })
    {
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (apvts, id, k->slider));
        setDefaultOnDoubleClick (k->slider, apvts, id);
        addAndMakeVisible (*k);
    }

    accent.slider.setTooltip ("How much the accent row boosts velocity");
    velDepth.slider.setTooltip ("How strongly velocity changes the volume");

    for (auto* c : std::initializer_list<juce::Component*> { &len16, &len32, &res16, &res32, &shiftLeft, &shiftRight, &copy, &paste,
                                                             &clear, &ghosts, &undo, &redo, &gridView, &rollView, &swing8,
                                                             &midiOut, &multiOut, &midiMode })
        addAndMakeVisible (c);

    refresh();
}

void PatternBar::editPattern (const std::function<void (Pattern&)>& fn)
{
    const int slot = state.processor.getEditPattern();
    state.processor.editModel ([&] (SequencerSnapshot& m) { fn (m.bank.patterns[(size_t) slot]); });
    refresh();
}

void PatternBar::copyPattern()
{
    const auto& p = state.processor.getModel().bank.patterns[(size_t) state.processor.getEditPattern()];
    juce::SystemClipboard::copyTextToClipboard (StateIO::patternToString (p));
    setStatus ("Pattern " + juce::String (state.processor.getEditPattern() + 1) + " copied");
}

void PatternBar::pastePattern()
{
    Pattern p;

    if (StateIO::patternFromString (juce::SystemClipboard::getTextFromClipboard(), p))
        editPattern ([&p] (Pattern& target) { target = p; });
    else
        setStatus ("Clipboard has no BreakForge pattern");
}

void PatternBar::refresh()
{
    const auto& p = state.processor.getModel().bank.patterns[(size_t) state.processor.getEditPattern()];
    len16.setToggleState (p.length == 16, juce::dontSendNotification);
    len32.setToggleState (p.length == 32, juce::dontSendNotification);
    res16.setToggleState (p.resolution == Resolution::Sixteenth, juce::dontSendNotification);
    res32.setToggleState (p.resolution == Resolution::ThirtySecond, juce::dontSendNotification);
    gridView.setToggleState (! state.isPianoRoll(), juce::dontSendNotification);
    rollView.setToggleState (state.isPianoRoll(), juce::dontSendNotification);
    undo.setEnabled (state.processor.canUndo());
    redo.setEnabled (state.processor.canRedo());

    if (status.isNotEmpty() && juce::Time::getMillisecondCounter() - statusTime > 5000)
    {
        status.clear();
        repaint();
    }
}

void PatternBar::setStatus (const juce::String& s)
{
    status = s;
    statusTime = juce::Time::getMillisecondCounter();
    repaint();
}

void PatternBar::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel.darker (0.15f));
    g.setColour (Colours::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    if (status.isNotEmpty())
    {
        g.setColour (Colours::accentAlt);
        g.setFont (font (12.5f));
        g.drawFittedText (status, getLocalBounds().reduced (10, 0).withLeft (midiMode.getRight() + 12).withRight (master.getX() - 6),
                          juce::Justification::centredLeft, 2);
    }
}

void PatternBar::resized()
{
    auto r = getLocalBounds().reduced (8, 5);
    const int bh = 24;
    const auto place = [&] (juce::Component& c, int w, int gapAfter = 2) {
        c.setBounds (r.removeFromLeft (w).withSizeKeepingCentre (w, bh));
        r.removeFromLeft (gapAfter);
    };

    place (len16, 32, 0);
    place (len32, 32, 8);
    place (res16, 40, 0);
    place (res32, 40, 10);
    place (shiftLeft, 24, 0);
    place (shiftRight, 24, 10);
    place (copy, 46);
    place (paste, 46);
    place (clear, 46, 10);
    place (ghosts, 62, 10);
    place (undo, 46);
    place (redo, 46, 14);
    place (gridView, 46, 0);
    place (rollView, 76, 14);

    accent.setBounds (r.removeFromLeft (48));
    velDepth.setBounds (r.removeFromLeft (52));
    r.removeFromLeft (10);
    place (swing8, 78, 10);
    place (midiOut, 66);
    place (multiOut, 70, 6);
    place (midiMode, 150, 0);
    master.setBounds (getLocalBounds().removeFromRight (64).reduced (6, 2));
}

} // namespace bf::ui
