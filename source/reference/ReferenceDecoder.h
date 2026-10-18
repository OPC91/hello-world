#pragma once

#include "ReferenceAudio.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>

namespace benchmark
{
    /**
        Decodes an audio file, converts it to the target sample rate and measures
        its loudness. Runs on a background thread; never call from the audio thread.
    */
    struct ReferenceDecoder
    {
        struct Result
        {
            std::shared_ptr<ReferenceAudio> audio; // null on failure or cancellation
            juce::String error;
            bool cancelled = false;
        };

        static constexpr double maxLengthSeconds = 30.0 * 60.0;

        static Result decode (const juce::File& file,
                              double targetSampleRate,
                              const std::atomic<bool>& cancelFlag,
                              std::atomic<float>* progress = nullptr);

        /** Measures loudness facts for audio already at its final rate. */
        static ReferenceInfo analyse (const juce::AudioBuffer<float>& samples, double sampleRate);

        static bool isSupportedFile (const juce::File& file);
        static juce::String getSupportedWildcard();

        /** Format manager with WAV, AIFF, FLAC, MP3 (plus OS codecs). */
        static std::unique_ptr<juce::AudioFormatManager> createFormatManager();
    };
}
