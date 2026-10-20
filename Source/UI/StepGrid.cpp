#include "StepGrid.h"
#include "StepPopup.h"
#include "Theme.h"
#include "Core/AudioFileLoader.h"
#include "Plugin/PluginProcessor.h"

namespace bf::ui
{

// PatternView ------------------------------------------------------------------

const Pattern& PatternView::pattern() const
{
    return state.processor.getModel().bank.patterns[(size_t) state.processor.getEditPattern()];
}

void PatternView::edit (const std::function<void (Pattern&)>& fn)
{
    const int slot = state.processor.getEditPattern();
    state.processor.editModel ([&] (SequencerSnapshot& m) { fn (m.bank.patterns[(size_t) slot]); }, firstEditOfGesture);
    firstEditOfGesture = false;
    repaint();
}

void PatternView::setPlayhead (float step)
{
    if (! juce::approximatelyEqual (step, playhead))
    {
        playhead = step;
        repaint();
    }
}

int PatternView::stepAtX (int x) const
{
    const int len = juce::jmax (1, pattern().length);
    return juce::jlimit (0, len - 1, (int) ((float) x * (float) len / (float) juce::jmax (1, getWidth())));
}

juce::StringArray PatternView::audioFilesIn (const juce::StringArray& files)
{
    juce::StringArray audio;

    for (const auto& f : files)
        if (isSupportedAudioFile (juce::File (f)))
            audio.add (f);

    // Slices are usually numbered: "break_1.wav, break_2.wav ... break_10.wav".
    std::sort (audio.begin(), audio.end(), [] (const juce::String& a, const juce::String& b) {
        return juce::File (a).getFileName().compareNatural (juce::File (b).getFileName()) < 0;
    });

    return audio;
}

bool PatternView::isInterestedInFileDrag (const juce::StringArray& files)
{
    return ! audioFilesIn (files).isEmpty();
}

void PatternView::fileDragMove (const juce::StringArray& files, int x, int y)
{
    const int hit = layout.hitTest (y);
    const int track = hit >= 0 ? hit : hit == -2 ? state.getSelectedTrack() : -1;
    const int step = stepAtX (x);
    const int count = audioFilesIn (files).size();

    if (track != dropTrack || step != dropStep || count != dropCount)
    {
        dropTrack = track;
        dropStep = step;
        dropCount = count;
        repaint();
    }
}

void PatternView::fileDragExit (const juce::StringArray&)
{
    dropTrack = dropStep = -1;
    repaint();
}

void PatternView::filesDropped (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
    const int track = dropTrack, first = dropStep;
    dropTrack = dropStep = -1;
    repaint();

    if (track < 0)
    {
        if (state.showStatus)
            state.showStatus ("Drop samples on a track's steps (the accent row has no sound)");
        return;
    }

    auto& proc = state.processor;
    const auto audio = audioFilesIn (files);
    const auto& trackName = proc.getTrackSettings()[(size_t) track].name;
    int placed = 0;

    for (int i = 0; i < audio.size() && first + i < pattern().length; ++i)
    {
        if (proc.assignHitSample (track, first + i, SampleRef::fromFile (juce::File (audio[i]))) < 0)
        {
            if (state.showStatus)
                state.showStatus (trackName + " already uses 15 different per-hit samples: reset some hits to the track sound first");
            break;
        }

        ++placed;
    }

    state.setSelectedTrack (track);

    if (placed > 0 && state.showStatus)
    {
        if (placed == 1)
            state.showStatus (trackName + " step " + juce::String (first + 1) + " now plays "
                              + juce::File (audio[0]).getFileNameWithoutExtension());
        else
            state.showStatus (juce::String (placed) + " samples placed on " + trackName + " steps "
                              + juce::String (first + 1) + "-" + juce::String (first + placed)
                              + (placed < audio.size() ? " (the rest didn't fit in the pattern)" : ""));
    }
}

void PatternView::paintDropTarget (juce::Graphics& g, float cw)
{
    if (dropTrack < 0 || dropStep < 0)
        return;

    const auto row = layout.row (dropTrack, getWidth()).toFloat();
    const int last = juce::jmin (pattern().length, dropStep + juce::jmax (1, dropCount)) - 1;
    const auto area = juce::Rectangle<float> (dropStep * cw, row.getY(), (last - dropStep + 1) * cw, row.getHeight()).reduced (1.0f);

    g.setColour (Colours::accent.withAlpha (0.25f));
    g.fillRoundedRectangle (area, 3.0f);
    g.setColour (Colours::accent);
    g.drawRoundedRectangle (area, 3.0f, 2.0f);
}

void PatternView::paintHitSampleMarker (juce::Graphics& g, juce::Rectangle<float> cell)
{
    // A folded corner: "this hit plays its own sample".
    const float s = juce::jlimit (5.0f, 9.0f, cell.getHeight() * 0.3f);
    juce::Path corner;
    corner.addTriangle (cell.getRight() - s, cell.getY(), cell.getRight(), cell.getY(), cell.getRight(), cell.getY() + s);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillPath (corner);
}

int PatternView::defaultVelocity (int track)
{
    return track == 2 ? 40 : 100; // ghost snares start soft
}

void PatternView::audition (int track, int velocity)
{
    state.processor.getEngine().audition (track, (float) velocity / 127.0f);
}

void PatternView::paintBackground (juce::Graphics& g, float cw)
{
    g.fillAll (Colours::panel);
    const auto& p = pattern();
    const int perBeat = p.stepsPerBeat();

    // Beat bands across all rows.
    for (int s = 0; s < p.length; s += perBeat)
        if ((s / perBeat) % 2 == 1)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (s * cw, 0.0f, perBeat * cw, (float) getHeight()));
        }

    // Selected row highlight.
    const auto sel = layout.row (state.getSelectedTrack(), getWidth());
    g.setColour (Colours::track (state.getSelectedTrack()).withAlpha (0.06f));
    g.fillRect (sel);
}

