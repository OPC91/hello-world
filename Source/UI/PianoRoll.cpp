#include "PianoRoll.h"
#include "Theme.h"
#include "Plugin/PluginProcessor.h"

namespace bf::ui
{

PianoRoll::PianoRoll (EditorState& s) : PatternView (s) {}

float PianoRoll::cellWidth() const
{
    return (float) getWidth() / (float) juce::jmax (1, pattern().length);
}

juce::Rectangle<float> PianoRoll::noteBounds (int track, int step) const
{
    const auto& st = pattern().tracks[(size_t) track].steps[(size_t) step];
    const float cw = cellWidth();
    const auto row = layout.row (track, getWidth()).toFloat().reduced (0.0f, 4.0f);
    const float x = (step + st.nudge) * cw;
    const float w = juce::jmax (5.0f, st.length * cw - 1.0f);
    return { x, row.getY(), w, row.getHeight() };
}

std::pair<int, int> PianoRoll::noteAt (juce::Point<int> pos) const
{
    const int t = layout.hitTest (pos.y);

    if (t < 0)
        return { -1, -1 };

    // Search backwards so the most recently drawn (topmost) note wins.
    for (int s = pattern().length - 1; s >= 0; --s)
        if (pattern().tracks[(size_t) t].steps[(size_t) s].on && noteBounds (t, s).expanded (1.0f, 0.0f).contains (pos.toFloat()))
            return { t, s };

    return { -1, -1 };
}

bool PianoRoll::onResizeEdge (juce::Point<int> pos, int track, int step) const
{
    const auto b = noteBounds (track, step);
    return pos.x >= b.getRight() - 6.0f && b.getWidth() > 10.0f;
}

void PianoRoll::paint (juce::Graphics& g)
{
    const auto& p = pattern();
    const float cw = cellWidth();
    const int perBeat = p.stepsPerBeat();
    paintBackground (g, cw);

    // Grid lines: bars strongest, beats, then steps.
    for (int s = 0; s <= p.length; ++s)
    {
        const float x = s * cw;
        g.setColour (s % (perBeat * 4) == 0 ? Colours::textDim.withAlpha (0.6f)
                     : s % perBeat == 0     ? Colours::outline.brighter (0.25f)
                                            : Colours::outline.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
    }

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto row = layout.row (t, getWidth());
        g.setColour (Colours::outline.withAlpha (0.5f));
        g.drawHorizontalLine (row.getBottom(), 0.0f, (float) getWidth());

        for (int s = 0; s < p.length; ++s)
        {
            const auto& st = p.tracks[(size_t) t].steps[(size_t) s];
            if (! st.on)
                continue;

            const auto b = noteBounds (t, s);
            g.setColour (Colours::forVelocity (t, st.velocity).withMultipliedAlpha (st.probability < 100 ? 0.6f : 1.0f));
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (Colours::track (t));
            g.drawRoundedRectangle (b, 3.0f, 1.0f);

            if (st.ratchet > 1)
            {
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                const float sub = cw / st.ratchet;
                for (int r = 1; r < st.ratchet; ++r)
                    g.fillRect (b.getX() + r * sub - 0.75f, b.getY() + 3.0f, 1.5f, b.getHeight() - 6.0f);
            }
        }
    }

    // Accent row as small markers.
    for (int s = 0; s < p.length; ++s)
        if (p.accents[(size_t) s])
        {
            g.setColour (Colours::accent);
            g.fillRoundedRectangle (juce::Rectangle<float> (s * cw + 2.0f, 4.0f, cw - 4.0f, (float) layout.accentHeight - 8.0f), 2.0f);
        }

    // Velocity lollipops for the selected track at the notes' real positions.
    paintLaneFrame (g);
    const auto lane = layout.lane (getWidth()).toFloat().reduced (0.0f, 4.0f);
    const int sel = state.getSelectedTrack();

    for (int s = 0; s < p.length; ++s)
    {
        const auto& st = p.tracks[(size_t) sel].steps[(size_t) s];
        if (! st.on)
            continue;

        const float x = (s + st.nudge) * cw + 3.0f;
        const float y = lane.getBottom() - lane.getHeight() * st.velocity / 127.0f;
        g.setColour (Colours::track (sel));
        g.drawLine (x, lane.getBottom(), x, y, 2.0f);
        g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
    }

    paintPlayhead (g, 0.0f, cw);
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    const auto [t, s] = noteAt (e.getPosition());

    if (t >= 0)
        setMouseCursor (onResizeEdge (e.getPosition(), t, s) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (layout.hitTest (e.y) == -2 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    beginGesture();
    mode = Mode::None;

    if (layout.hitTest (e.y) == -2)
    {
        // Velocity: pick the note of the selected track nearest to the mouse.
        mode = Mode::Lane;
        mouseDrag (e);
        return;
    }

    const auto [t, s] = noteAt (e.getPosition());

    if (t < 0)
    {
        if (const int row = layout.hitTest (e.y); row >= 0)
            state.setSelectedTrack (row);
        return;
    }

    state.setSelectedTrack (t);
    dragTrack = t;
    dragStep = s;
    const auto& st = pattern().tracks[(size_t) t].steps[(size_t) s];
    dragStartPos = (float) s + st.nudge;
    dragStartLength = st.length;
    mode = onResizeEdge (e.getPosition(), t, s) ? Mode::Resize : Mode::Move;

    if (mode == Mode::Move)
        audition (t, st.velocity);
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    const float cw = cellWidth();
    const auto& p = pattern();

    if (mode == Mode::Lane)
    {
        const int sel = state.getSelectedTrack();
        int best = -1;
        float bestDist = cw;

        for (int s = 0; s < p.length; ++s)
        {
            const auto& st = p.tracks[(size_t) sel].steps[(size_t) s];
            const float d = std::abs ((s + st.nudge) * cw + 3.0f - (float) e.x);
            if (st.on && d < bestDist) { bestDist = d; best = s; }
        }

        if (best >= 0)
            setVelocityFromLane (best, e.y);
        return;
    }

    if (dragTrack < 0 || mode == Mode::None)
        return;

    const float delta = (float) e.getDistanceFromDragStartX() / cw;

    if (mode == Mode::Resize)
    {
        // Shift for fine control.
        const float len = juce::jlimit (0.1f, 16.0f, dragStartLength + delta * (e.mods.isShiftDown() ? 0.25f : 1.0f));
        edit ([&] (Pattern& pat) { pat.tracks[(size_t) dragTrack].steps[(size_t) dragStep].length = len; });
        return;
    }

    // Move: the note keeps its musical position; it changes step when nudged past half a step.
    const float pos = juce::jlimit (-0.5f, (float) p.length - 0.51f, dragStartPos + delta * (e.mods.isShiftDown() ? 0.25f : 1.0f));
    const int newStep = juce::jlimit (0, p.length - 1, (int) std::lround (pos));
    const float nudge = juce::jlimit (-0.5f, 0.5f, pos - (float) newStep);

    if (newStep != dragStep && p.tracks[(size_t) dragTrack].steps[(size_t) newStep].on)
        return; // don't overwrite another note

    edit ([&] (Pattern& pat) {
        auto& steps = pat.tracks[(size_t) dragTrack].steps;
        auto moved = steps[(size_t) dragStep];
        moved.nudge = nudge;

        if (newStep != dragStep)
            steps[(size_t) dragStep] = Step {};

        steps[(size_t) newStep] = moved;
    });

    dragStep = newStep;
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    mode = Mode::None;
    dragTrack = dragStep = -1;
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    beginGesture();
    const auto [t, s] = noteAt (e.getPosition());

    if (t >= 0)
    {
        edit ([&] (Pattern& p) { p.tracks[(size_t) t].steps[(size_t) s] = Step {}; });
        return;
    }

    const int row = layout.hitTest (e.y);

    if (row < 0)
        return;

    const int step = juce::jlimit (0, pattern().length - 1, (int) ((float) e.x / cellWidth()));
    edit ([&] (Pattern& p) {
        auto& st = p.tracks[(size_t) row].steps[(size_t) step];
        st = Step {};
        st.on = true;
        st.velocity = (std::uint8_t) defaultVelocity (row);
    });
    audition (row, defaultVelocity (row));
}

} // namespace bf::ui
