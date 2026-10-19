#pragma once

#include <cstdint>
#include <vector>

#include "Pattern.h"
#include "Tracks.h"

namespace bf
{

enum class SubStyle
{
    Liquid = 0,
    JumpUp,
    Neuro,
    Jungle,
    Halftime,
    Rollers,
    Minimal
};

/** Rule-based DnB pattern generator. No ML: per-style weighted probability tables for each drum
    role, built around fixed kick/snare anchors so every result is immediately usable. */
namespace PatternGenerator
{
    struct Options
    {
        SubStyle style = SubStyle::Liquid;
        float variation = 0.35f;              // 0 = core pattern only, 1 = far from the core
        std::uint32_t seed = 1;
        int length = 16;                      // 16 or 32 steps
        Resolution resolution = Resolution::Sixteenth;
        std::array<bool, kNumTracks> locked {}; // locked tracks are copied from `base`
        /** Length of the FX sample in quarter notes, if it is a riser/reverse cymbal that should
            swell into the next loop. 0 = treat FX as an on-the-beat hit. */
        double fxRiserLengthPpq = 0.0;
    };

    /** Generates a full pattern. Locked tracks keep their content from `base`. */
    Pattern generate (const Options&, const Pattern& base = {});

    /** Re-rolls a single track of `pattern`, in the context of the other tracks. */
    void generateTrack (Pattern& pattern, int track, const Options&);

    /** A one-bar fill derived from `source`: the groove continues for the first half, then
        builds into snare rolls, ratchets and a kick push into the next downbeat. */
    Pattern generateFill (const Pattern& source, const Options&);

    /** Snare backbeat positions (in 16ths within a bar) for a style. */
    std::vector<int> snareAnchors (SubStyle);

    /** Groove template index (see grooveNames()) and swing that suit a style. */
    struct GrooveSuggestion { int templateIndex; float swing; };
    GrooveSuggestion suggestedGroove (SubStyle);
}

} // namespace bf
