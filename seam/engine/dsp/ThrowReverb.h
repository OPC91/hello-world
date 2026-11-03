#pragma once

#include <array>
#include <vector>

namespace seam::engine::dsp
{
    /** A big, washy stereo reverb for snare throws (Freeverb topology: 8 damped combs + 4 allpasses per side).
        All buffers are sized in prepare(); process() never allocates. */
    class ThrowReverb
    {
    public:
        void prepare (double sampleRate);
        void reset() noexcept;

        /** One stereo sample in, wet only out. */
        void process (float inL, float inR, float& outL, float& outR) noexcept;

        /** True when the tail has decayed below audibility (or was just cleared). */
        bool isQuiet() const noexcept { return energy < 1.0e-9f; }

    private:
        struct Comb
        {
            std::vector<float> buffer;
            size_t index = 0;
            float store = 0.0f;
            float process (float x, float feedback, float damp) noexcept
            {
                const float out = buffer[index];
                store = out * (1.0f - damp) + store * damp;
                buffer[index] = x + store * feedback;
                if (++index >= buffer.size()) index = 0;
                return out;
            }
        };
        struct Allpass
        {
            std::vector<float> buffer;
            size_t index = 0;
            float process (float x) noexcept
            {
                const float delayed = buffer[index];
                const float out = delayed - x;
                buffer[index] = x + delayed * 0.5f;
                if (++index >= buffer.size()) index = 0;
                return out;
            }
        };

        std::array<std::array<Comb, 8>, 2> combs;
        std::array<std::array<Allpass, 4>, 2> allpasses;
        float energy = 0.0f;
        float energyCoeff = 0.999f;
    };
}
