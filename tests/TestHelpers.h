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

#include "engine/SeamEngine.h"

#include <random>

namespace seam::test
{
    /** Stereo test signal with a deterministic noise generator. */
    struct Stereo
    {
        std::vector<float> left, right;
        explicit Stereo (size_t n = 0) : left (n), right (n) {}
        size_t size() const { return left.size(); }
    };

    inline Stereo noise (size_t n, float level = 0.3f, unsigned seed = 1)
    {
        Stereo s (n);
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-level, level);
        for (size_t i = 0; i < n; ++i) { s.left[i] = dist (rng); s.right[i] = dist (rng); }
        return s;
    }

    /** Runs the engine over a whole signal in blocks of varying size, with a settings callback per block
        (given the block's start sample). Returns the processed copy. */
    template <typename SettingsFn>
    Stereo runEngine (SeamEngine& engine, FakeHost& host, const Stereo& input, SettingsFn&& settingsAt,
                      int baseBlock = 256, bool vary = true, bool playing = true)
    {
        Stereo out = input;
        size_t pos = 0;
        int counter = 0;
        while (pos < out.size())
        {
            const int block = vary ? baseBlock / 2 + (counter++ * 61) % baseBlock + 1 : baseBlock;
            const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (block), out.size() - pos));
            float* channels[2] { out.left.data() + pos, out.right.data() + pos };
            auto t = host.next (n);
            t.playing = playing;
            engine.process (channels, 2, n, t, settingsAt (pos));
            pos += static_cast<size_t> (n);
        }
        return out;
    }

    inline size_t firstMismatch (const Stereo& a, const Stereo& b, size_t from = 0)
    {
        for (size_t i = from; i < a.size(); ++i)
            if (a.left[i] != b.left[i] || a.right[i] != b.right[i])
                return i;
        return a.size();
    }

    inline double rms (const std::vector<float>& x, size_t from, size_t to)
    {
        double sum = 0.0;
        for (size_t i = from; i < to; ++i) sum += static_cast<double> (x[i]) * x[i];
        return to > from ? std::sqrt (sum / static_cast<double> (to - from)) : 0.0;
    }
}
