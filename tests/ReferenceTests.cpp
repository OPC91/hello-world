#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "reference/ReferenceDecoder.h"
#include "reference/ReferenceLibrary.h"

using benchmark::ReferenceDecoder;
using benchmark::ReferenceLibrary;
using Catch::Approx;
using Status = ReferenceLibrary::Status;

namespace
{
    std::atomic<bool> notCancelled { false };
}

TEST_CASE ("Decoder reads, resamples and measures a WAV file", "[reference]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    testing::TempDir temp;

    const auto file = temp / "tone.wav";
    testing::writeWav (file, testing::makeSine (44100.0, 10.0, 1000.0, -23.0), 44100.0);

    std::atomic<float> progress { 0.0f };
    const auto result = ReferenceDecoder::decode (file, 48000.0, notCancelled, &progress);

    REQUIRE (result.audio != nullptr);
    CHECK (result.error.isEmpty());
    CHECK (result.audio->sampleRate == Approx (48000.0));
    CHECK (result.audio->samples.getNumChannels() == 2);
    CHECK (result.audio->samples.getNumSamples() == 480000);
    CHECK (result.audio->info.sourceSampleRate == Approx (44100.0));
    CHECK (result.audio->info.lengthSeconds == Approx (10.0).margin (1e-3));
    CHECK (result.audio->info.integratedStereo == Approx (-23.0).margin (0.1));
    CHECK (result.audio->info.integratedMono == Approx (-26.01).margin (0.1)); // (L + R) / 2 of identical channels
    CHECK (result.audio->info.samplePeak == Approx (std::pow (10.0, -23.0 / 20.0)).epsilon (0.01));
    CHECK (progress.load() == Approx (1.0f));
}

TEST_CASE ("Mono files play on both channels of a stereo bus", "[reference]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    testing::TempDir temp;

    const auto file = temp / "mono.wav";
    testing::writeWav (file, testing::makeSine (48000.0, 5.0, 1000.0, -23.0, 1), 48000.0);

    const auto result = ReferenceDecoder::decode (file, 48000.0, notCancelled);
    REQUIRE (result.audio != nullptr);
    CHECK (result.audio->samples.getNumChannels() == 1);
    CHECK (result.audio->info.integratedStereo == Approx (-23.0).margin (0.1));
    CHECK (result.audio->info.integratedMono == Approx (-26.01).margin (0.1));
}

TEST_CASE ("FLAC and AIFF files decode", "[reference]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    testing::TempDir temp;
    const auto tone = testing::makeSine (48000.0, 3.0, 1000.0, -20.0);

    juce::FlacAudioFormat flac;
    juce::AiffAudioFormat aiff;
    testing::writeAudioFile (temp / "tone.flac", flac, tone, 48000.0, 24);
    testing::writeAudioFile (temp / "tone.aiff", aiff, tone, 48000.0, 16);

    for (const auto* name : { "tone.flac", "tone.aiff" })
    {
        CAPTURE (name);
        const auto result = ReferenceDecoder::decode (temp / name, 48000.0, notCancelled);
        REQUIRE (result.audio != nullptr);
        CHECK (result.audio->samples.getNumSamples() == tone.getNumSamples());
        CHECK (result.audio->info.integratedStereo == Approx (-20.0).margin (0.1));
    }
}

TEST_CASE ("Decoder reports missing and unreadable files", "[reference]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    testing::TempDir temp;

    const auto missing = ReferenceDecoder::decode (temp / "nope.wav", 48000.0, notCancelled);
    CHECK (missing.audio == nullptr);
    CHECK (missing.error == "File not found");

    const auto garbage = temp / "garbage.wav";
    garbage.replaceWithText ("this is not audio");
    const auto bad = ReferenceDecoder::decode (garbage, 48000.0, notCancelled);
    CHECK (bad.audio == nullptr);
    CHECK (bad.error.isNotEmpty());

    std::atomic<bool> cancelled { true };
    testing::writeWav (temp / "ok.wav", testing::makeSine (48000.0, 1.0, 1000.0, -20.0), 48000.0);
    const auto cancel = ReferenceDecoder::decode (temp / "ok.wav", 48000.0, cancelled);
    CHECK (cancel.audio == nullptr);
    CHECK (cancel.cancelled);
}

