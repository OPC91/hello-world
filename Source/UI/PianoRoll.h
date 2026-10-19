#pragma once

#include "StepGrid.h"

namespace bf::ui
{

/** Detailed timing view: notes sit at their real (nudged) position and show their length.
    Drag a note to move it off-grid (it can hop to neighbouring steps), drag its right edge to
    change the length, double-click to add or delete, and edit velocity in the lane below. */
class PianoRoll final : public PatternView
{
public:
    explicit PianoRoll (EditorState&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Mode { None, Move, Resize, Lane };

    float cellWidth() const;
    juce::Rectangle<float> noteBounds (int track, int step) const;
    /** Note under the mouse: {track, step} or {-1, -1}. */
    std::pair<int, int> noteAt (juce::Point<int>) const;
    bool onResizeEdge (juce::Point<int>, int track, int step) const;

    Mode mode = Mode::None;
    int dragTrack = -1, dragStep = -1;
    float dragStartPos = 0.0f;   // in steps (step + nudge)
    float dragStartLength = 1.0f;
};

} // namespace bf::ui