void PatternView::paintBeatDividers (juce::Graphics& g, float cw)
{
    const auto& p = pattern();
    const int perBeat = p.stepsPerBeat();
    const int perBar = perBeat * 4;
    const float h = (float) getHeight();

    for (int s = perBeat; s < p.length; s += perBeat)
    {
        const float x = s * cw;
        const bool bar = s % perBar == 0;

        // A background-coloured gutter separates the beats, with a line down its middle.
        g.setColour (Colours::panel);
        g.fillRect (juce::Rectangle<float> (x - 2.0f, 0.0f, 4.0f, h));
        g.setColour (bar ? Colours::accent.withAlpha (0.7f) : Colours::textDim.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (x - (bar ? 1.0f : 0.5f), 0.0f, bar ? 2.0f : 1.0f, h));
    }
}

juce::Colour PatternView::offCellColour (int step) const
{
    const int perBeat = pattern().stepsPerBeat();
    const bool oddBeat = (step / perBeat) % 2 == 1;
    const auto base = oddBeat ? Colours::cellOff.brighter (0.07f) : Colours::cellOff;
    return step % perBeat == 0 ? base.brighter (0.08f) : base; // the beat itself stands out
}

void PatternView::paintLaneFrame (juce::Graphics& g)
{
    const auto lane = layout.lane (getWidth());
    g.setColour (Colours::background.withAlpha (0.6f));
    g.fillRect (lane);
    g.setColour (Colours::outline);
    g.drawHorizontalLine (lane.getY(), 0.0f, (float) getWidth());
    g.drawHorizontalLine (lane.getBottom() - 1, 0.0f, (float) getWidth());
}

void PatternView::paintPlayhead (juce::Graphics& g, float x0, float cw)
{
    if (playhead < 0.0f)
        return;

    g.setColour (Colours::playhead);
    g.fillRect (juce::Rectangle<float> (x0 + std::floor (playhead) * cw, 0.0f, cw, (float) getHeight()));
    g.setColour (Colours::accent.withAlpha (0.9f));
    g.fillRect (juce::Rectangle<float> (x0 + playhead * cw - 1.0f, 0.0f, 2.0f, (float) getHeight()));
}

void PatternView::setVelocityFromLane (int step, int y)
{
    const auto lane = layout.lane (getWidth()).reduced (0, 4);
    const int track = state.getSelectedTrack();
    const int vel = juce::jlimit (1, 127, (int) std::lround (juce::jmap ((float) y, (float) lane.getBottom(), (float) lane.getY(), 1.0f, 127.0f)));

    if (step < 0 || step >= pattern().length || ! pattern().tracks[(size_t) track].steps[(size_t) step].on)
        return;

    edit ([&] (Pattern& p) { p.tracks[(size_t) track].steps[(size_t) step].velocity = (std::uint8_t) vel; });
}

// StepGrid ---------------------------------------------------------------------

