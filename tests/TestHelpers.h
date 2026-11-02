#pragma once

#include "engine/PhraseTracker.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace seam::test
{
    using namespace seam::engine;

    inline TransportSnapshot playingAt (double ppq, double bpm = 174.0, double sampleRate = 48000.0, int numSamples = 512,
                                        int num = 4, int den = 4)
    {
        TransportSnapshot t;
        t.playing = true;
        t.hasPosition = true;
        t.ppq = ppq;
        t.bpm = bpm;
        t.sampleRate = sampleRate;
        t.numSamples = numSamples;
        t.timeSigNumerator = num;
        t.timeSigDenominator = den;
        return t;
    }

    /** A host that plays continuously from `startPpq`, handing out blocks of varying size. */
    struct FakeHost
    {
        double ppq = 0.0, bpm = 174.0, sampleRate = 48000.0;
        int num = 4, den = 4;

        TransportSnapshot next (int numSamples)
        {
            auto t = playingAt (ppq, bpm, sampleRate, numSamples, num, den);
            ppq += numSamples * bpm / (60.0 * sampleRate);
            return t;
        }
        double samplesPerQuarter() const { return sampleRate * 60.0 / bpm; }
    };
}
