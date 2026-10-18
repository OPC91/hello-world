#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "TestHelpers.h"
#include "dsp/LoudnessMeter.h"

using benchmark::dsp::LoudnessMeter;
using Catch::Approx;

namespace
{
    constexpr double tolerance = 0.1; // EBU Tech 3341 tolerance for M, S and I: +/- 0.1 LU

    LoudnessMeter measure (const juce::AudioBuffer<float>& buffer, double sampleRate, int blockSize = 512)
    {
        LoudnessMeter meter;
        meter.prepare (sampleRate, buffer.getNumChannels());

        for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
        {
            const auto n = std::min (blockSize, buffer.getNumSamples() - start);
            const float* ptrs[2] = { buffer.getReadPointer (0, start),
                                     buffer.getReadPointer (std::min (1, buffer.getNumChannels() - 1), start) };
            meter.process (ptrs, buffer.getNumChannels(), n);
        }

        return meter;
    }
}

TEST_CASE ("K-weighting matches the BS.1770-4 coefficients at 48 kHz", "[loudness]")
{
    const auto shelf = benchmark::dsp::KWeighting::shelf (48000.0);
    CHECK (shelf.b0 == Approx (1.53512485958697).margin (1e-9));
    CHECK (shelf.b1 == Approx (-2.69169618940638).margin (1e-9));
    CHECK (shelf.b2 == Approx (1.19839281085285).margin (1e-9));
    CHECK (shelf.a1 == Approx (-1.69065929318241).margin (1e-9));
    CHECK (shelf.a2 == Approx (0.73248077421585).margin (1e-9));

    const auto hp = benchmark::dsp::KWeighting::highPass (48000.0);
    CHECK (hp.b0 == Approx (1.0));
    CHECK (hp.b1 == Approx (-2.0));
    CHECK (hp.b2 == Approx (1.0));
    CHECK (hp.a1 == Approx (-1.99004745483398).margin (1e-9));
    CHECK (hp.a2 == Approx (0.99007225036621).margin (1e-9));
}

TEST_CASE ("EBU Tech 3341 minimum requirements (stereo sine cases)", "[loudness][ebu]")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0, 192000.0);
    CAPTURE (sampleRate);

    SECTION ("Case 1: -23 dBFS for 20 s reads -23.0 M, S and I")
    {
        const auto m = measure (testing::makeSine (sampleRate, 20.0, 1000.0, -23.0), sampleRate);
        CHECK (m.getMomentary() == Approx (-23.0).margin (tolerance));
        CHECK (m.getShortTerm() == Approx (-23.0).margin (tolerance));
        CHECK (m.getIntegrated() == Approx (-23.0).margin (tolerance));
    }

    SECTION ("Case 2: -33 dBFS for 20 s reads -33.0 M, S and I")
    {
        const auto m = measure (testing::makeSine (sampleRate, 20.0, 1000.0, -33.0), sampleRate);
        CHECK (m.getMomentary() == Approx (-33.0).margin (tolerance));
        CHECK (m.getShortTerm() == Approx (-33.0).margin (tolerance));
        CHECK (m.getIntegrated() == Approx (-33.0).margin (tolerance));
    }

    SECTION ("Case 3: -36 / -23 / -36 dBFS (10 / 60 / 10 s) integrates to -23.0")
    {
        const auto m = measure (testing::makeSineSequence (sampleRate, { { 10, -36 }, { 60, -23 }, { 10, -36 } }), sampleRate);
        CHECK (m.getIntegrated() == Approx (-23.0).margin (tolerance));
    }

    SECTION ("Case 4: -72 / -36 / -23 / -36 / -72 dBFS integrates to -23.0")
    {
        const auto m = measure (testing::makeSineSequence (sampleRate, { { 10, -72 }, { 10, -36 }, { 60, -23 }, { 10, -36 }, { 10, -72 } }),
                                sampleRate);
        CHECK (m.getIntegrated() == Approx (-23.0).margin (tolerance));
    }

    SECTION ("Case 5: -26 / -20 / -26 dBFS (20 / 20.1 / 20 s) integrates to -23.0")
    {
        const auto m = measure (testing::makeSineSequence (sampleRate, { { 20, -26 }, { 20.1, -20 }, { 20, -26 } }), sampleRate);
        CHECK (m.getIntegrated() == Approx (-23.0).margin (tolerance));
    }
}

