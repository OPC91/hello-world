#include "ReferenceDecoder.h"
#include "Resampler.h"
#include "dsp/LoudnessMeter.h"

namespace benchmark
{
    std::unique_ptr<juce::AudioFormatManager> ReferenceDecoder::createFormatManager()
    {
        auto manager = std::make_unique<juce::AudioFormatManager>();
        manager->registerBasicFormats();
        return manager;
    }

    bool ReferenceDecoder::isSupportedFile (const juce::File& file)
    {
        return file.hasFileExtension ("wav;wave;aif;aiff;aifc;flac;mp3");
    }

    juce::String ReferenceDecoder::getSupportedWildcard()
    {
        return "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.flac;*.mp3";
    }

    ReferenceInfo ReferenceDecoder::analyse (const juce::AudioBuffer<float>& samples, double sampleRate)
    {
        ReferenceInfo info;
        const auto numSamples = samples.getNumSamples();
        const auto numChannels = samples.getNumChannels();

        info.lengthSeconds = numSamples / sampleRate;
        info.sourceChannels = numChannels;

        for (int ch = 0; ch < numChannels; ++ch)
            info.samplePeak = std::max (info.samplePeak, samples.getMagnitude (ch, 0, numSamples));

        if (numSamples == 0 || numChannels == 0)
            return info;

        constexpr int chunk = 8192;
        const auto* left = samples.getReadPointer (0);
        const auto* right = samples.getReadPointer (std::min (1, numChannels - 1));

        // As played on a stereo bus (a mono file goes to both speakers).
        dsp::LoudnessMeter stereo;
        stereo.prepare (sampleRate, 2);

        // As played on a mono bus ((L + R) / 2).
        dsp::LoudnessMeter mono;
        mono.prepare (sampleRate, 1);
        std::vector<float> downmix ((size_t) chunk);

        for (int start = 0; start < numSamples; start += chunk)
        {
            const auto n = std::min (chunk, numSamples - start);
            const float* stereoPtrs[] = { left + start, right + start };
            stereo.process (stereoPtrs, 2, n);

            for (int i = 0; i < n; ++i)
                downmix[(size_t) i] = 0.5f * (left[start + i] + right[start + i]);

            const float* monoPtr[] = { downmix.data() };
            mono.process (monoPtr, 1, n);
        }

        info.integratedStereo = stereo.getIntegrated();
        info.integratedMono = mono.getIntegrated();
        info.loudnessRange = stereo.getLoudnessRange();
        return info;
    }

    ReferenceDecoder::Result ReferenceDecoder::decode (const juce::File& file,
                                                       double targetSampleRate,
                                                       const std::atomic<bool>& cancelFlag,
                                                       std::atomic<float>* progress)
    {
        juce::ScopedNoDenormals noDenormals;

        const auto setProgress = [progress] (float p)
        {
            if (progress != nullptr)
                progress->store (p, std::memory_order_relaxed);
        };

        const auto cancelled = [&cancelFlag] { return cancelFlag.load (std::memory_order_relaxed); };

        Result result;

        if (! file.existsAsFile())
        {
            result.error = "File not found";
            return result;
        }

        const auto formats = createFormatManager();
        std::unique_ptr<juce::AudioFormatReader> reader (formats->createReaderFor (file));

        if (reader == nullptr)
        {
            result.error = "Unsupported or unreadable audio file";
            return result;
        }

        if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        {
            result.error = "File contains no audio";
            return result;
        }

        if ((double) reader->lengthInSamples / reader->sampleRate > maxLengthSeconds)
        {
            result.error = "File is longer than 30 minutes";
            return result;
        }

        // Decode (first two channels if the file has more).
        const auto numChannels = (int) std::min (2u, reader->numChannels);
        const auto length = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> decoded (numChannels, length);

        constexpr int chunk = 1 << 16;
        for (int start = 0; start < length; start += chunk)
        {
            if (cancelled())
            {
                result.cancelled = true;
                return result;
            }

            const auto n = std::min (chunk, length - start);
            reader->read (&decoded, start, n, start, true, numChannels > 1);
            setProgress (0.4f * (float) (start + n) / (float) length);
        }

        // Resample to the host rate.
        auto audio = std::make_shared<ReferenceAudio>();
        audio->sampleRate = targetSampleRate;

        if (std::abs (reader->sampleRate - targetSampleRate) < 1e-6)
        {
            audio->samples = std::move (decoded);
        }
        else
        {
            audio->samples = Resampler::process (decoded, reader->sampleRate, targetSampleRate,
                                                 [&] (float p)
                                                 {
                                                     setProgress (0.4f + 0.45f * p);
                                                     return ! cancelled();
                                                 });

            if (audio->samples.getNumSamples() == 0 || cancelled())
            {
                result.cancelled = true;
                return result;
            }
        }

        decoded.setSize (0, 0);

        if (cancelled())
        {
            result.cancelled = true;
            return result;
        }

        audio->info = analyse (audio->samples, targetSampleRate);
        audio->info.sourceSampleRate = reader->sampleRate;
        audio->info.sourceChannels = (int) reader->numChannels;
        audio->overview = std::make_shared<WaveformOverview> (WaveformOverview::compute (audio->samples, targetSampleRate));
        setProgress (1.0f);

        result.audio = std::move (audio);
        return result;
    }
}
