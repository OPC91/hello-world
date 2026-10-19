#include "GrooveEngine.h"

#include <cmath>

#include <juce_core/juce_core.h>

namespace bf::groove
{

namespace
{
    GrooveTemplate makeTemplate (float swing, std::array<float, 4> timingPerBeat, std::array<float, 4> velocityPerBeat)
    {
        GrooveTemplate t;
        t.swing = swing;

        for (int i = 0; i < 16; ++i)
        {
            t.timing[(size_t) i] = timingPerBeat[(size_t) (i % 4)];
            t.velocity[(size_t) i] = velocityPerBeat[(size_t) (i % 4)];
        }

        return t;
    }

    std::array<GrooveTemplate, numFactoryTemplates> buildFactoryTemplates()
    {
        std::array<GrooveTemplate, numFactoryTemplates> t;

        // Straight: quantised, no velocity shaping.
        t[0] = GrooveTemplate {};

        // Light Shuffle: a little swing, off-16ths a touch softer.
        t[1] = makeTemplate (0.54f, { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 0.86f, 0.95f, 0.84f });

        // Heavy Swing: deep shuffle with clearly weaker off-beats.
        t[2] = makeTemplate (0.63f, { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 0.74f, 0.92f, 0.72f });

        // Amen Feel: modelled on the push and pull of a played funk break. The backbeat snare
        // sits slightly late, the "e"s rush a little and the "a"s drag, with uneven dynamics.
        t[3].swing = 0.53f;
        t[3].timing   = { 0.00f, -0.04f, 0.02f, 0.05f,   0.06f, -0.03f, 0.01f, 0.07f,
                          0.00f, -0.05f, 0.03f, 0.04f,   0.05f, -0.02f, 0.02f, 0.08f };
        t[3].velocity = { 1.00f, 0.70f, 0.88f, 0.76f,   1.00f, 0.68f, 0.84f, 0.80f,
                          0.97f, 0.66f, 0.90f, 0.74f,   1.00f, 0.70f, 0.82f, 0.86f };

        // Rolling: steady forward motion, 8th off-beats pushed slightly early, strong
        // 1-e-and-a accent contour for hats.
        t[4] = makeTemplate (0.56f, { 0.0f, 0.0f, -0.03f, 0.0f }, { 1.0f, 0.72f, 0.9f, 0.78f });

        return t;
    }
}

const GrooveTemplate& factoryTemplate (int index) noexcept
{
    static const auto templates = buildFactoryTemplates();
    return templates[(size_t) juce::jlimit (0, numFactoryTemplates - 1, index)];
}

double warp (double ppq, double swing, double unitPpq) noexcept
{
    const double cycle = std::floor (ppq / unitPpq);
    const double x = ppq - cycle * unitPpq; // 0..unit
    const double half = unitPpq * 0.5;
    const double split = swing * unitPpq;

    const double warped = x < half ? x * (split / half)
                                   : split + (x - half) * ((unitPpq - split) / half);

    return cycle * unitPpq + warped;
}

double applySwing (double ppq, double swing, bool swingEighths) noexcept
{
    if (swing <= 0.5)
        return ppq;

    auto result = warp (ppq, swing, 0.5);

    if (swingEighths)
        result = warp (result, swing, 1.0);

    return result;
}

int sixteenthInBar (double ppq) noexcept
{
    const auto s = (std::int64_t) std::floor (ppq * 4.0 + 1.0e-9);
    return (int) (((s % 16) + 16) % 16);
}

float random01 (std::uint32_t seed, std::int64_t a, std::uint32_t b, std::uint32_t c) noexcept
{
    // splitmix64 over the combined key.
    std::uint64_t z = ((std::uint64_t) seed << 32) ^ (std::uint64_t) a * 0x9E3779B97F4A7C15ull
                      ^ ((std::uint64_t) b << 48) ^ ((std::uint64_t) c << 40);
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (float) ((double) (z >> 40) / (double) (1ull << 24));
}

float velocityToGain (float velocity, float depth) noexcept
{
    const float v = juce::jlimit (0.0f, 1.0f, velocity);
    const float curved = v * v;
    return 1.0f + depth * (curved - 1.0f);
}

} // namespace bf::groove
