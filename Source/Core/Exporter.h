#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Engine.h"

namespace bf::Exporter
{
    constexpr int midiTicksPerQuarter = 960;

    /** Total export length: at least `bars` bars, rounded up to whole pattern loops. */
    double exportLengthPpq (const Pattern&, int bars) noexcept;

    /** The pattern as a MIDI file (channel 10, GM-style notes) with swing, groove, accents,
        probability and humanize baked in exactly as the engine would play them. A hit nudged
        before the loop start of the next repeat wraps to the end, so the file loops seamlessly. */
    juce::MidiFile makeMidiFile (const Pattern&, const GrooveContext&, const std::array<int, kNumTracks>& notes,
                                 double bpm, int bars, const juce::String& name = "BreakForge");

    struct RenderRequest
    {
        SequencerSnapshot snapshot;
        EngineParams params;
        /** Indexed track * kSampleVariants + variant (variant 0 = the track's own sample). */
        std::array<SampleData::Ptr, kNumSampleSlots> samples;
        int pattern = 0;
        double sampleRate = 48000.0;
        double bpm = 174.0;
        int bars = 4;
        bool renderStems = false;
        bool applyMaster = true; // drive/limiter on the mix (never on stems)
    };

    struct RenderResult
    {
        juce::AudioBuffer<float> mix;
        std::array<juce::AudioBuffer<float>, kNumTracks> stems;
        std::array<bool, kNumTracks> trackHasAudio {};
    };

    /** Renders the pattern offline through the real engine (same scheduler, groove and voices).
        The render starts a few seconds early and discards the pre-roll, so ringing tails from the
        previous loop are present at the start and the result loops seamlessly. */
    RenderResult render (const RenderRequest&);

    bool writeWav (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate);
    bool writeMidi (const juce::File&, const juce::MidiFile&);

    /** Folder for files handed to the host by drag and drop. */
    juce::File getExportFolder();
}
