#pragma once

#include <cstdint>

#include "Pattern.h"

namespace bf::PatternTools
{
    /** Adds low-velocity snare ghosts at the classic DnB positions around the backbeat
        (the "a" of 2, the "e" of 3, the "and"/"a" of 4 ...) to `ghostTrack`, avoiding steps
        where the main snare already plays. Existing ghosts are kept. */
    void addGhostNotes (Pattern&, int ghostTrack, int snareTrack, std::uint32_t seed);

    /** Rotates every track (and the accent row) by `steps` within the pattern length. */
    void shift (Pattern&, int steps);

    /** Rotates a single track. */
    void shiftTrack (Pattern&, int track, int steps);

    /** Changes length/resolution, keeping events at the same musical position where possible. */
    void setLengthAndResolution (Pattern&, int newLength, Resolution newResolution);

    /** Grid steps per 16th note (1 at 1/16 resolution, 2 at 1/32). */
    inline int stepsPerSixteenth (const Pattern& p) noexcept { return p.resolution == Resolution::ThirtySecond ? 2 : 1; }

    /** Number of 16th notes the pattern spans. */
    inline int lengthInSixteenths (const Pattern& p) noexcept { return p.length / stepsPerSixteenth (p); }
}
