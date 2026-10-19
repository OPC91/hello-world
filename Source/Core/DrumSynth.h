#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace bf
{

/** Procedural drum synthesis used to build the built-in kits.
    Everything is rendered offline (never on the audio thread) into buffers at the host rate,
    and is fully deterministic for a given set of parameters and seed. */
namespace DrumSynth
{
    /** Character applied after synthesis: saturation and optional vintage-sampler crunch. */
    struct Colour
    {
        double drive = 1.0;          // tanh drive, 1 = clean
        double crushRate = 0.0;      // sample-and-hold rate in Hz (0 = off)
        int crushBits = 0;           // quantisation bits (0 = off)
    };

    struct KickParams
    {
        double startHz = 190.0;
        double endHz = 48.0;
        double pitchDecay = 0.035;   // seconds (time constant)
        double ampDecay = 0.32;      // seconds (time constant)
        double length = 0.75;        // seconds
        double click = 0.5;          // 0..1
        double drive = 1.6;          // >= 1
    };

    struct SnareParams
    {
        double toneHz = 185.0;       // fundamental of the shell
        double toneDecay = 0.09;
        double toneLevel = 1.0;
        double noiseDecay = 0.16;
        double noiseLevel = 0.75;
        double noiseHp = 1400.0;     // high-pass on the wires
        double noiseLp = 7000.0;
        double click = 0.6;
        double length = 0.45;
        double drive = 1.8;
    };

    struct MetalParams
    {
        double tune = 1.0;           // multiplies the oscillator cluster
        double decay = 0.045;        // seconds (time constant)
        double length = 0.25;
        double hp = 7000.0;
        double bandHz = 8500.0;
        double noise = 0.25;         // white-noise blend for sizzle
        double bell = 0.0;           // ride bell partials
        double width = 0.35;         // stereo decorrelation 0..1
    };

    juce::AudioBuffer<float> renderKick (double sampleRate, const KickParams& p, std::uint32_t seed = 1);
    juce::AudioBuffer<float> renderSnare (double sampleRate, const SnareParams& p, std::uint32_t seed = 2);
    juce::AudioBuffer<float> renderMetal (double sampleRate, const MetalParams& p, std::uint32_t seed = 3);
    juce::AudioBuffer<float> renderRimshot (double sampleRate, double tune, std::uint32_t seed = 4);
    juce::AudioBuffer<float> renderClap (double sampleRate, double tone, std::uint32_t seed = 5);
    juce::AudioBuffer<float> renderShaker (double sampleRate, double brightness, std::uint32_t seed = 6);
    juce::AudioBuffer<float> renderReverseCrash (double sampleRate, double lengthSeconds, double tune, std::uint32_t seed = 7);

    /** Applies drive and crunch, then re-normalises to `peak`. */
    void applyColour (juce::AudioBuffer<float>&, double sampleRate, const Colour&, float peak);
}

} // namespace bf
