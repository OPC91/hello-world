#include "AudioFileLoader.h"

#include <cmath>

namespace bf
{

namespace
{
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;

        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;

            if (term < sum * 1.0e-12)
                break;
        }

        return sum;
    }

    /** Precomputed, linearly interpolated windowed-sinc kernel. */
    struct SincTable
    {
        static constexpr int zeroCrossings = 32;
        static constexpr int oversample = 512;

        explicit SincTable (double beta)
        {
            table.resize ((size_t) (zeroCrossings * oversample + 2));
            const double i0Beta = besselI0 (beta);

            for (size_t i = 0; i < table.size(); ++i)
            {
                const double x = (double) i / oversample; // in zero crossings
                const double r = x / zeroCrossings;
                const double window = r < 1.0 ? besselI0 (beta * std::sqrt (1.0 - r * r)) / i0Beta : 0.0;
                const double sinc = x < 1.0e-12 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                table[i] = (float) (sinc * window);
            }
        }

        float operator() (double x) const noexcept
        {
            x = std::abs (x) * oversample;
            const auto i = (size_t) x;

            if (i + 1 >= table.size())
                return 0.0f;

            const float f = (float) (x - (double) i);
            return table[i] + f * (table[i + 1] - table[i]);
        }

        std::vector<float> table;
    };

    const SincTable& sincTable()
    {
        static const SincTable t (9.0);
        return t;
    }
}

juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& input, double inputRate, double outputRate)
{
    if (juce::approximatelyEqual (inputRate, outputRate) || input.getNumSamples() == 0)
        return input;

    const auto& sinc = sincTable();
    const double ratio = outputRate / inputRate;
    const double cutoff = juce::jmin (1.0, ratio) * 0.97; // a little guard band below Nyquist
    const double halfWidth = SincTable::zeroCrossings / cutoff; // in input samples
    const int inLen = input.getNumSamples();
    const int outLen = juce::jmax (1, (int) std::ceil (inLen * ratio - 1.0e-6));

    juce::AudioBuffer<float> out (input.getNumChannels(), outLen);

    for (int ch = 0; ch < input.getNumChannels(); ++ch)
    {
        const float* in = input.getReadPointer (ch);
        float* o = out.getWritePointer (ch);

        for (int n = 0; n < outLen; ++n)
        {
            const double centre = n / ratio;
            const int first = juce::jmax (0, (int) std::ceil (centre - halfWidth));
            const int last = juce::jmin (inLen - 1, (int) std::floor (centre + halfWidth));
            double acc = 0.0;

            for (int k = first; k <= last; ++k)
                acc += (double) in[k] * sinc ((centre - k) * cutoff);

            o[n] = (float) (acc * cutoff);
        }
    }

    return out;
}

namespace
{
    juce::AudioFormatManager& formatManager()
    {
        // Constructed once; AudioFormatManager is safe to use for creating readers concurrently.
        static const auto fm = [] {
            auto m = std::make_unique<juce::AudioFormatManager>();
            m->registerBasicFormats();
            return m;
        }();
        return *fm;
    }
}

juce::String supportedAudioWildcard()
{
    return "*.wav;*.aif;*.aiff;*.flac;*.ogg";
}

bool isSupportedAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg");
}

juce::AudioBuffer<float> loadAudioFile (const juce::File& file, double targetRate, juce::String& error, double maxSeconds)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return {};
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Unsupported or corrupt audio file: " + file.getFileName();
        return {};
    }

    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0 || reader->numChannels <= 0)
    {
        error = "Empty audio file: " + file.getFileName();
        return {};
    }

    const int numChannels = (int) juce::jmin (2u, reader->numChannels);
    const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));

    juce::AudioBuffer<float> raw (numChannels, length);

    if (! reader->read (&raw, 0, length, 0, true, numChannels > 1))
    {
        error = "Could not read audio data: " + file.getFileName();
        return {};
    }

    // Identical channels are stored as mono: half the memory and CPU per voice.
    if (numChannels == 2)
    {
        const float* l = raw.getReadPointer (0);
        const float* r = raw.getReadPointer (1);
        bool identical = true;

        for (int i = 0; i < length && identical; ++i)
            identical = std::abs (l[i] - r[i]) < 1.0e-7f;

        if (identical)
            raw.setSize (1, length, true);
    }

    return resample (raw, reader->sampleRate, targetRate);
}

} // namespace bf
