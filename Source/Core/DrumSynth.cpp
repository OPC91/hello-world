#include "DrumSynth.h"

#include <cmath>

namespace bf::DrumSynth
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    /** Small deterministic noise source so kits render identically every time. */
    struct Noise
    {
        explicit Noise (std::uint32_t seed) : state (seed == 0 ? 0x9E3779B9u : seed) {}

        float next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) ((double) state / 2147483648.0 - 1.0);
        }

        std::uint32_t state;
    };

    float saturate (float x, double drive) noexcept
    {
        if (drive <= 1.0)
            return x;

        return (float) (std::tanh (x * drive) / std::tanh (drive));
    }

    void normalise (juce::AudioBuffer<float>& b, float peakTarget)
    {
        const auto peak = b.getMagnitude (0, b.getNumSamples());

        if (peak > 0.0f)
            b.applyGain (peakTarget / peak);
    }

    /** Short raised-cosine fade at the tail so nothing clicks when the buffer ends. */
    void fadeTail (juce::AudioBuffer<float>& b, double sampleRate, double seconds)
    {
        const int n = juce::jmin (b.getNumSamples(), (int) (seconds * sampleRate));
        const int start = b.getNumSamples() - n;

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getWritePointer (ch);

            for (int i = 0; i < n; ++i)
                d[start + i] *= (float) (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * (double) i / (double) n));
        }
    }
}

juce::AudioBuffer<float> renderKick (double sampleRate, const KickParams& p, std::uint32_t seed)
{
    const int numSamples = juce::jmax (1, (int) (p.length * sampleRate));
    juce::AudioBuffer<float> out (1, numSamples);
    auto* d = out.getWritePointer (0);

    Noise noise (seed);
    double phase = 0.0;
    float hpState = 0.0f, prevNoise = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const double t = (double) i / sampleRate;

        // Body: sine with exponential pitch sweep, a touch of 2nd harmonic for "knock".
        const double freq = p.endHz + (p.startHz - p.endHz) * std::exp (-t / p.pitchDecay);
        phase += twoPi * freq / sampleRate;
        const double attack = juce::jmin (1.0, t / 0.0008);
        const double amp = attack * std::exp (-t / p.ampDecay);
        double body = std::sin (phase) + 0.12 * std::sin (2.0 * phase) * std::exp (-t / 0.02);

        // Click: high-passed noise burst plus a fast sine chirp, both a few ms long.
        const float n = noise.next();
        hpState = 0.86f * (hpState + n - prevNoise);
        prevNoise = n;
        const double clickEnv = std::exp (-t / 0.0025);
        const double click = p.click * clickEnv * (0.35 * hpState + 0.5 * std::sin (twoPi * 1600.0 * t));

        d[i] = saturate ((float) (amp * body + click), p.drive);
    }

    normalise (out, 0.95f);
    fadeTail (out, sampleRate, 0.01);
    return out;
}

} // namespace bf::DrumSynth
