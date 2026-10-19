#include "DrumSynth.h"

#include <cmath>

namespace bf::DrumSynth
{

namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    /** Small deterministic noise source so kits render identically every time. */
    struct Noise
    {
        explicit Noise (std::uint32_t seed) : state (seed == 0 ? 0x9E3779B9u : seed * 2654435761u + 1u) {}

        float next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) ((double) state / 2147483648.0 - 1.0);
        }

        std::uint32_t state;
    };

    /** RBJ biquad, direct form II transposed (offline use only). */
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        static Biquad make (int type, double sr, double freq, double q)
        {
            Biquad f;
            const double w = twoPi * juce::jlimit (10.0, sr * 0.49, freq) / sr;
            const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            double b0 = 0, b1 = 0, b2 = 0;
            const double a0 = 1.0 + alpha;

            switch (type)
            {
                case 0: b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; break;      // low-pass
                case 1: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; break;   // high-pass
                default: b0 = alpha; b1 = 0; b2 = -alpha; break;                       // band-pass
            }

            f.b0 = b0 / a0; f.b1 = b1 / a0; f.b2 = b2 / a0;
            f.a1 = -2.0 * cw / a0; f.a2 = (1.0 - alpha) / a0;
            return f;
        }

        static Biquad lowPass (double sr, double f, double q = 0.707)  { return make (0, sr, f, q); }
        static Biquad highPass (double sr, double f, double q = 0.707) { return make (1, sr, f, q); }
        static Biquad bandPass (double sr, double f, double q = 1.0)   { return make (2, sr, f, q); }

        float process (float xIn) noexcept
        {
            const double x = xIn;
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };

    /** Band-limited square via PolyBLEP, for alias-free metallic clusters. */
    struct BlepSquare
    {
        double phase = 0.0, inc = 0.0;

        static double blep (double t, double dt) noexcept
        {
            if (t < dt)        { t /= dt; return t + t - t * t - 1.0; }
            if (t > 1.0 - dt)  { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
            return 0.0;
        }

        float next() noexcept
        {
            double v = phase < 0.5 ? 1.0 : -1.0;
            v += blep (phase, inc);
            v -= blep (std::fmod (phase + 0.5, 1.0), inc);
            phase += inc;
            if (phase >= 1.0) phase -= 1.0;
            return (float) v;
        }
    };

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
                d[start + i] *= (float) (0.5 + 0.5 * std::cos (pi * (double) i / (double) n));
        }
    }

    float saturate (float x, double drive) noexcept
    {
        if (drive <= 1.0)
            return x;

        return (float) (std::tanh (x * drive) / std::tanh (drive));
    }

    int samplesFor (double seconds, double sr) { return juce::jmax (1, (int) (seconds * sr)); }

    // TR-808-style metallic oscillator cluster (Hz), inharmonic on purpose.
    constexpr double metalFreqs[] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
}

// -----------------------------------------------------------------------------

juce::AudioBuffer<float> renderKick (double sr, const KickParams& p, std::uint32_t seed)
{
    const int n = samplesFor (p.length, sr);
    juce::AudioBuffer<float> out (1, n);
    auto* d = out.getWritePointer (0);

    Noise noise (seed);
    auto clickHp = Biquad::highPass (sr, 2500.0);
    double phase = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;

        // Body: sine with an exponential pitch sweep plus a brief 2nd harmonic for knock.
        const double freq = p.endHz + (p.startHz - p.endHz) * std::exp (-t / p.pitchDecay);
        phase += twoPi * freq / sr;
        const double attack = juce::jmin (1.0, t / 0.0008);
        const double amp = attack * std::exp (-t / p.ampDecay);
        const double body = std::sin (phase) + 0.12 * std::sin (2.0 * phase) * std::exp (-t / 0.02);

        // Click: high-passed noise plus a fast sine blip, a few ms long.
        const double clickEnv = std::exp (-t / 0.0025);
        const double click = p.click * clickEnv * (0.4 * clickHp.process (noise.next()) + 0.5 * std::sin (twoPi * 1600.0 * t));

        d[i] = saturate ((float) (amp * body + click), p.drive);
    }

    normalise (out, 0.95f);
    fadeTail (out, sr, 0.01);
    return out;
}

