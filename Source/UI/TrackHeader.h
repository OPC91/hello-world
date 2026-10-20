#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EditorState.h"

namespace bf::ui
{

/** Shows a track's sample and accepts dropped audio files. Click for the sample menu. */
class SampleSlot final : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         public juce::SettableTooltipClient
{
public:
    SampleSlot (EditorState&, int track);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    void refresh();

private:
    void showMenu();
    void browse();

    EditorState& state;
    const int track;
    bool dragOver = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

/** Left-hand controls for one track row. Dropping a file anywhere on it sets the track's sample. */
class TrackHeader final : public juce::Component,
                          public juce::FileDragAndDropTarget
{
public:
    TrackHeader (EditorState&, int track);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    void refresh();
    void flash() { activity = 1.0f; repaint(); }
    void decay();

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    EditorState& state;
    const int track;
    juce::Label name;
    juce::TextButton mute { "M" }, solo { "S" }, generate { "Gen" }, lock { "Lock" };
    std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;
    SampleSlot sample;
    float activity = 0.0f;
    bool dragOver = false;
};

} // namespace bf::ui
