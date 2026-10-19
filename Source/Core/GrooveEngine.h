#pragma once

#include <array>
#include <cstdint>

#include "Pattern.h"

namespace bf
{

/** A groove template: a suggested swing amount plus per-16th timing and velocity offsets
    across one bar. Offsets are relative to the step length, so they scale with tempo. */
struct GrooveTemplate
{
    float swing = 0.5f;                      // 0.5..0.75
    std::array<float, 16> timing {};         // -0.5..+0.5 of a 16th
    std::array<float, 16> velocity {};       // multiplier, 1 = unchanged

    GrooveTemplate() { velocity.fill (1.0f); }

    bool operator== (const GrooveTemplate&) const = default;
};

namespace groove
{
    /** Factory templates in the order of grooveNames() (excluding "User"). */
    const GrooveTemplate& factoryTemplate (int index) noexcept;
    constexpr int numFactoryTemplates = 5;

    /** MPC-style swing as a piecewise-linear time warp.

        Within each `unitPpq` span (0.5 ppq for 16th swing, 1 ppq for 8th swing) the first half
        is stretched to `swing` of the span and the second half compressed into the remainder,
        so the off-beat lands at swing * unit. 0.5 = straight, 0.66 = triplet feel. Because the
        warp is expressed in beats it is computed relative to the host tempo, not in ms. */
    double warp (double ppq, double swing, double unitPpq) noexcept;

    /** Applies 16th swing (and optionally 8th swing) to a position in quarter notes. */
    double applySwing (double ppq, double swing, bool swingEighths) noexcept;

    /** Index (0..15) of the 16th note inside the bar that contains the given ppq position. */
    int sixteenthInBar (double ppq) noexcept;

    /** Deterministic hash -> uniform float in [0, 1). Used for humanize and probability so
        playback is repeatable until the seed is re-rolled. */
    float random01 (std::uint32_t seed, std::int64_t a, std::uint32_t b, std::uint32_t c) noexcept;

    /** random01 mapped to [-1, 1). */
    inline float randomBipolar (std::uint32_t seed, std::int64_t a, std::uint32_t b, std::uint32_t c) noexcept
    {
        return random01 (seed, a, b, c) * 2.0f - 1.0f;
    }

    /** How velocity (0..1) maps to amplitude given the global Velocity Depth (0..1).
        depth 0 = every hit at full level; depth 1 = gentle square-law response. */
    float velocityToGain (float velocity, float depth) noexcept;
}

} // namespace bf
