#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dsp/GainMatcher.h"

#include <cmath>
#include <limits>

using benchmark::dsp::computeMatchGains;
using Catch::Approx;

TEST_CASE ("Louder mix is turned down to the reference", "[gainmatch]")
{
    const auto g = computeMatchGains (-8.0f, -12.5f);
    CHECK (g.mixDb == Approx (-4.5f));
    CHECK (g.referenceDb == Approx (0.0f));
}

TEST_CASE ("Louder reference is turned down to the mix", "[gainmatch]")
{
    const auto g = computeMatchGains (-14.0f, -7.25f);
    CHECK (g.mixDb == Approx (0.0f));
    CHECK (g.referenceDb == Approx (-6.75f));
}

TEST_CASE ("Equal loudness needs no change", "[gainmatch]")
{
    const auto g = computeMatchGains (-10.0f, -10.0f);
    CHECK (g.mixDb == Approx (0.0f));
    CHECK (g.referenceDb == Approx (0.0f));
}

TEST_CASE ("Nothing is ever boosted", "[gainmatch]")
{
    for (float mix = -60.0f; mix <= 0.0f; mix += 1.7f)
        for (float ref = -60.0f; ref <= 0.0f; ref += 2.3f)
        {
            if (std::abs (mix - ref) > 30.0f)
                continue; // beyond the attenuation limit, covered below

            const auto g = computeMatchGains (mix, ref);
            REQUIRE (g.mixDb <= 0.0f);
            REQUIRE (g.referenceDb <= 0.0f);

            // After matching both play at the quieter loudness.
            REQUIRE (mix + g.mixDb == Approx (ref + g.referenceDb).margin (1e-4));
            REQUIRE (mix + g.mixDb == Approx (std::min (mix, ref)).margin (1e-4));
        }
}

TEST_CASE ("Missing or gated measurements disable matching", "[gainmatch]")
{
    constexpr auto inf = std::numeric_limits<float>::infinity();
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();

    for (const auto& [mix, ref] : { std::pair { -inf, -10.0f }, { -10.0f, -inf }, { nan, -10.0f },
                                   { -10.0f, nan }, { -75.0f, -10.0f }, { -10.0f, -71.0f } })
    {
        const auto g = computeMatchGains (mix, ref);
        CHECK (g.mixDb == Approx (0.0f));
        CHECK (g.referenceDb == Approx (0.0f));
    }
}

TEST_CASE ("Attenuation is limited to the maximum range", "[gainmatch]")
{
    const auto g = computeMatchGains (-5.0f, -60.0f, 30.0f);
    CHECK (g.mixDb == Approx (-30.0f));
    CHECK (g.referenceDb == Approx (0.0f));
}
