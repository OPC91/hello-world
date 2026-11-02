#pragma once

#include "Break.h"

#include <cstdint>
#include <string>
#include <vector>

namespace seam::engine
{
    enum class FillStyle { liquid = 0, neuro = 1, jungle = 2, jumpUp = 3 };

    /** What a fill event is doing (for the MIDI clip, the UI and tests). */
    enum class FillRole { groove, halfTime, ghostChop, roll, reverse };

    /** One hit of a fill. */
    struct FillEvent
    {
        double ppq = 0.0;        ///< quarter notes from the fill start. For reversed hits: where the hit ENDS.
        int slice = 0;
        float gain = 1.0f;       ///< 0..1
        bool reverse = false;    ///< played backwards, on its own lane (it is not cut by the main lane)
        FillRole role = FillRole::groove;
    };

    /** A generated fill, ready for the audio thread. Immutable once published. */
    struct FillPattern
    {
        int bars = 1;
        double quartersPerBar = 4.0;
        double length = 4.0;              ///< quarter notes
        std::vector<FillEvent> events;    ///< sorted by ppq
        std::uint64_t seed = 0;
        std::uint64_t kitId = 0;          ///< the kit whose slice numbers these are
        std::string description;          ///< e.g. "Neuro: half-time switch, 1/32T roll, reverse snare"
    };

    struct FillRequest
    {
        FillStyle style = FillStyle::liquid;
        float complexity = 0.5f;          ///< 0..1: density, how early and how fast the roll goes
        int bars = 1;                     ///< 1, 2 or 4
        double quartersPerBar = 4.0;
        std::uint64_t seed = 1;
    };

    /** Builds a fill from the slices available. Deterministic: the same kit summary, request and seed give the same
        fill on every platform. Vocabulary:
          - groove: the break re-chopped as a two-step (or amen-style for Jungle), with ghosts and hats by complexity;
          - half-time switch: kick on 1, snare on 3, once it switches it stays switched until the climax;
          - ghost-note stutter chops: bursts of 1/32 ghost notes;
          - snare roll in the final bar, ramping 1/16 -> 1/16T -> 1/32 -> 1/32T and getting louder;
          - reversed snare that ends exactly on the seam.
        Runs off the audio thread (it allocates). */
    FillPattern generateFill (const KitSummary&, const FillRequest&);

    /** Roll subdivisions in quarter notes, in the order a roll ramps through them. */
    inline constexpr double rollSteps[] { 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0 };
    const char* rollStepName (double quarters) noexcept;
}
