#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace exnilo
{
    /** Small deterministic PRNG (xoshiro256** seeded through SplitMix64).

        Every draw goes through this class instead of <random> distributions, because the standard
        distributions are implementation-defined: the same seed would give different melodies on MSVC,
        libc++ and libstdc++. With this class, a seed means the same melody on every platform. */
    class Random
    {
    public:
        explicit Random (std::uint64_t seed) noexcept
        {
            auto s = seed;
            for (auto& word : state)
                word = splitMix (s);
        }

        std::uint64_t next() noexcept
        {
            const auto result = rotl (state[1] * 5, 7) * 9;
            const auto t = state[1] << 17;
            state[2] ^= state[0];
            state[3] ^= state[1];
            state[1] ^= state[2];
            state[0] ^= state[3];
            state[2] ^= t;
            state[3] = rotl (state[3], 45);
            return result;
        }

        /** Uniform in [0, 1). */
        double uniform() noexcept { return static_cast<double> (next() >> 11) * 0x1.0p-53; }

        /** Uniform in [lo, hi). */
        double uniform (double lo, double hi) noexcept { return lo + (hi - lo) * uniform(); }

        /** Uniform integer in [lo, hi] (inclusive). */
        int uniformInt (int lo, int hi) noexcept
        {
            if (hi <= lo)
                return lo;
            const auto range = static_cast<std::uint64_t> (hi - lo) + 1;
            return lo + static_cast<int> (((next() >> 32) * range) >> 32);
        }

        bool chance (double probability) noexcept { return uniform() < probability; }

        /** Index drawn in proportion to `weights`; returns weights.size() if they sum to zero. */
        std::size_t weightedIndex (std::span<const double> weights) noexcept
        {
            double total = 0.0;
            for (const double w : weights)
                total += w > 0.0 ? w : 0.0;
            if (total <= 0.0)
                return weights.size();

            double pick = uniform() * total;
            std::size_t lastPositive = weights.size();
            for (std::size_t i = 0; i < weights.size(); ++i)
            {
                if (weights[i] <= 0.0)
                    continue;
                lastPositive = i;
                pick -= weights[i];
                if (pick < 0.0)
                    return i;
            }
            return lastPositive; // rounding at the very end of the range
        }

        /** An independent stream derived from this one's seed state, without advancing it. Used so that,
            for example, changing the rhythm settings does not change which pitches the pitch stage draws. */
        Random fork (std::uint64_t salt) const noexcept
        {
            return Random (state[0] ^ rotl (state[2], 17) ^ (salt * 0x9E3779B97F4A7C15ull));
        }

    private:
        static std::uint64_t rotl (std::uint64_t x, int k) noexcept { return (x << k) | (x >> (64 - k)); }

        static std::uint64_t splitMix (std::uint64_t& s) noexcept
        {
            auto z = (s += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        std::array<std::uint64_t, 4> state {};
    };
}
