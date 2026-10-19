#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace bf
{

/** Procedural drum synthesis used to build the built-in kits.
    Everything is rendered offline (never on the audio thread) into buffers at the host rate. */
namespace DrumSynth
{
    struct KickParams
    {
        double startHz = 190.0;
        double endHz = 48.0;
        double pitchDecay = 0.035; // seconds (time constant)
        double ampDecay = 0.32;    // seconds (time constant)
        double length = 0.75;      // seconds
        double click = 0.5;        // 0..1
        double drive = 1.6;        // >= 1
    };

    juce::AudioBuffer<float> renderKick (double sampleRate, const KickParams& p, std::uint32_t seed = 1);
}

} // namespace bf
