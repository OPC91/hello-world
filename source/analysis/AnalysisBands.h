#pragma once

#include <array>
#include <cmath>

namespace benchmark::analysis
{
    /** ISO 1/3-octave band centres, 20 Hz - 20 kHz (31 bands). */
    inline constexpr std::array<float, 31> thirdOctaveCentres {
        20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f,
        200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f,
        2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f
    };

    inline constexpr int numThirdOctaveBands = (int) thirdOctaveCentres.size();

    /** Exact base-2 band edges around a nominal centre (1/6 octave either side). */
    inline float thirdOctaveLow (float centre)  { return centre * std::pow (2.0f, -1.0f / 6.0f); }
    inline float thirdOctaveHigh (float centre) { return centre * std::pow (2.0f, 1.0f / 6.0f); }

    /** Points of the smoothed spectrum curve, log-spaced 20 Hz - 20 kHz. */
    inline constexpr int numCurvePoints = 240;
    inline constexpr float minFrequency = 20.0f;
    inline constexpr float maxFrequency = 20000.0f;

    inline float curvePointFrequency (int index)
    {
        return minFrequency * std::pow (maxFrequency / minFrequency, (float) index / (float) (numCurvePoints - 1));
    }

    /** The four stereo / band-solo bands. Crossovers shared with the band-solo filter. */
    inline constexpr int numStereoBands = 4;
    inline constexpr std::array<float, numStereoBands + 1> stereoBandEdges { 20.0f, 150.0f, 1000.0f, 5000.0f, 20000.0f };
    inline constexpr std::array<const char*, numStereoBands> stereoBandNames { "Low", "Low-mid", "High-mid", "High" };

    /** Wider ranges used for plain-language tonal hints. */
    struct TonalRange
    {
        float low, high;
        const char* label;    // e.g. "200-500 Hz"
        const char* name;     // e.g. "low-mids"
    };

    inline constexpr std::array<TonalRange, 7> tonalRanges { {
        { 20.0f,    60.0f,    "20-60 Hz",    "sub bass" },
        { 60.0f,    200.0f,   "60-200 Hz",   "bass" },
        { 200.0f,   500.0f,   "200-500 Hz",  "low-mids" },
        { 500.0f,   2000.0f,  "500 Hz-2 kHz", "mids" },
        { 2000.0f,  5000.0f,  "2-5 kHz",     "upper mids" },
        { 5000.0f,  10000.0f, "5-10 kHz",    "presence" },
        { 10000.0f, 20000.0f, "10-20 kHz",   "air" },
    } };

    inline constexpr int numTonalRanges = (int) tonalRanges.size();

    /** Short-term loudness history kept for the dynamics graph (10 values per second). */
    inline constexpr int historyLength = 600; // 60 s
}
