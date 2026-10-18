#include "GainMatcher.h"

#include <algorithm>
#include <cmath>

namespace benchmark::dsp
{
    MatchGains computeMatchGains (float mixLufs, float referenceLufs, float maxAttenuationDb) noexcept
    {
        constexpr float absoluteGate = -70.0f;

        const auto valid = [] (float v) { return std::isfinite (v) && v > absoluteGate; };

        if (! valid (mixLufs) || ! valid (referenceLufs))
            return {};

        const auto difference = std::min (std::abs (mixLufs - referenceLufs), std::abs (maxAttenuationDb));

        if (mixLufs > referenceLufs)
            return { -difference, 0.0f };

        return { 0.0f, -difference };
    }
}