StepGrid::StepGrid (EditorState& s) : PatternView (s)
{
    setRepaintsOnMouseActivity (false);
}

float StepGrid::cellWidth() const
{
    return (float) getWidth() / (float) juce::jmax (1, pattern().length);
}

int StepGrid::stepAt (int x) const
{
    return juce::jlimit (0, pattern().length - 1, (int) ((float) x / cellWidth()));
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto& p = pattern();
    const float cw = cellWidth();
    const int perBeat = p.stepsPerBeat();
    paintBackground (g, cw);

    // Accent row.
    const auto accentRow = layout.accentRow (getWidth()).toFloat();
    for (int s = 0; s < p.length; ++s)
    {
        const auto cell = juce::Rectangle<float> (s * cw, accentRow.getY(), cw, accentRow.getHeight()).reduced (2.0f, 3.0f);
        g.setColour (p.accents[(size_t) s] ? Colours::accent : offCellColour (s).withAlpha (0.6f));
        g.fillRoundedRectangle (cell, 2.0f);

        // Beat numbers (bar.beat once the pattern is longer than a bar).
        if (s % perBeat == 0)
        {
            const int beat = s / perBeat;
            const auto text = p.length > perBeat * 4 ? juce::String (beat / 4 + 1) + "." + juce::String (beat % 4 + 1)
                                                     : juce::String (beat + 1);
            g.setColour (p.accents[(size_t) s] ? juce::Colours::black : Colours::text.withAlpha (0.8f));
            g.setFont (font (10.5f, true));
            g.drawText (text, cell.withTrimmedLeft (3.0f), juce::Justification::centredLeft);
        }
    }

    // Track rows.
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto row = layout.row (t, getWidth()).toFloat();

        for (int s = 0; s < p.length; ++s)
        {
            const auto& st = p.tracks[(size_t) t].steps[(size_t) s];
            const auto cell = juce::Rectangle<float> (s * cw, row.getY(), cw, row.getHeight()).reduced (juce::jmin (2.5f, cw * 0.08f), 3.0f);

            if (! st.on)
            {
                g.setColour (offCellColour (s));
                g.fillRoundedRectangle (cell, 3.0f);
                continue;
            }

            auto colour = Colours::forVelocity (t, st.velocity);
            if (st.probability < 100)
                colour = colour.withMultipliedAlpha (0.45f + 0.55f * st.probability / 100.0f);

            g.setColour (colour);
            g.fillRoundedRectangle (cell, 3.0f);

            // Probability: a dotted outline that fills in as the chance rises.
            if (st.probability < 100)
            {
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                const float w = cell.getWidth() * st.probability / 100.0f;
                g.fillRect (cell.getX(), cell.getBottom() - 2.0f, w, 2.0f);
            }

            // Ratchets: one tick per repeat.
            if (st.ratchet > 1)
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                const float tickW = cell.getWidth() / (float) st.ratchet;
                for (int r = 1; r < st.ratchet; ++r)
                    g.fillRect (cell.getX() + r * tickW - 0.75f, cell.getY() + 3.0f, 1.5f, cell.getHeight() - 6.0f);
            }

            // Nudge: a small marker showing the shift off the grid.
            if (std::abs (st.nudge) > 0.005f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                const float x = cell.getCentreX() + st.nudge * cell.getWidth();
                g.fillRect (x - 1.0f, cell.getY() + 1.0f, 2.0f, 4.0f);
            }

            if (st.sample > 0)
                paintHitSampleMarker (g, cell);

            if (st.pitch != 0 && cell.getWidth() > 18.0f && cell.getHeight() > 16.0f)
            {
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.setFont (font (9.5f, true));
                g.drawText ((st.pitch > 0 ? "+" : "") + juce::String (st.pitch), cell.reduced (2.0f), juce::Justification::topLeft);
            }
        }
    }

    // Velocity lane for the selected track.
    paintLaneFrame (g);
    const auto lane = layout.lane (getWidth()).toFloat().reduced (0.0f, 4.0f);
    const int sel = state.getSelectedTrack();

    for (int s = 0; s < p.length; ++s)
    {
        const auto& st = p.tracks[(size_t) sel].steps[(size_t) s];
        const auto column = juce::Rectangle<float> (s * cw, lane.getY(), cw, lane.getHeight()).reduced (juce::jmin (3.0f, cw * 0.15f), 0.0f);

        if (! st.on)
        {
            g.setColour (Colours::cellOff);
            g.fillRect (column.withTop (column.getBottom() - 2.0f));
            continue;
        }

        const float h = column.getHeight() * st.velocity / 127.0f;
        g.setColour (Colours::forVelocity (sel, st.velocity));
        g.fillRect (column.withTop (column.getBottom() - h));
        g.setColour (Colours::track (sel));
        g.fillRect (column.withHeight (2.0f).withY (column.getBottom() - h));
    }

    paintBeatDividers (g, cw);
    paintDropTarget (g, cw);
    paintPlayhead (g, 0.0f, cw);
}

