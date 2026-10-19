#include <catch2/catch_test_macros.hpp>

#include "Core/BuiltInKits.h"
#include "Core/KitLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    /** Zero-crossing rate: a crude brightness measure (crossings per second). */
    double zeroCrossingRate (const juce::AudioBuffer<float>& b, double sr)
    {
        const auto* d = b.getReadPointer (0);
        int crossings = 0;
        const int n = juce::jmin (b.getNumSamples(), (int) (0.05 * sr));
        for (int i = 1; i < n; ++i)
            crossings += (d[i - 1] < 0.0f) != (d[i] < 0.0f);
        return crossings / (n / sr);
    }
}

TEST_CASE ("Every built-in sound renders cleanly at common rates", "[kit]")
{
    for (const auto& kit : bf::BuiltInKits::kitNames())
    {
        for (int r = 0; r < bf::kNumRoles; ++r)
        {
            const auto sounds = bf::BuiltInKits::soundsFor (kit, (bf::Role) r);
            REQUIRE_FALSE (sounds.isEmpty()); // every role is covered in every kit

            for (const auto& id : sounds)
            {
                for (double sr : { 44100.0, 48000.0, 96000.0 })
                {
                    INFO (id << " @ " << sr);
                    const auto b = bf::BuiltInKits::render (id, sr);
                    REQUIRE (b.getNumSamples() > (int) (0.05 * sr));
                    REQUIRE (b.getNumSamples() < (int) (3.0 * sr));

                    const float peak = b.getMagnitude (0, b.getNumSamples());
                    REQUIRE (peak > 0.5f);
                    REQUIRE (peak <= 1.0f);

                    for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    {
                        const auto* d = b.getReadPointer (ch);
                        REQUIRE (std::all_of (d, d + b.getNumSamples(), [] (float x) { return std::isfinite (x); }));

                        // Ends in (near) silence, so one-shots never click when they stop.
                        REQUIRE (std::abs (d[b.getNumSamples() - 1]) < 1.0e-3f);
                    }

                    if (! id.endsWith ("Reverse Crash"))
                    {
                        // Drums hit straight away: the peak is in the first 80 ms (a clap's
                        // last "hand" lands ~35 ms in).
                        const int early = (int) (0.08 * sr);
                        REQUIRE (b.getMagnitude (0, early) >= peak * 0.9f); // noisy cymbals peak a bit randomly
                    }
                }
            }
        }
    }
}

TEST_CASE ("Built-in rendering is deterministic and kits sound different", "[kit]")
{
    const auto a = bf::BuiltInKits::render ("Neuro/Snare A", 48000.0);
    const auto b = bf::BuiltInKits::render ("Neuro/Snare A", 48000.0);
    REQUIRE (a.getNumSamples() == b.getNumSamples());
    REQUIRE (std::equal (a.getReadPointer (0), a.getReadPointer (0) + a.getNumSamples(), b.getReadPointer (0)));

    const auto clean = bf::BuiltInKits::render ("Clean/Kick A", 48000.0);
    const auto neuro = bf::BuiltInKits::render ("Neuro/Kick A", 48000.0);
    const auto jungle = bf::BuiltInKits::render ("Jungle/Kick A", 48000.0);
    REQUIRE (clean.getNumSamples() == neuro.getNumSamples());
    double diff = 0.0;
    for (int i = 0; i < clean.getNumSamples(); ++i)
        diff += std::abs (clean.getSample (0, i) - neuro.getSample (0, i));
    REQUIRE (diff > 100.0);
    REQUIRE (jungle.getNumSamples() > 0);

    // Hats are much brighter than kicks.
    const auto hat = bf::BuiltInKits::render ("Clean/Closed Hat A", 48000.0);
    REQUIRE (zeroCrossingRate (hat, 48000.0) > 10.0 * zeroCrossingRate (clean, 48000.0));

    REQUIRE (bf::BuiltInKits::render ("Nope/Kick A", 48000.0).getNumSamples() == 0);
    REQUIRE (bf::BuiltInKits::render ("Clean/Nope", 48000.0).getNumSamples() == 0);
}

