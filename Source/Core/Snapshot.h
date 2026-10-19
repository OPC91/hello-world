#pragma once

#include "GrooveEngine.h"
#include "Pattern.h"
#include "Tracks.h"

namespace bf
{

/** Non-parameter sequencer state the audio thread needs. Published from the message thread
    through a TripleBuffer whenever the user edits something. */
struct SequencerSnapshot
{
    PatternBank bank {};
    GrooveTemplate userGroove {};
    std::array<int, kNumTracks> midiNotes {};
    std::uint32_t humanizeSeed = 1;

    SequencerSnapshot()
    {
        for (int t = 0; t < kNumTracks; ++t)
            midiNotes[(size_t) t] = kTrackDefaults[(size_t) t].midiNote;
    }
};

} // namespace bf