juce::AudioBuffer<float> renderSnare (double sr, const SnareParams& p, std::uint32_t seed)
{
    const int n = samplesFor (p.length, sr);
    juce::AudioBuffer<float> out (1, n);
    auto* d = out.getWritePointer (0);

    Noise noise (seed);
    auto wiresHp = Biquad::highPass (sr, p.noiseHp, 0.6);
    auto wiresLp = Biquad::lowPass (sr, p.noiseLp, 0.6);
    auto snapBp = Biquad::bandPass (sr, 4200.0, 0.9);
    auto clickHp = Biquad::highPass (sr, 3000.0);
    double ph1 = 0.0, ph2 = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;

        // Shell: two modes (fundamental and the first membrane overtone) with a quick pitch drop.
        const double drop = 1.0 + 0.35 * std::exp (-t / 0.012);
        ph1 += twoPi * p.toneHz * drop / sr;
        ph2 += twoPi * p.toneHz * 1.78 * drop / sr;
        const double tone = p.toneLevel * std::exp (-t / p.toneDecay) * (std::sin (ph1) + 0.55 * std::sin (ph2));

        // Wires: filtered noise with a snappier band on top.
        const float w = noise.next();
        const double wires = p.noiseLevel * (std::exp (-t / p.noiseDecay) * wiresLp.process (wiresHp.process (w))
                                              + 0.6 * std::exp (-t / (p.noiseDecay * 0.35)) * snapBp.process (w));

        // Stick transient.
        const double click = p.click * std::exp (-t / 0.0015) * clickHp.process (noise.next());

        const double attack = juce::jmin (1.0, t / 0.0004);
        d[i] = saturate ((float) (attack * (tone + wires + click)), p.drive);
    }

    normalise (out, 0.95f);
    fadeTail (out, sr, 0.02);
    return out;
}

juce::AudioBuffer<float> renderMetal (double sr, const MetalParams& p, std::uint32_t seed)
{
    const int n = samplesFor (p.length, sr);
    juce::AudioBuffer<float> out (2, n);

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = out.getWritePointer (ch);
        Noise noise (seed + (std::uint32_t) ch * 7919u);
        auto bp = Biquad::bandPass (sr, p.bandHz, 0.8);
        auto hp1 = Biquad::highPass (sr, p.hp, 0.7);
        auto hp2 = Biquad::highPass (sr, p.hp, 0.7);
        auto bellBp = Biquad::bandPass (sr, 3100.0 * p.tune, 2.0);

        // Each channel's cluster is detuned slightly differently for a natural stereo image.
        std::array<BlepSquare, 6> osc {};
        const double detune = 1.0 + (ch == 0 ? -1.0 : 1.0) * 0.004 * p.width;

        for (size_t k = 0; k < osc.size(); ++k)
            osc[k].inc = metalFreqs[k] * 1.47 * p.tune * detune / sr;

        double bellPhase[3] = {};
        constexpr double bellRatios[] = { 1.0, 2.41, 3.73 };

        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            float cluster = 0.0f;

            for (auto& o : osc)
                cluster += o.next();

            float x = bp.process (cluster * 0.2f) + (float) p.noise * noise.next() * 0.5f;
            x = hp2.process (hp1.process (x));

            double env = std::exp (-t / p.decay) * juce::jmin (1.0, t / 0.0003);
            double y = x * env;

            if (p.bell > 0.0)
            {
                double bell = 0.0;
                for (int k = 0; k < 3; ++k)
                {
                    bellPhase[k] += twoPi * 3100.0 * p.tune * bellRatios[k] * detune / sr;
                    bell += std::sin (bellPhase[k]) / (k + 1);
                }
                y += p.bell * (0.5 * bell + bellBp.process (cluster * 0.1f)) * std::exp (-t / (p.decay * 0.6));
            }

            d[i] = (float) y;
        }
    }

    normalise (out, 0.9f);
    fadeTail (out, sr, 0.02);
    return out;
}

juce::AudioBuffer<float> renderRimshot (double sr, double tune, std::uint32_t seed)
{
    const int n = samplesFor (0.12, sr);
    juce::AudioBuffer<float> out (1, n);
    auto* d = out.getWritePointer (0);
    Noise noise (seed);
    auto hp = Biquad::highPass (sr, 2000.0);
    auto bp = Biquad::bandPass (sr, 1700.0 * tune, 3.0);
    double ph1 = 0.0, ph2 = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        ph1 += twoPi * 480.0 * tune / sr;
        ph2 += twoPi * 1720.0 * tune / sr;
        const double tone = std::exp (-t / 0.012) * (0.6 * std::sin (ph1) + 0.8 * std::sin (ph2));
        const double ring = 2.5 * bp.process ((float) tone);
        const double click = std::exp (-t / 0.0012) * hp.process (noise.next());
        d[i] = saturate ((float) (tone + ring * std::exp (-t / 0.03) + click), 2.2);
    }

    normalise (out, 0.9f);
    fadeTail (out, sr, 0.01);
    return out;
}