TEST_CASE ("Supported file types", "[reference]")
{
    for (const auto* name : { "a.wav", "b.WAV", "c.aif", "d.aiff", "e.flac", "f.mp3" })
        CHECK (ReferenceDecoder::isSupportedFile (juce::File::getCurrentWorkingDirectory().getChildFile (name)));

    for (const auto* name : { "a.txt", "b.m4a", "c" })
        CHECK_FALSE (ReferenceDecoder::isSupportedFile (juce::File::getCurrentWorkingDirectory().getChildFile (name)));
}

//==============================================================================
namespace
{
    struct LibraryFixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        testing::TempDir temp;
        benchmark::RealtimeHandoff<benchmark::ReferenceAudio> handoff;
        std::unique_ptr<ReferenceLibrary> library = std::make_unique<ReferenceLibrary> (handoff);
        juce::StringArray messages;

        LibraryFixture()
        {
            library->onMessage = [this] (const juce::String& m) { messages.add (m); };
        }

        juce::File makeTone (const juce::String& name, double level = -20.0, double seconds = 2.0)
        {
            const auto file = temp / name;
            testing::writeWav (file, testing::makeSine (44100.0, seconds, 1000.0, level), 44100.0);
            return file;
        }

        const benchmark::ReferenceAudio* published()
        {
            const auto* p = handoff.acquire();
            handoff.release();
            return p;
        }
    };
}

TEST_CASE ("Library loads files in the background and publishes the active one", "[library]")
{
    LibraryFixture f;
    f.library->setTargetSampleRate (48000.0);

    const auto a = f.makeTone ("a.wav", -20.0);
    const auto b = f.makeTone ("b.wav", -14.0);
    const auto text = f.temp / "notes.txt";
    text.replaceWithText ("hello");

    const auto result = f.library->addFiles ({ a, b, text, a });
    CHECK (result.added == 2);
    CHECK (result.unsupported == 1);
    CHECK (result.duplicates == 1);
    CHECK (f.messages.size() == 1);

    REQUIRE (f.library->waitUntilLoaded (20000));
    REQUIRE (f.library->size() == 2);
    CHECK (f.library->getActiveIndex() == 0);

    for (int i = 0; i < 2; ++i)
    {
        CHECK ((*f.library)[i].status == Status::ready);
        REQUIRE ((*f.library)[i].audio != nullptr);
        CHECK ((*f.library)[i].audio->sampleRate == Approx (48000.0));
    }

    CHECK ((*f.library)[0].info.integratedStereo == Approx (-20.0).margin (0.1));
    CHECK ((*f.library)[1].info.integratedStereo == Approx (-14.0).margin (0.1));
    CHECK (f.published() == (*f.library)[0].audio.get());

    f.library->setActive (1);
    CHECK (f.published() == (*f.library)[1].audio.get());

    f.library->remove (1);
    CHECK (f.library->getActiveIndex() == 0);
    CHECK (f.published() == (*f.library)[0].audio.get());

    f.library->remove (0);
    CHECK (f.library->getActiveIndex() == -1);
    CHECK (f.published() == nullptr);
}

TEST_CASE ("Library waits for a sample rate before loading", "[library]")
{
    LibraryFixture f;
    f.library->addFiles ({ f.makeTone ("a.wav") });

    CHECK ((*f.library)[0].status == Status::waiting);
    CHECK (f.published() == nullptr);

    f.library->setTargetSampleRate (96000.0);
    REQUIRE (f.library->waitUntilLoaded (20000));
    REQUIRE ((*f.library)[0].audio != nullptr);
    CHECK ((*f.library)[0].audio->sampleRate == Approx (96000.0));
    CHECK (f.published() != nullptr);

    // A host sample rate change reloads at the new rate.
    f.library->setTargetSampleRate (44100.0);
    REQUIRE (f.library->waitUntilLoaded (20000));
    REQUIRE ((*f.library)[0].audio != nullptr);
    CHECK ((*f.library)[0].audio->sampleRate == Approx (44100.0));
    CHECK (f.published()->sampleRate == Approx (44100.0));
}

