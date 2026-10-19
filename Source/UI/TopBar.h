#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EditorState.h"
#include "ExportHandle.h"
#include "Theme.h"

namespace bf::ui
{

/** 16 pattern slots: click to select (what plays and what you edit), right-click for slot tools. */
class PatternSelector final : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    explicit PatternSelector (EditorState&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int index) const;
    EditorState& state;
};

/** First row: patterns, generator, groove, sound, master and export. */
class TopBar final : public juce::Component,
                     private juce::ChangeListener
{
public:
    explicit TopBar (EditorState&);
    ~TopBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh(); // called from the editor timer

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuildKitList(); }
    void rebuildKitList();
    void showGrooveMenu();
    void showKitMenu();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    EditorState& state;
    PatternSelector patterns;
    juce::ComboBox subStyle, groove, kit;
    juce::TextButton generate { "GENERATE" }, fill { "Fill" }, grooveMenu { "..." }, reroll { "Re-roll" },
        randomKit { "Shuffle" }, kitMenu { "..." }, driveOn { "On" }, limiter { "Limit" };
    Knob variation { "Variation" }, swing { "Swing" }, humTime { "Hum Time" }, humVel { "Hum Vel" }, drive { "Drive" };
    ExportHandle exportHandle;

    std::unique_ptr<ComboAttachment> subStyleAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::unique_ptr<ButtonAttachment> driveOnAttachment, limiterAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
};

/** Second row: pattern editing tools, view switch and secondary settings. */
class PatternBar final : public juce::Component
{
public:
    explicit PatternBar (EditorState&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    void setStatus (const juce::String&);

private:
    void editPattern (const std::function<void (Pattern&)>&);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    EditorState& state;
    juce::TextButton len16 { "16" }, len32 { "32" }, res16 { "1/16" }, res32 { "1/32" }, shiftLeft { "<" }, shiftRight { ">" },
        copy { "Copy" }, paste { "Paste" }, clear { "Clear" }, ghosts { "+Ghosts" }, undo { "Undo" }, redo { "Redo" },
        gridView { "Grid" }, rollView { "Piano Roll" }, swing8 { "Swing 8ths" }, midiOut { "MIDI Out" }, multiOut { "Multi-Out" };
    Knob accent { "Accent" }, velDepth { "Vel Depth" }, master { "Master" };
    juce::ComboBox midiMode;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::unique_ptr<ComboAttachment> midiModeAttachment;

    juce::String status;
    juce::uint32 statusTime = 0;
};

} // namespace bf::ui