void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    const int hit = layout.hitTest (e.y);
    setMouseCursor (hit == -2 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    beginGesture();
    const int hit = layout.hitTest (e.y);
    const int step = stepAt (e.x);
    lastStep = step;

    if (hit == -1) // accent row
    {
        paintValue = ! pattern().accents[(size_t) step];
        mode = e.mods.isShiftDown() ? Mode::AccentPaint : Mode::Accent;
        edit ([&] (Pattern& p) { p.accents[(size_t) step] = paintValue; });
        return;
    }

    if (hit == -2) // velocity lane
    {
        mode = Mode::Lane;
        setVelocityFromLane (step, e.y);
        return;
    }

    dragTrack = hit;
    state.setSelectedTrack (hit);

    if (e.mods.isPopupMenu())
    {
        mode = Mode::None;
        openStepPopup (hit, step);
        return;
    }

    if (e.mods.isAltDown())
    {
        mode = Mode::VelocityPaint;
        applyAt (step, e.y);
        return;
    }

    paintValue = ! pattern().tracks[(size_t) hit].steps[(size_t) step].on;
    mode = e.mods.isShiftDown() ? Mode::Paint : Mode::Toggle;
    applyAt (step, e.y);

    if (paintValue)
        audition (hit, pattern().tracks[(size_t) hit].steps[(size_t) step].velocity);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (mode == Mode::None || mode == Mode::Toggle || mode == Mode::Accent)
        return;

    const int step = stepAt (e.x);

    // Fill every step between the last and current position so fast drags leave no gaps.
    const int from = juce::jmin (lastStep, step), to = juce::jmax (lastStep, step);

    for (int s = from; s <= to; ++s)
    {
        if (mode == Mode::Lane)
            setVelocityFromLane (s, e.y);
        else if (mode == Mode::AccentPaint)
            edit ([&] (Pattern& p) { p.accents[(size_t) s] = paintValue; });
        else
            applyAt (s, e.y);
    }

    lastStep = step;
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    mode = Mode::None;
}

void StepGrid::applyAt (int step, int y)
{
    const int t = dragTrack;
    const auto row = layout.row (t, getWidth());

    if (mode == Mode::VelocityPaint)
    {
        if (! pattern().tracks[(size_t) t].steps[(size_t) step].on)
            return;

        const int vel = juce::jlimit (1, 127, (int) juce::jmap ((float) y, (float) row.getBottom(), (float) row.getY(), 1.0f, 127.0f));
        edit ([&] (Pattern& p) { p.tracks[(size_t) t].steps[(size_t) step].velocity = (std::uint8_t) vel; });
        return;
    }

    if (pattern().tracks[(size_t) t].steps[(size_t) step].on == paintValue)
        return;

    edit ([&] (Pattern& p) {
        auto& st = p.tracks[(size_t) t].steps[(size_t) step];
        if (paintValue && st == Step {})
            st.velocity = (std::uint8_t) defaultVelocity (t);
        st.on = paintValue;
    });
}

void StepGrid::openStepPopup (int track, int step)
{
    const float cw = cellWidth();
    const auto row = layout.row (track, getWidth());
    const auto cell = juce::Rectangle<int> ((int) (step * cw), row.getY(), (int) cw, row.getHeight());

    // The popup edits the step directly; turning it on first makes the controls meaningful.
    if (! pattern().tracks[(size_t) track].steps[(size_t) step].on)
        edit ([&] (Pattern& p) {
            auto& st = p.tracks[(size_t) track].steps[(size_t) step];
            if (st == Step {}) st.velocity = (std::uint8_t) defaultVelocity (track);
            st.on = true;
        });

    // Parented to the editor so the popup can never outlive it.
    auto* editor = getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously (std::make_unique<StepPopup> (state, track, step, [this] { repaint(); }),
                                            editor->getLocalArea (this, cell), editor);
}

} // namespace bf::ui
