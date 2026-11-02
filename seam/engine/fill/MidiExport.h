#pragma once

#include "Break.h"
#include "FillGenerator.h"

#include <cstdint>
#include <vector>

namespace seam::engine
{
    /** Note for slice 1. MIDI 36 is C1 in the convention most DAWs use (C3 = middle C = 60: Ableton, FL Studio,
        Cubase, Bitwig), and is also where drum racks and slice-to-MIDI start. */
    inline constexpr int firstSliceNote = 36;

    struct MidiClipOptions
    {
        double bpm = 174.0;
        int ticksPerQuarter = 960;
        int timeSigNumerator = 4, timeSigDenominator = 4;
    };

    /** The fill as a Standard MIDI File (format 0): one note per hit, slice n -> note 35 + n (C1 = slice 1).
        Velocity follows the hit's gain. A reversed hit becomes a note at the time the reversed sound starts (so
        it still ends on the seam). Notes last until the next main-lane hit (min 1/64), like the sampler chokes.
        The clip is exactly the fill's length. */
    std::vector<std::uint8_t> exportFillMidi (const FillPattern&, const KitSummary&, const MidiClipOptions& = {});
}