juce::AudioBuffer<float> renderClap (double sr, double tone, std::uint32_t seed)
{
    const int n = samplesFor (0.4, sr);
    juce::AudioBuffer<float> out (2, n);

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = out.getWritePointer (ch);
        Noise noise (seed + (std::uint32_t) ch * 31u);
        auto bp = Biquad::bandPass (sr, 1150.0 * tone, 1.4);
        auto hp = Biquad::highPass (sr, 600.0);
        constexpr double burstStarts[] = { 0.0, 0.011, 0.021, 0.034 }; // several hands, slightly apart

        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            double env = 0.0;

            for (size_t b = 0; b < 3; ++b)
                if (t >= burstStarts[b] && t < burstStarts[b + 1])
                    env = std::exp (-(t - burstStarts[b]) / 0.0035);

            if (t >= burstStarts[3])
                env = std::exp (-(t - burstStarts[3]) / 0.09);

            d[i] = (float) (env * hp.process (bp.process (noise.next()) * 3.0f));
        }
    }

    normalise (out, 0.9f);
    fadeTail (out, sr, 0.02);
    return out;
}

juce::AudioBuffer<float> renderShaker (double sr, double brightness, std::uint32_t seed)
{
    const int n = samplesFor (0.16, sr);
    juce::AudioBuffer<float> out (2, n);

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = out.getWritePointer (ch);
        Noise noise (seed + (std::uint32_t) ch * 101u);
        Noise grains (seed * 17u + (std::uint32_t) ch);
        auto bp = Biquad::bandPass (sr, 6500.0 * brightness, 1.2);
        auto hp = Biquad::highPass (sr, 3500.0 * brightness);
        float grainEnv = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            // Slow-ish attack then decay: the beads hit the shell as a swarm, not a single click.
            const double env = (1.0 - std::exp (-t / 0.012)) * std::exp (-t / 0.045);

            // Random bead impacts give the grainy texture.
            if (grains.next() > 0.995f)
                grainEnv = 1.0f;
            grainEnv *= 0.995f;

            d[i] = (float) (env * hp.process (bp.process (noise.next())) * (0.55f + grainEnv));
        }
    }

    normalise (out, 0.85f);
    fadeTail (out, sr, 0.02);
    return out;
}

juce::AudioBuffer<float> renderReverseCrash (double sr, double length, double tune, std::uint32_t seed)
{
    MetalParams crash;
    crash.tune = 0.62 * tune;
    crash.decay = length * 0.38;
    crash.length = length;
    crash.hp = 2800.0;
    crash.bandHz = 7000.0;
    crash.noise = 0.9;
    crash.width = 1.0;

    auto out = renderMetal (sr, crash, seed);

    for (int ch = 0; ch < out.getNumChannels(); ++ch)
    {
        auto* d = out.getWritePointer (ch);
        std::reverse (d, d + out.getNumSamples());
    }

    // The swell ends abruptly on the downbeat; give it a tiny fade so it doesn't click.
    const int fade = samplesFor (0.004, sr);
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        out.applyGainRamp (ch, out.getNumSamples() - fade, fade, 1.0f, 0.0f);

    out.applyGainRamp (0, juce::jmin (out.getNumSamples(), samplesFor (0.05, sr)), 0.0f, 1.0f);
    normalise (out, 0.85f);
    return out;
}

// -----------------------------------------------------------------------------

void applyColour (juce::AudioBuffer<float>& b, double sr, const Colour& c, float peak)
{
    const bool crushRate = c.crushRate > 0.0 && c.crushRate < sr;
    const double holdStep = crushRate ? c.crushRate / sr : 1.0;
    const float levels = c.crushBits > 0 ? (float) (1 << (c.crushBits - 1)) : 0.0f;

    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        auto* d = b.getWritePointer (ch);
        double holdPhase = 1.0;
        float held = 0.0f;
        auto smooth = Biquad::lowPass (sr, crushRate ? c.crushRate * 0.45 : sr * 0.45);

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float x = saturate (d[i], c.drive);

            if (crushRate)
            {
                holdPhase += holdStep;
                if (holdPhase >= 1.0) { holdPhase -= 1.0; held = x; }
                x = smooth.process (held); // a vintage DAC's reconstruction filter softens the steps
            }

            if (levels > 0.0f)
                x = std::round (x * levels) / levels;

            d[i] = x;
        }
    }

    normalise (b, peak);
}

} // namespace bf::DrumSynth
