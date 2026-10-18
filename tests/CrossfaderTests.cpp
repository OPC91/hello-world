#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "dsp/Crossfader.h"

#include <cmath>
#include <numbers>

using benchmark::dsp::Crossfader;
using Catch::Approx;

TEST_CASE ("Crossfade length follows the sample rate", "[crossfade]")
{
    Crossfader xf;
    xf.prepare (48000.0, 15.0);
    CHECK (xf.getFadeLengthSamples() == 720);
    xf.prepare (44100.0, 15.0);
    CHECK (xf.getFadeLengthSamples() == 662);
    xf.prepare (192000.0, 10.0);
    CHECK (xf.getFadeLengthSamples() == 1920);
}

TEST_CASE ("Crossfade is equal-power, monotonic and completes on time", "[crossfade]")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    Crossfader xf;
    xf.prepare (sampleRate, 15.0);
    REQUIRE (xf.isFullyA());

    const auto length = xf.getFadeLengthSamples();
    const auto maxStep = std::numbers::pi_v<float> * 0.5f / (float) length + 1e-5f;

    xf.setTarget (true);
    float prevA = 1.0f, prevB = 0.0f;

    for (int i = 0; i < length; ++i)
    {
        float a, b;
        xf.next (a, b);

        REQUIRE (a * a + b * b == Approx (1.0f).margin (1e-5));
        REQUIRE (a <= prevA);
        REQUIRE (b >= prevB);
        REQUIRE (std::abs (a - prevA) <= maxStep); // no jumps -> no clicks
        REQUIRE (std::abs (b - prevB) <= maxStep);
        prevA = a;
        prevB = b;
    }

    CHECK (xf.isSettled());
    CHECK (prevA == Approx (0.0f).margin (1e-7));
    CHECK (prevB == Approx (1.0f));

    // Settled: stays put.
    float a, b;
    xf.next (a, b);
    CHECK (a == Approx (0.0f).margin (1e-7));
    CHECK (b == Approx (1.0f));
}

TEST_CASE ("Crossfade reverses smoothly mid-fade", "[crossfade]")
{
    Crossfader xf;
    xf.prepare (48000.0, 15.0);
    xf.setTarget (true);

    float a = 1.0f, b = 0.0f;
    for (int i = 0; i < 300; ++i)
        xf.next (a, b);

    const auto midA = a;
    xf.setTarget (false);

    float a2, b2;
    xf.next (a2, b2);
    CHECK (a2 > midA);                          // turned around
    CHECK (std::abs (a2 - midA) < 0.0025f);     // without a jump

    for (int i = 1; i < 300; ++i)
        xf.next (a2, b2);

    CHECK (xf.isFullyA());
    CHECK (a2 == Approx (1.0f));
    CHECK (b2 == Approx (0.0f).margin (1e-7));
}

TEST_CASE ("Crossfading two signals keeps the output continuous", "[crossfade]")
{
    // Mix = +0.5 DC, reference = -0.5 DC: the output must glide between them.
    Crossfader xf;
    xf.prepare (48000.0, 15.0);
    xf.setTarget (true);

    float previous = 0.5f;
    float maxDelta = 0.0f;

    for (int i = 0; i < 2000; ++i)
    {
        float a, b;
        xf.next (a, b);
        const auto out = 0.5f * a - 0.5f * b;
        maxDelta = std::max (maxDelta, std::abs (out - previous));
        previous = out;
    }

    CHECK (previous == Approx (-0.5f));
    CHECK (maxDelta < 0.003f);
}
