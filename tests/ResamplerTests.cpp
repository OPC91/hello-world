#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "TestHelpers.h"
#include "reference/Resampler.h"

using benchmark::Resampler;
using Catch::Approx;

namespace
{
    /** Max error against an ideal sine, ignoring the filter's edge transients. */
    double maxSineError (const juce::AudioBuffer<float>& out, double rate, double frequency, double amplitude)
    {
        const auto skip = (int) (rate * 0.05);
        double maxError = 0.0;

        for (int i = skip; i < out.getNumSamples() - skip; ++i)
        {
            const auto ideal = amplitude * std::sin (2.0 * std::numbers::pi * frequency * i / rate);
            maxError = std::max (maxError, std::abs (out.getSample (0, i) - ideal));
        }

        return maxError;
    }

    double rmsMiddle (const juce::AudioBuffer<float>& out, double rate)
    {
        const auto skip = (int) (rate * 0.05);
        return out.getRMSLevel (0, skip, out.getNumSamples() - 2 * skip);
    }
}

TEST_CASE ("Equal rates return an identical copy", "[resampler]")
{
    const auto in = testing::makeSine (48000.0, 0.5, 1234.0, -6.0);
    const auto out = Resampler::process (in, 48000.0, 48000.0);

    REQUIRE (out.getNumSamples() == in.getNumSamples());
    for (int i = 0; i < in.getNumSamples(); ++i)
        REQUIRE (juce::exactlyEqual (out.getSample (1, i), in.getSample (1, i)));
}

TEST_CASE ("Output length matches the rate ratio", "[resampler]")
{
    CHECK (Resampler::getOutputLength (44100, 44100.0, 48000.0) == 48000);
    CHECK (Resampler::getOutputLength (48000, 48000.0, 44100.0) == 44100);
    CHECK (Resampler::getOutputLength (96000, 96000.0, 44100.0) == 44100);
    CHECK (Resampler::getOutputLength (1, 44100.0, 192000.0) == 5);
    CHECK (Resampler::getOutputLength (0, 44100.0, 48000.0) == 0);
}

TEST_CASE ("Passband tones keep amplitude, frequency and phase", "[resampler]")
{
    using Rates = std::pair<double, double>;
    const auto rates = GENERATE (Rates { 44100.0, 48000.0 }, Rates { 48000.0, 44100.0 }, Rates { 44100.0, 96000.0 },
                                 Rates { 96000.0, 44100.0 }, Rates { 22050.0, 48000.0 }, Rates { 192000.0, 48000.0 },
                                 Rates { 44100.0, 47999.5 }); // non-integer rate exercises the generic phase path
    const auto frequency = GENERATE (100.0, 1000.0, 9000.0);

    const auto [from, to] = rates;
    CAPTURE (from, to, frequency);

    if (frequency >= 0.45 * std::min (from, to))
        SKIP();

    const auto in = testing::makeSine (from, 1.0, frequency, -6.0, 1);
    const auto out = Resampler::process (in, from, to);

    CHECK (out.getNumSamples() == Resampler::getOutputLength (in.getNumSamples(), from, to));

    // Within 0.001 of a 0.5 amplitude sine: better than -60 dB error, i.e. < 0.02 dB amplitude change.
    CHECK (maxSineError (out, to, frequency, std::pow (10.0, -6.0 / 20.0)) < 1e-3);
}

TEST_CASE ("DC passes with unity gain", "[resampler]")
{
    juce::AudioBuffer<float> in (1, 48000);
    in.clear();
    in.applyGain (0.0f);
    for (int i = 0; i < in.getNumSamples(); ++i)
        in.setSample (0, i, 0.5f);

    const auto out = Resampler::process (in, 48000.0, 44100.0);
    for (int i = 2000; i < out.getNumSamples() - 2000; ++i)
        REQUIRE (out.getSample (0, i) == Approx (0.5f).margin (1e-4));
}

TEST_CASE ("Downsampling rejects content above the new Nyquist frequency", "[resampler]")
{
    SECTION ("30 kHz at 96 kHz -> 44.1 kHz")
    {
        const auto in = testing::makeSine (96000.0, 1.0, 30000.0, 0.0, 1);
        const auto out = Resampler::process (in, 96000.0, 44100.0);
        CHECK (juce::Decibels::gainToDecibels (rmsMiddle (out, 44100.0) / std::sqrt (0.5)) < -70.0);
    }

    SECTION ("23 kHz at 48 kHz -> 44.1 kHz")
    {
        const auto in = testing::makeSine (48000.0, 1.0, 23000.0, 0.0, 1);
        const auto out = Resampler::process (in, 48000.0, 44100.0);
        CHECK (juce::Decibels::gainToDecibels (rmsMiddle (out, 44100.0) / std::sqrt (0.5)) < -70.0);
    }
}

TEST_CASE ("Resampling can be cancelled", "[resampler]")
{
    const auto in = testing::makeSine (44100.0, 2.0, 440.0, -6.0);
    int calls = 0;
    const auto out = Resampler::process (in, 44100.0, 48000.0, [&calls] (float) { return ++calls < 2; });

    CHECK (out.getNumSamples() == 0);
    CHECK (calls == 2);
}
