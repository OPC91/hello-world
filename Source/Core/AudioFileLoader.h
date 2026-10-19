#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace bf
{

/** Offline, band-limited sample-rate conversion (Kaiser-windowed sinc).
    When downsampling, the cutoff follows the target Nyquist so nothing aliases. */
juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& input, double inputRate, double outputRate);

/** Decodes an audio file (WAV/AIFF/FLAC/OGG...), keeps at most two channels, trims it to
    `maxSeconds`, and resamples it to `targetRate`. Returns an empty buffer and sets `error`
    on failure. Safe to call from any thread except the audio thread. */
juce::AudioBuffer<float> loadAudioFile (const juce::File& file, double targetRate, juce::String& error, double maxSeconds = 30.0);

/** Wildcard pattern of the file types loadAudioFile() accepts. */
juce::String supportedAudioWildcard();
bool isSupportedAudioFile (const juce::File&);

} // namespace bf
