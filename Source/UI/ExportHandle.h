#pragma once

#include "EditorState.h"
#include "Plugin/PluginProcessor.h"

namespace bf::ui
{

/** Drag source for exporting the pattern into the DAW. Pressing starts a background render
    (MIDI, mix or stems at the host tempo and rate, with swing and humanize); dragging then hands
    the files to the host. The small arrow chooses what to export. */
class ExportHandle final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    explicit ExportHandle (EditorState&);
    ~ExportHandle() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    using Kind = BreakForgeProcessor::ExportKind;

private:
    void showMenu();
    void startRender();
    juce::String kindName() const;
    juce::Rectangle<int> menuArea() const { return getLocalBounds().removeFromRight (18); }

    EditorState& state;
    Kind kind = Kind::Midi;
    int bars = 4;
    bool rendering = false;
    bool mouseIsDown = false;
    bool dragStarted = false;
    juce::Array<juce::File> files;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace bf::ui
