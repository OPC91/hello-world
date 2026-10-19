#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/EditorState.h"
#include "UI/PianoRoll.h"
#include "UI/StepGrid.h"
#include "UI/Theme.h"
#include "UI/TopBar.h"
#include "UI/TrackHeader.h"
#include "UI/TrackPanel.h"

class BreakForgeEditor final : public juce::AudioProcessorEditor,
                               public juce::DragAndDropContainer,
                               private juce::Timer,
                               private juce::ChangeListener
{
public:
    explicit BreakForgeEditor (BreakForgeProcessor&);
    ~BreakForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void layoutTrackArea();
    void refreshAll();

    BreakForgeProcessor& processor;
    bf::ui::LookAndFeel lookAndFeel;
    bf::ui::EditorState state { processor };

    bf::ui::TopBar topBar { state };
    bf::ui::PatternBar patternBar { state };
    std::vector<std::unique_ptr<bf::ui::TrackHeader>> headers;
    bf::ui::StepGrid grid { state };
    bf::ui::PianoRoll pianoRoll { state };
    bf::ui::TrackPanel trackPanel { state };
    juce::TooltipWindow tooltips { this, 600 };

    juce::Rectangle<int> trackArea, accentHeaderArea;
    int lastEditPattern = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakForgeEditor)
};
