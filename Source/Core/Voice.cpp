#include "Voice.h"
#include "GrooveEngine.h"

#include <cmath>

namespace bf
{

void SvfCoeffs::set (float cutoffHz, float resonance, double sampleRate) noexcept
{
    const float fc = juce::jlimit (10.0f, (float) (sampleRate * 0.49), cutoffHz);
    const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sampleRate);
    k = 2.0f - 1.9f * juce::jlimit (0.0f, 1.0f, resonance); // 2 = Butterworth-ish damping, low = resonant
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

namespace
{
    float coefForTime (float ms, double sampleRate) noexcept
    {
        // Exponential decay reaching -60 dB after `ms`.
        const double samples = juce::jmax (1.0, (double) ms * 0.001 * sampleRate);
        return (float) std::exp (std::log (0.001) / samples);
    }

    /** 4-point, 3rd-order Hermite interpolation. */
    inline float hermite (const float* d, int len, double pos) noexcept
    {
        const int i = (int) pos;
        const float f = (float) (pos - i);
        const float xm1 = i > 0 ? d[i - 1] : 0.0f;
        const float x0 = d[i];
        const float x1 = i + 1 < len ? d[i + 1] : 0.0f;
        const float x2 = i + 2 < len ? d[i + 2] : 0.0f;
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float bNeg = w + a;
        return ((((a * f) - bNeg) * f + c) * f + x0);
    }
}

void Voice::start (const Trigger& t, const TrackParams& tp, float velocityDepth, double sampleRate) noexcept
{
    sample = t.sample;
    active = sample != nullptr && sample->buffer.getNumSamples() > 0;

    if (! active)
        return;

    track = t.track;
    age = t.age;
    delay = t.delaySamples;
    chokeAt = -1;
    velocity = t.velocity;

    const float semis = tp.pitch + t.pitchSemis + tp.velToPitch * (t.velocity - 1.0f);
    increment = (sample->sampleRate / sampleRate) * std::pow (2.0, (double) semis / 12.0);
    position = juce::jlimit (0.0, (double) sample->buffer.getNumSamples() - 1.0, (double) tp.start * sample->buffer.getNumSamples());

    // Equal-power pan, scaled by the velocity response.
    const float gain = groove::velocityToGain (t.velocity, velocityDepth);
    const float angle = (tp.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    gainL = gain * std::cos (angle) * juce::MathConstants<float>::sqrt2;
    gainR = gain * std::sin (angle) * juce::MathConstants<float>::sqrt2;

    envMode = tp.envMode;
    stage = Stage::Attack;
    env = 0.0f;
    const double attackSamples = (double) tp.attackMs * 0.001 * sampleRate;
    attackStep = attackSamples < 1.0 ? 1.0f : (float) (1.0 / attackSamples);
    sustain = envMode == EnvMode::ADSR ? tp.sustain : 0.0f;
    decayCoef = (envMode == EnvMode::OneShot && tp.decayMs >= kDecayInfiniteMs) ? 1.0f : coefForTime (tp.decayMs, sampleRate);
    releaseCoef = coefForTime (tp.releaseMs, sampleRate);
    gateRemaining = envMode == EnvMode::ADSR ? juce::jmax (1.0, t.gateSamples) : 0.0;

    // Velocity -> filter: positive amounts darken soft hits (up to 5 octaves).
    filterMod = std::exp2 (tp.velToFilter * 5.0f * (t.velocity - 1.0f));

    for (auto& s : lp) s.reset();
    for (auto& s : hp) s.reset();

    updateFilters (tp, sampleRate);
}

void Voice::updateFilters (const TrackParams& tp, double sampleRate) noexcept
{
    const float lpHz = juce::jmin (tp.lowPassHz * filterMod, 20000.0f);
    lpOn = lpHz < 19900.0f;
    hpOn = tp.highPassHz > 21.0f;

    if (lpOn) lpCoeffs.set (lpHz, tp.resonance, sampleRate);
    if (hpOn) hpCoeffs.set (tp.highPassHz, tp.resonance, sampleRate);
}

void Voice::choke (int atSample) noexcept
{
    if (active && (chokeAt < 0 || atSample < chokeAt))
        chokeAt = atSample;
}

void Voice::kill() noexcept
{
    active = false;
    stage = Stage::Done;
    sample = nullptr;
}

float Voice::nextEnvelope() noexcept
{
    switch (stage)
    {
        case Stage::Attack:
            env += attackStep;
            if (env >= 1.0f) { env = 1.0f; stage = Stage::Decay; }
            break;

        case Stage::Decay:
            env = sustain + (env - sustain) * decayCoef;
            if (envMode == EnvMode::OneShot && env < 1.0e-4f) stage = Stage::Done;
            break;

        case Stage::Release:
            env *= releaseCoef;
            if (env < 1.0e-4f) stage = Stage::Done;
            break;

        case Stage::Done:
            env = 0.0f;
            break;
    }

    return env;
}

void Voice::render (float* left, float* right, int numSamples, const TrackParams& tp, double sampleRate) noexcept
{
    if (! active)
        return;

    updateFilters (tp, sampleRate);

    const auto& buf = sample->buffer;
    const int len = buf.getNumSamples();
    const float* srcL = buf.getReadPointer (0);
    const float* srcR = buf.getReadPointer (buf.getNumChannels() > 1 ? 1 : 0);
    const float chokeCoef = coefForTime (6.0f, sampleRate);

    int i = 0;

    if (delay > 0)
    {
        i = juce::jmin (delay, numSamples);
        delay -= i;
    }

    for (; i < numSamples; ++i)
    {
        if (i == chokeAt)
        {
            stage = Stage::Release;
            releaseCoef = chokeCoef;
        }

        if (envMode == EnvMode::ADSR && stage != Stage::Release && stage != Stage::Done && (gateRemaining -= 1.0) <= 0.0)
            stage = Stage::Release;

        const float e = nextEnvelope();

        if (stage == Stage::Done || position >= len)
        {
            kill();
            return;
        }

        float l = hermite (srcL, len, position);
        float r = srcR == srcL ? l : hermite (srcR, len, position);
        position += increment;

        if (lpOn)
        {
            float hi;
            svfProcess (lp[0], lpCoeffs, l, l, hi);
            svfProcess (lp[1], lpCoeffs, r, r, hi);
        }

        if (hpOn)
        {
            float lo;
            svfProcess (hp[0], hpCoeffs, l, lo, l);
            svfProcess (hp[1], hpCoeffs, r, lo, r);
        }

        left[i]  += l * e * gainL;
        right[i] += r * e * gainR;
    }

    if (chokeAt >= 0)
        chokeAt = chokeAt >= numSamples ? chokeAt - numSamples : -1;
}

} // namespace bf
