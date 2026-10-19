#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/AudioFileLoader.h"
#include "Core/BuiltInKits.h"
#include "Core/SampleManager.h"

namespace
{
    juce::AudioBuffer<float> sine (double freq, double rate, int length, int channels = 1)
    {
        juce::AudioBuffer<float> b (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                b.setSample (ch, i, (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * freq * i / rate)));
        return b;
    }

    double rmsDb (const float* d, int n)
    {
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += (double) d[i] * d[i];
        return 10.0 * std::log10 (sum / n + 1e-30);
    }

    juce::File writeFile (const juce::File& f, juce::AudioFormat& format, const juce::AudioBuffer<float>& audio, double rate)
    {
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                          .withSampleRate (rate)
                                                          .withNumChannels (audio.getNumChannels())
                                                          .withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        return f;
    }

    struct TempDir
    {
        TempDir() { dir.createDirectory(); }
        ~TempDir() { dir.deleteRecursively(); }
        juce::File dir = juce::File::createTempFile ("bf_samples");
    };
}

TEST_CASE ("Sinc resampler preserves in-band content", "[samples][resample]")
{
    const auto in = sine (1000.0, 44100.0, 44100);
    const auto out = bf::resample (in, 44100.0, 48000.0);
    REQUIRE (out.getNumSamples() == 48000);

    const auto ideal = sine (1000.0, 48000.0, 48000);
    juce::AudioBuffer<float> err (1, 40000);
    for (int i = 0; i < 40000; ++i)
        err.setSample (0, i, out.getSample (0, i + 4000) - ideal.getSample (0, i + 4000));

    // Error at least 70 dB below the signal (signal RMS is about -9 dBFS).
    REQUIRE (rmsDb (err.getReadPointer (0), 40000) < -79.0);
}

TEST_CASE ("Sinc resampler removes content above the new Nyquist", "[samples][resample]")
{
    const auto in = sine (30000.0, 96000.0, 96000);
    const auto out = bf::resample (in, 96000.0, 44100.0);
    REQUIRE (out.getNumSamples() == 44100);
    REQUIRE (rmsDb (out.getReadPointer (0) + 2000, 40000) < -70.0);
}

TEST_CASE ("Audio files decode, downmix identical stereo and resample", "[samples][files]")
{
    TempDir tmp;
    juce::WavAudioFormat wav;
    juce::AiffAudioFormat aiff;
    juce::FlacAudioFormat flac;

    const auto mono = sine (220.0, 44100.0, 4410, 2); // identical channels
    auto stereo = sine (220.0, 44100.0, 4410, 2);
    stereo.applyGain (1, 0, stereo.getNumSamples(), 0.5f);

    for (auto* fmt : std::initializer_list<juce::AudioFormat*> { &wav, &aiff, &flac })
    {
        const auto ext = fmt->getFileExtensions()[0];
        juce::String error;

        const auto f1 = writeFile (tmp.dir.getChildFile ("mono" + ext), *fmt, mono, 44100.0);
        const auto a = bf::loadAudioFile (f1, 48000.0, error);
        INFO (error);
        REQUIRE (a.getNumChannels() == 1);
        REQUIRE (a.getNumSamples() == 4800);

        const auto f2 = writeFile (tmp.dir.getChildFile ("stereo" + ext), *fmt, stereo, 44100.0);
        const auto b = bf::loadAudioFile (f2, 44100.0, error);
        REQUIRE (b.getNumChannels() == 2);
        REQUIRE (b.getNumSamples() == 4410);
        REQUIRE (bf::isSupportedAudioFile (f2));
    }

    juce::String error;
    REQUIRE (bf::loadAudioFile (tmp.dir.getChildFile ("nope.wav"), 48000.0, error).getNumSamples() == 0);
    REQUIRE (error.contains ("not found"));

    const auto junk = tmp.dir.getChildFile ("junk.wav");
    junk.replaceWithText ("this is not audio");
    REQUIRE (bf::loadAudioFile (junk, 48000.0, error).getNumSamples() == 0);
    REQUIRE (error.isNotEmpty());
}

TEST_CASE ("Sample manager loads files in the background and falls back on failure", "[samples][manager]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TempDir tmp;
    juce::WavAudioFormat wav;
    const auto file = writeFile (tmp.dir.getChildFile ("hit.wav"), wav, sine (440.0, 44100.0, 1000), 44100.0);

    bf::SampleSlots slots;
    bf::SampleManager manager (slots,
                               [] (const juce::String& id, double rate) { return bf::BuiltInKits::render (id, rate); },
                               [] (int track) { return bf::BuiltInKits::defaultFor (bf::kTrackDefaults[(size_t) track].role); });

    // Requests before the sample rate is known are remembered, not loaded.
    manager.load (0, bf::SampleRef::fromFile (file));
    REQUIRE (slots.acquire (0) == nullptr);

    manager.setSampleRate (88200.0);
    REQUIRE (manager.waitAndApply (10000));
    REQUIRE (slots.acquire (0) != nullptr);
    REQUIRE (slots.acquire (0)->buffer.getNumSamples() == 2000);
    REQUIRE (slots.acquire (0)->name == "hit");
    REQUIRE_FALSE (manager.hasWarning (0));

    // Missing file: warning, original path kept, built-in fallback installed.
    const auto missing = tmp.dir.getChildFile ("gone.wav");
    manager.load (1, bf::SampleRef::fromFile (missing));
    REQUIRE (manager.waitAndApply (10000));
    REQUIRE (manager.hasWarning (1));
    REQUIRE (manager.getRef (1).file == missing);
    REQUIRE (slots.acquire (1) != nullptr);
    REQUIRE (slots.acquire (1)->path.isEmpty()); // the fallback is a built-in sound

    // Rapid re-requests: only the last one wins.
    manager.load (2, bf::SampleRef::fromFile (missing));
    manager.load (2, bf::SampleRef::fromFile (file));
    REQUIRE (manager.waitAndApply (10000));
    REQUIRE_FALSE (manager.hasWarning (2));
    REQUIRE (slots.acquire (2)->name == "hit");

    // Sample-rate change re-renders everything at the new rate.
    manager.setSampleRate (44100.0);
    REQUIRE (manager.waitAndApply (10000));
    REQUIRE (slots.acquire (0)->buffer.getNumSamples() == 1000);
    REQUIRE_THAT (slots.acquire (0)->sampleRate, Catch::Matchers::WithinAbs (44100.0, 1e-9));
}

TEST_CASE ("Sample references round-trip through strings", "[samples][state]")
{
    const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a b/c.wav");
    REQUIRE (bf::SampleRef::fromString (bf::SampleRef::fromFile (f).toString()) == bf::SampleRef::fromFile (f));
    REQUIRE (bf::SampleRef::fromString (bf::SampleRef::builtIn ("Neuro/Kick B").toString()).builtInId == "Neuro/Kick B");
    REQUIRE (bf::SampleRef::fromString ("garbage").isEmpty());
    REQUIRE (bf::SampleRef::fromString ("file:relative/path.wav").isEmpty());
}