TEST_CASE ("Library is limited to 12 references", "[library]")
{
    LibraryFixture f;
    juce::Array<juce::File> files;
    for (int i = 0; i < 14; ++i)
        files.add (f.makeTone ("t" + juce::String (i) + ".wav", -20.0, 0.5));

    const auto result = f.library->addFiles (files);
    CHECK (result.added == ReferenceLibrary::maxReferences);
    CHECK (result.overLimit == 2);
    CHECK (f.library->size() == ReferenceLibrary::maxReferences);
}

TEST_CASE ("Library state round-trips and keeps missing files", "[library]")
{
    LibraryFixture f;
    f.library->setTargetSampleRate (48000.0);

    const auto a = f.makeTone ("a.wav");
    const auto b = f.makeTone ("b.wav");
    f.library->addFiles ({ a, b });
    f.library->setActive (1);
    REQUIRE (f.library->waitUntilLoaded (20000));

    const auto state = f.library->getStateSnapshot();
    CHECK (state.getNumChildren() == 2);

    b.deleteFile();

    benchmark::RealtimeHandoff<benchmark::ReferenceAudio> otherHandoff;
    ReferenceLibrary restored (otherHandoff);
    restored.setTargetSampleRate (48000.0);
    restored.restoreState (state);
    REQUIRE (restored.waitUntilLoaded (20000));

    REQUIRE (restored.size() == 2);
    CHECK (restored.getActiveIndex() == 1);
    CHECK (restored[0].status == Status::ready);
    CHECK (restored[0].file == a);
    CHECK (restored[1].status == Status::missing);
    CHECK (restored[1].file == b); // path kept so the user can relocate it

    // The missing entry survives another save, and can be relocated.
    CHECK (restored.getStateSnapshot().getChild (1).getProperty ("path").toString() == b.getFullPathName());

    const auto replacement = f.makeTone ("b-moved.wav", -16.0);
    restored.relocate (1, replacement);
    REQUIRE (restored.waitUntilLoaded (20000));
    CHECK (restored[1].status == Status::ready);
    CHECK (restored[1].info.integratedStereo == Approx (-16.0).margin (0.1));

    // Garbage state is ignored.
    restored.restoreState (juce::ValueTree ("Nonsense"));
    CHECK (restored.size() == 0);
}

TEST_CASE ("Library unloads least recently used audio over the memory budget", "[library]")
{
    LibraryFixture f;
    f.library->setTargetSampleRate (48000.0);
    f.library->addFiles ({ f.makeTone ("a.wav"), f.makeTone ("b.wav"), f.makeTone ("c.wav") });
    REQUIRE (f.library->waitUntilLoaded (20000));

    const auto oneFile = (*f.library)[0].audio->getMemoryBytes();
    f.library->setActive (2);
    f.library->setActive (1);

    // Room for two references: the least recently used non-active one (index 0) goes.
    f.library->setMemoryBudget (oneFile * 2);
    CHECK ((*f.library)[0].audio == nullptr);
    CHECK ((*f.library)[0].status == Status::ready); // info is kept
    CHECK ((*f.library)[1].audio != nullptr);
    CHECK ((*f.library)[2].audio != nullptr);

    // Selecting it again reloads it.
    f.library->setActive (0);
    CHECK ((*f.library)[0].status == Status::loading);
    REQUIRE (f.library->waitUntilLoaded (20000));
    CHECK ((*f.library)[0].audio != nullptr);
    CHECK (f.published() == (*f.library)[0].audio.get());
    CHECK (f.library->getLoadedBytes() <= oneFile * 2);
}