TEST_CASE ("Kit folder samples are organised by role", "[kit][library]")
{
    const auto root = juce::File::createTempFile ("bf_kits");
    const auto kitDir = root.getChildFile ("My Jungle Kit");

    const char* files[] = {
        "Kicks/BD_01.wav", "Kicks/kick tight.wav", "Snares/Amen Snare 2.wav", "Snares/ghost_soft.wav",
        "Hats/Closed/hh1.wav", "Hats/Open/oh 3.aif", "hat_open_long.flac", "Ride_Bell.wav", "Perc/rim.wav",
        "FX/reverse cymbal.wav", "random/unknown thing.wav", "notes.txt",
    };

    for (auto* f : files)
    {
        auto file = kitDir.getChildFile (f);
        file.getParentDirectory().createDirectory();
        file.replaceWithText ("x");
    }

    root.getChildFile ("Empty Kit").createDirectory();

    const auto kits = bf::KitLibrary::scan (root);
    REQUIRE (kits.size() == bf::BuiltInKits::kitNames().size() + 1); // empty folders are skipped

    const auto& kit = kits.back();
    REQUIRE (kit.name == "My Jungle Kit");
    REQUIRE_FALSE (kit.builtIn);

    const auto count = [&] (bf::Role r) { return kit.samples[(size_t) r].size(); };
    REQUIRE (count (bf::Role::Kick) == 2);
    REQUIRE (count (bf::Role::Snare) == 1);
    REQUIRE (count (bf::Role::GhostSnare) == 1);
    REQUIRE (count (bf::Role::ClosedHat) == 1);
    REQUIRE (count (bf::Role::OpenHat) == 2);
    REQUIRE (count (bf::Role::Ride) == 1);
    REQUIRE (count (bf::Role::Perc) == 1);
    REQUIRE (count (bf::Role::FX) == 1);
    REQUIRE (kit.totalSamples() == 10);

    // Natural sort and role fallbacks.
    REQUIRE (kit.samples[(size_t) bf::Role::Kick][0].file.getFileName() == "BD_01.wav");

    bf::KitInfo sparse;
    sparse.samples[(size_t) bf::Role::Snare].add (bf::SampleRef::fromFile (kitDir.getChildFile ("s.wav")));
    REQUIRE (sparse.samplesFor (bf::Role::GhostSnare).size() == 1);
    REQUIRE (sparse.samplesFor (bf::Role::Kick).isEmpty());

    root.deleteRecursively();
}

// Hidden: `BreakForgeTests "[.render]"` writes every built-in sound to $BF_RENDER_DIR for listening.
TEST_CASE ("Render built-in kits to WAV", "[.render]")
{
    const auto dirEnv = juce::SystemStats::getEnvironmentVariable ("BF_RENDER_DIR", {});
    REQUIRE (dirEnv.isNotEmpty());
    const juce::File dir (dirEnv);
    dir.createDirectory();
    juce::WavAudioFormat wav;

    for (const auto& kit : bf::BuiltInKits::kitNames())
    {
        juce::StringArray done;
        for (int r = 0; r < bf::kNumRoles; ++r)
        {
            for (const auto& id : bf::BuiltInKits::soundsFor (kit, (bf::Role) r))
            {
                if (done.contains (id))
                    continue;
                done.add (id);

                const auto audio = bf::BuiltInKits::render (id, 48000.0);
                auto file = dir.getChildFile (id.replace ("/", " - ") + ".wav");
                file.deleteFile();
                std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0)
                                                               .withNumChannels (audio.getNumChannels()).withBitsPerSample (24));
                REQUIRE (writer != nullptr);
                writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
            }
        }
    }
}