TEST_CASE ("EBU Tech 3341 case 9: short-term reads a constant -23.0 for alternating levels", "[loudness][ebu]")
{
    constexpr double sampleRate = 48000.0;

    // 1.34 s at -20 dBFS then 1.66 s at -30 dBFS, repeated: every 3 s window holds exactly one period.
    std::vector<std::pair<double, double>> segments;
    for (int i = 0; i < 5; ++i)
    {
        segments.push_back ({ 1.34, -20.0 });
        segments.push_back ({ 1.66, -30.0 });
    }

    const auto signal = testing::makeSineSequence (sampleRate, segments);

    LoudnessMeter meter;
    meter.prepare (sampleRate, 2);

    const auto hop = (int) (sampleRate * 0.1);
    int checked = 0;

    for (int start = 0; start + hop <= signal.getNumSamples(); start += hop)
    {
        const float* ptrs[2] = { signal.getReadPointer (0, start), signal.getReadPointer (1, start) };
        meter.process (ptrs, 2, hop);

        if (meter.getElapsedSeconds() >= 3.0 - 1e-9)
        {
            REQUIRE (meter.getShortTerm() == Approx (-23.0).margin (tolerance));
            ++checked;
        }
    }

    CHECK (checked > 100);
}

TEST_CASE ("EBU Tech 3342 loudness range cases", "[loudness][ebu]")
{
    constexpr double sampleRate = 48000.0;
    constexpr double lraTolerance = 1.0; // Tech 3342: +/- 1 LU

    SECTION ("Case 1: -20 / -30 dBFS gives LRA 10")
    {
        const auto m = measure (testing::makeSineSequence (sampleRate, { { 20, -20 }, { 20, -30 } }), sampleRate);
        CHECK (m.getLoudnessRange() == Approx (10.0).margin (lraTolerance));
    }

    SECTION ("Case 2: -20 / -15 dBFS gives LRA 5")
    {
        const auto m = measure (testing::makeSineSequence (sampleRate, { { 20, -20 }, { 20, -15 } }), sampleRate);
        CHECK (m.getLoudnessRange() == Approx (5.0).margin (lraTolerance));
    }

    SECTION ("Case 3: -40 / -20 dBFS gives LRA 20")
    {
        const auto m = measure (testing::makeSineSequence (sampleRate, { { 20, -40 }, { 20, -20 } }), sampleRate);
        CHECK (m.getLoudnessRange() == Approx (20.0).margin (lraTolerance));
    }

    SECTION ("Case 4: -50 / -35 / -20 / -35 / -50 dBFS gives LRA 15")
    {
        const auto m = measure (testing::makeSineSequence (sampleRate, { { 20, -50 }, { 20, -35 }, { 20, -20 }, { 20, -35 }, { 20, -50 } }),
                                sampleRate);
        CHECK (m.getLoudnessRange() == Approx (15.0).margin (lraTolerance));
    }
}

TEST_CASE ("Mono signals are measured as a single channel", "[loudness]")
{
    // A full-scale 1 kHz sine in one channel reads -3.01 LUFS (BS.1770 channel sum).
    const auto m = measure (testing::makeSine (48000.0, 10.0, 1000.0, 0.0, 1), 48000.0);
    CHECK (m.getIntegrated() == Approx (-3.01).margin (tolerance));
}

TEST_CASE ("Loudness readings do not depend on block size", "[loudness]")
{
    const auto signal = testing::makeSineSequence (48000.0, { { 5, -30 }, { 10, -18 }, { 5, -40 } });
    const auto reference = measure (signal, 48000.0, 4096);

    for (const auto blockSize : { 1, 37, 480, 1000, 8192 })
    {
        const auto m = measure (signal, 48000.0, blockSize);
        CHECK (m.getIntegrated() == Approx (reference.getIntegrated()).margin (1e-3));
        CHECK (m.getShortTerm() == Approx (reference.getShortTerm()).margin (1e-3));
        CHECK (m.getLoudnessRange() == Approx (reference.getLoudnessRange()).margin (1e-3));
    }
}

TEST_CASE ("Silence and reset", "[loudness]")
{
    LoudnessMeter meter;
    meter.prepare (48000.0, 2);

    juce::AudioBuffer<float> silence (2, 48000 * 5);
    silence.clear();
    const float* ptrs[2] = { silence.getReadPointer (0), silence.getReadPointer (1) };
    meter.process (ptrs, 2, silence.getNumSamples());

    CHECK (std::isinf (meter.getIntegrated()));
    CHECK (std::isinf (meter.getShortTerm()));

    // Long enough that the gating blocks straddling the silence/tone edge don't matter.
    const auto tone = testing::makeSine (48000.0, 20.0, 1000.0, -23.0);
    const float* tonePtrs[2] = { tone.getReadPointer (0), tone.getReadPointer (1) };
    meter.process (tonePtrs, 2, tone.getNumSamples());
    CHECK (meter.getIntegrated() == Approx (-23.0).margin (tolerance));

    meter.reset();
    CHECK (std::isinf (meter.getIntegrated()));
    CHECK (meter.getElapsedSeconds() == Approx (0.0));
}
