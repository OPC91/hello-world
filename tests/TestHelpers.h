#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <numbers>
#include <vector>

namespace testing
{
    /** A sine at a peak level in dBFS (EBU Tech 3341 convention: -23 dBFS stereo = -23 LUFS). */
    inline juce::AudioBuffer<float> makeSine (double sampleRate, double seconds, double frequency,
                                              double peakDbfs, int numChannels = 2, double startPhase = 0.0)
    {
        const auto length = (int) std::lround (seconds * sampleRate);
        const auto amplitude = std::pow (10.0, peakDbfs / 20.0);
        juce::AudioBuffer<float> buffer (numChannels, length);

        for (int i = 0; i < length; ++i)
        {
            const auto v = (float) (amplitude * std::sin (startPhase + 2.0 * std::numbers::pi * frequency * i / sampleRate));
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample (ch, i, v);
        }

        return buffer;
    }

    /** A piecewise sequence of 1 kHz stereo sine segments: {seconds, dBFS}. A level <= -200 means silence. */
    inline juce::AudioBuffer<float> makeSineSequence (double sampleRate, const std::vector<std::pair<double, double>>& segments,
                                                      int numChannels = 2)
    {
        int total = 0;
        for (const auto& [seconds, level] : segments)
            total += (int) std::lround (seconds * sampleRate);

        juce::AudioBuffer<float> buffer (numChannels, total);
        buffer.clear();

        int position = 0;
        for (const auto& [seconds, level] : segments)
        {
            const auto length = (int) std::lround (seconds * sampleRate);
            const auto amplitude = level <= -200.0 ? 0.0 : std::pow (10.0, level / 20.0);

            for (int i = 0; i < length; ++i)
            {
                // Continuous phase across segments.
                const auto v = (float) (amplitude * std::sin (2.0 * std::numbers::pi * 1000.0 * (position + i) / sampleRate));
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.setSample (ch, position + i, v);
            }

            position += length;
        }

        return buffer;
    }

    inline void writeAudioFile (const juce::File& file, juce::AudioFormat& format,
                                const juce::AudioBuffer<float>& buffer, double sampleRate, int bits = 24)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                          .withSampleRate (sampleRate)
                                                          .withNumChannels (buffer.getNumChannels())
                                                          .withBitsPerSample (bits));
        jassert (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }

    inline void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        juce::WavAudioFormat wav;
        writeAudioFile (file, wav, buffer, sampleRate);
    }

    /** A temporary directory removed when the object goes out of scope. */
    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("benchmark-tests-" + juce::Uuid().toString());

        TempDir() { dir.createDirectory(); }
        ~TempDir() { dir.deleteRecursively(); }

        juce::File operator/ (const juce::String& name) const { return dir.getChildFile (name); }
    };
}
