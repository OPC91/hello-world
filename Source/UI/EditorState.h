#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Pattern.h"

class BreakForgeProcessor;

namespace bf::ui
{

/** UI-only state shared by the editor's components. */
class EditorState : public juce::ChangeBroadcaster
{
public:
    explicit EditorState (BreakForgeProcessor& p) : processor (p) {}

    BreakForgeProcessor& processor;

    int getSelectedTrack() const noexcept { return selectedTrack; }
    void setSelectedTrack (int t)
    {
        t = juce::jlimit (0, kNumTracks - 1, t);
        if (t != selectedTrack) { selectedTrack = t; sendChangeMessage(); }
    }

    bool isPianoRoll() const noexcept { return pianoRoll; }
    void setPianoRoll (bool b) { if (b != pianoRoll) { pianoRoll = b; sendChangeMessage(); } }

    /** Shows a short message in the status line. */
    std::function<void (const juce::String&)> showStatus;

private:
    int selectedTrack = 0;
    bool pianoRoll = false;
};

/** Vertical layout shared by the track headers and the grid / piano roll, so rows line up. */
struct RowLayout
{
    int accentHeight = 18;
    int rowHeight = 30;
    int laneHeight = 54;
    int selected = 0;

    juce::Rectangle<int> accentRow (int width) const { return { 0, 0, width, accentHeight }; }

    juce::Rectangle<int> row (int track, int width) const
    {
        int y = accentHeight + track * rowHeight;
        if (track > selected) y += laneHeight;
        return { 0, y, width, rowHeight };
    }

    juce::Rectangle<int> lane (int width) const
    {
        return { 0, accentHeight + (selected + 1) * rowHeight, width, laneHeight };
    }

    int totalHeight() const { return accentHeight + kNumTracks * rowHeight + laneHeight; }

    /** Track index under y, -1 for the accent row, -2 for the velocity lane. */
    int hitTest (int y) const
    {
        if (y < accentHeight) return -1;
        if (lane (1).contains (0, y)) return -2;
        for (int t = 0; t < kNumTracks; ++t)
            if (row (t, 1).contains (0, y)) return t;
        return kNumTracks - 1;
    }

    static RowLayout fit (int height, int selectedTrack)
    {
        RowLayout l;
        l.selected = selectedTrack;
        // Rows share the height; the velocity lane gets ~1.8 rows.
        l.rowHeight = juce::jmax (18, (int) ((height - l.accentHeight) / (kNumTracks + 1.8f)));
        l.laneHeight = juce::jmax (30, height - l.accentHeight - kNumTracks * l.rowHeight);
        return l;
    }
};

} // namespace bf::ui
