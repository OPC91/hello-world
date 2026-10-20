#pragma once

#include "EditorState.h"
#include "Core/Snapshot.h"

namespace bf::ui
{

/** Common base for the two pattern views: model access, edit grouping, the velocity lane and
    dropping samples onto individual hits. */
class PatternView : public juce::Component,
                    public juce::FileDragAndDropTarget
{
public:
    explicit PatternView (EditorState& s) : state (s) {}

    void setLayout (const RowLayout& l) { layout = l; repaint(); }
    void setPlayhead (float step);

    // Dropping audio files on a step makes that hit play the file; several files fill the
    // following steps in name order (handy for the slices of a chopped break).
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override { fileDragMove (files, x, y); }
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

protected:
    const Pattern& pattern() const;

    /** Applies an edit to the edit pattern. The first edit of a mouse gesture is undoable;
        the rest of the gesture is merged into it. */
    void edit (const std::function<void (Pattern&)>& fn);
    void beginGesture() noexcept { firstEditOfGesture = true; }

    void paintPlayhead (juce::Graphics&, float x0, float cellWidth);
    void paintBackground (juce::Graphics&, float cellWidth);
    void paintLaneFrame (juce::Graphics&);
    void paintBeatDividers (juce::Graphics&, float cellWidth);
    juce::Colour offCellColour (int step) const;
    void setVelocityFromLane (int step, int y);
    int stepAtX (int x) const;
    void paintDropTarget (juce::Graphics&, float cellWidth);
    static void paintHitSampleMarker (juce::Graphics&, juce::Rectangle<float> cell);
    static juce::StringArray audioFilesIn (const juce::StringArray&);
    void audition (int track, int velocity);
    static int defaultVelocity (int track);

    EditorState& state;
    RowLayout layout;
    float playhead = -1.0f;
    bool firstEditOfGesture = true;
    int dropTrack = -1, dropStep = -1, dropCount = 0;
};

class StepGrid final : public PatternView
{
public:
    explicit StepGrid (EditorState&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    enum class Mode { None, Toggle, Paint, VelocityPaint, Lane, Accent, AccentPaint };

    float cellWidth() const;
    int stepAt (int x) const;
    void applyAt (int step, int y);
    void openStepPopup (int track, int step);

    Mode mode = Mode::None;
    int dragTrack = 0;
    int lastStep = -1;
    bool paintValue = true;
};

} // namespace bf::ui
