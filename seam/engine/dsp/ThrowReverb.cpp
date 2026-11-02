#include "ThrowReverb.h"

#include <algorithm>
#include <cmath>

namespace seam::engine::dsp
{
    namespace
    {
        constexpr int combTunings[8] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        constexpr int allpassTunings[4] { 556, 441, 341, 225 };
        constexpr int stereoSpread = 23;
        constexpr float feedback = 0.86f;   // long, washy
        constexpr float damping = 0.32f;
        constexpr float inputGain = 0.015f;
        constexpr float wetGain = 3.0f;
    }

    void ThrowReverb::prepare (double sampleRate)
    {
        const double scale = sampleRate / 44100.0;
        for (size_t side = 0; side < 2; ++side)
        {
            const int spread = side == 0 ? 0 : stereoSpread;
            for (size_t i = 0; i < 8; ++i)
                combs[side][i].buffer.assign (static_cast<size_t> (std::max (1.0, std::round ((combTunings[i] + spread) * scale))), 0.0f);
            for (size_t i = 0; i < 4; ++i)
                allpasses[side][i].buffer.assign (static_cast<size_t> (std::max (1.0, std::round ((allpassTunings[i] + spread) * scale))), 0.0f);
        }
        energyCoeff = static_cast<float> (std::exp (-1.0 / (0.05 * sampleRate)));
        reset();
    }

    void ThrowReverb::reset() noexcept
    {
        for (auto& side : combs)
            for (auto& c : side) { std::fill (c.buffer.begin(), c.buffer.end(), 0.0f); c.index = 0; c.store = 0.0f; }
        for (auto& side : allpasses)
            for (auto& a : side) { std::fill (a.buffer.begin(), a.buffer.end(), 0.0f); a.index = 0; }
        energy = 0.0f;
    }

    void ThrowReverb::process (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float input = (inL + inR) * inputGain;
        float sides[2] { 0.0f, 0.0f };
        for (size_t side = 0; side < 2; ++side)
        {
            float acc = 0.0f;
            for (auto& c : combs[side])
                acc += c.process (input, feedback, damping);
            for (auto& a : allpasses[side])
                acc = a.process (acc);
            sides[side] = acc * wetGain;
        }
        outL = sides[0];
        outR = sides[1];
        energy = std::max (energy * energyCoeff, std::max (sides[0] * sides[0], sides[1] * sides[1]) + input * input * 100.0f);
        if (energy < 1.0e-12f)
            energy = 0.0f;
    }
}
