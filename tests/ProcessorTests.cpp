#include <thread>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "PluginProcessor.h"
#include "Parameters.h"

using benchmark::BenchmarkProcessor;
namespace ParamIDs = benchmark::ParamIDs;

namespace
{
    void fillWithNoise (juce::AudioBuffer<float>& buffer, int seed)
    {
        juce::Random rng (seed);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
    }

    bool setLayout (BenchmarkProcessor& p, const juce::AudioChannelSet& set)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        return p.setBusesLayout (layout);
    }

    void setParam (BenchmarkProcessor& p, const char* id, float normalised)
    {
        p.getValueTreeState().getParameter (id)->setValueNotifyingHost (normalised);
    }

    float getParam (BenchmarkProcessor& p, const char* id)
    {
        return p.getValueTreeState().getParameter (id)->getValue();
    }
}

TEST_CASE ("Processor passes audio through unchanged", "[processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto sampleRate = GENERATE (44100.0, 48000.0, 88200.0, 96000.0, 192000.0);
    const auto blockSize = GENERATE (1, 32, 511, 4096);
    const auto stereo = GENERATE (false, true);
    const auto bypassed = GENERATE (false, true);

    BenchmarkProcessor processor;
    REQUIRE (setLayout (processor, stereo ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono()));
    setParam (processor, ParamIDs::bypass, bypassed ? 1.0f : 0.0f);
    processor.prepareToPlay (sampleRate, blockSize);

    const int numChannels = stereo ? 2 : 1;
    juce::AudioBuffer<float> buffer (numChannels, blockSize);
    juce::MidiBuffer midi;

    for (int block = 0; block < 4; ++block)
    {
        fillWithNoise (buffer, block);
        juce::AudioBuffer<float> expected (buffer);

        processor.processBlock (buffer, midi);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < blockSize; ++i)
                REQUIRE (juce::exactlyEqual (buffer.getSample (ch, i), expected.getSample (ch, i)));
    }

    processor.releaseResources();
}

TEST_CASE ("Processor handles a block larger than announced", "[processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    BenchmarkProcessor processor;
    processor.prepareToPlay (48000.0, 64);

    juce::AudioBuffer<float> buffer (2, 1000);
    fillWithNoise (buffer, 7);
    juce::AudioBuffer<float> expected (buffer);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (juce::exactlyEqual (buffer.getSample (ch, i), expected.getSample (ch, i)));
}

TEST_CASE ("Supported bus layouts", "[processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    BenchmarkProcessor processor;

    CHECK (setLayout (processor, juce::AudioChannelSet::mono()));
    CHECK (setLayout (processor, juce::AudioChannelSet::stereo()));
    CHECK_FALSE (setLayout (processor, juce::AudioChannelSet::create5point1()));

    juce::AudioProcessor::BusesLayout mismatched;
    mismatched.inputBuses.add (juce::AudioChannelSet::mono());
    mismatched.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK_FALSE (processor.checkBusesLayoutSupported (mismatched));
}

TEST_CASE ("Parameters exist and are automatable", "[parameters]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    BenchmarkProcessor processor;

    for (auto* id : { ParamIDs::bypass, ParamIDs::listenRef, ParamIDs::loudnessMatch })
    {
        auto* param = processor.getValueTreeState().getParameter (id);
        REQUIRE (param != nullptr);
        CHECK (param->isAutomatable());
    }

    CHECK (processor.getBypassParameter() == processor.getValueTreeState().getParameter (ParamIDs::bypass));

    auto* ab = processor.getValueTreeState().getParameter (ParamIDs::listenRef);
    CHECK (ab->getText (0.0f, 32) == "Mix");
    CHECK (ab->getText (1.0f, 32) == "Reference");
}

TEST_CASE ("State round-trips through get/setStateInformation", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::MemoryBlock saved;
    {
        BenchmarkProcessor source;
        setParam (source, ParamIDs::listenRef, 1.0f);
        setParam (source, ParamIDs::loudnessMatch, 0.0f);
        source.setSavedEditorSize ({ 1234, 777 });
        source.getStateInformation (saved);
    }

    BenchmarkProcessor restored;
    REQUIRE (juce::exactlyEqual (getParam (restored, ParamIDs::listenRef), 0.0f));
    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    CHECK (juce::exactlyEqual (getParam (restored, ParamIDs::listenRef), 1.0f));
    CHECK (juce::exactlyEqual (getParam (restored, ParamIDs::loudnessMatch), 0.0f));
    CHECK (restored.getSavedEditorSize() == juce::Point<int> { 1234, 777 });
}

TEST_CASE ("Corrupt or foreign state is ignored", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    BenchmarkProcessor processor;
    setParam (processor, ParamIDs::listenRef, 1.0f);

    const char garbage[] = "definitely not plugin state";
    processor.setStateInformation (garbage, (int) sizeof (garbage));
    processor.setStateInformation (nullptr, 0);

    juce::MemoryBlock foreign;
    juce::AudioProcessor::copyXmlToBinary (juce::XmlElement ("SomeOtherPlugin"), foreign);
    processor.setStateInformation (foreign.getData(), (int) foreign.getSize());

    CHECK (juce::exactlyEqual (getParam (processor, ParamIDs::listenRef), 1.0f));
}

//==============================================================================
#include "TestHelpers.h"
#include <catch2/catch_approx.hpp>

namespace
{
    struct ProcessorWithReference
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        testing::TempDir temp;
        BenchmarkProcessor processor;
        static constexpr double sampleRate = 48000.0;
        static constexpr int blockSize = 256;

        explicit ProcessorWithReference (const juce::AudioBuffer<float>& referenceAudio)
        {
            processor.prepareToPlay (sampleRate, blockSize);
            const auto file = temp / "reference.wav";
            testing::writeWav (file, referenceAudio, sampleRate);
            processor.getReferenceLibrary().addFiles ({ file });
            REQUIRE (processor.getReferenceLibrary().waitUntilLoaded (20000));
            setParam (processor, ParamIDs::loudnessMatch, 0.0f);
        }

        /** Runs `seconds` of the given constant-per-channel input; returns the output. */
        juce::AudioBuffer<float> run (const juce::AudioBuffer<float>& input)
        {
            juce::AudioBuffer<float> output (input);
            juce::MidiBuffer midi;

            for (int start = 0; start < output.getNumSamples(); start += blockSize)
            {
                const auto n = std::min (blockSize, output.getNumSamples() - start);
                juce::AudioBuffer<float> view (output.getArrayOfWritePointers(), output.getNumChannels(), start, n);
                processor.processBlock (view, midi);
            }

            return output;
        }
    };

    juce::AudioBuffer<float> constant (float value, int numSamples)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (b.getWritePointer (ch), value, numSamples);
        return b;
    }
}

TEST_CASE ("A/B switches between mix and reference with a smooth crossfade", "[processor][ab]")
{
    // Reference is a constant -0.25 (24-bit WAV), mix is a constant +0.5.
    ProcessorWithReference p (constant (-0.25f, 48000));

    const auto mixOnly = p.run (constant (0.5f, 4800));
    CHECK (mixOnly.getSample (0, 4799) == Catch::Approx (0.5f));

    setParam (p.processor, ParamIDs::listenRef, 1.0f);
    const auto switched = p.run (constant (0.5f, 4800));

    float maxDelta = std::abs (switched.getSample (0, 0) - 0.5f);
    for (int i = 1; i < switched.getNumSamples(); ++i)
        maxDelta = std::max (maxDelta, std::abs (switched.getSample (0, i) - switched.getSample (0, i - 1)));

    CHECK (maxDelta < 0.005f); // no click
    CHECK (switched.getSample (0, 4799) == Catch::Approx (-0.25f).margin (1e-4));
    CHECK (switched.getSample (1, 4799) == Catch::Approx (-0.25f).margin (1e-4));

    // Back to the mix.
    setParam (p.processor, ParamIDs::listenRef, 0.0f);
    const auto back = p.run (constant (0.5f, 4800));
    CHECK (back.getSample (0, 4799) == Catch::Approx (0.5f));

    // Bypass always plays the mix.
    setParam (p.processor, ParamIDs::listenRef, 1.0f);
    setParam (p.processor, ParamIDs::bypass, 1.0f);
    const auto bypassed = p.run (constant (0.5f, 4800));
    CHECK (bypassed.getSample (0, 4799) == Catch::Approx (0.5f));
}

TEST_CASE ("Without a reference, Reference mode keeps playing the mix", "[processor][ab]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    BenchmarkProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    setParam (processor, ParamIDs::listenRef, 1.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    fillWithNoise (buffer, 3);
    juce::AudioBuffer<float> expected (buffer);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    for (int i = 0; i < 512; ++i)
        REQUIRE (juce::exactlyEqual (buffer.getSample (0, i), expected.getSample (0, i)));
}

TEST_CASE ("Loudness matching turns the louder source down", "[processor][match]")
{
    constexpr double sr = ProcessorWithReference::sampleRate;

    SECTION ("Mix louder than reference")
    {
        ProcessorWithReference p (testing::makeSine (sr, 10.0, 1000.0, -23.0)); // -23 LUFS
        setParam (p.processor, ParamIDs::loudnessMatch, 1.0f);
        p.run (testing::makeSine (sr, 6.0, 500.0, -14.0));                       // about -14 LUFS

        const auto& r = p.processor.getReadings();
        CHECK (r.mixGainDb.load() == Catch::Approx (r.referenceIntegrated.load() - r.mixIntegrated.load()).margin (0.05));
        CHECK (r.mixGainDb.load() < -8.0f);
        CHECK (r.referenceGainDb.load() == Catch::Approx (0.0f).margin (1e-3));
    }

    SECTION ("Reference louder than mix")
    {
        ProcessorWithReference p (testing::makeSine (sr, 10.0, 1000.0, -10.0)); // -10 LUFS
        setParam (p.processor, ParamIDs::loudnessMatch, 1.0f);
        p.run (testing::makeSine (sr, 6.0, 1000.0, -20.0));                      // -20 LUFS

        const auto& r = p.processor.getReadings();
        CHECK (r.mixIntegrated.load() == Catch::Approx (-20.0).margin (0.1));
        CHECK (r.referenceIntegrated.load() == Catch::Approx (-10.0).margin (0.1));
        CHECK (r.referenceGainDb.load() == Catch::Approx (-10.0).margin (0.15));
        CHECK (r.mixGainDb.load() == Catch::Approx (0.0f).margin (1e-3));

        // Listening to the reference now gives the mix's loudness.
        setParam (p.processor, ParamIDs::listenRef, 1.0f);
        const auto out = p.run (testing::makeSine (sr, 4.0, 1000.0, -20.0));
        const auto rms = out.getRMSLevel (0, 48000, 48000 * 3);
        CHECK (juce::Decibels::gainToDecibels (rms) == Catch::Approx (-20.0 - 3.01).margin (0.2)); // sine RMS = peak - 3 dB
    }

    SECTION ("Match off applies no gain")
    {
        ProcessorWithReference p (testing::makeSine (sr, 10.0, 1000.0, -23.0));
        p.run (testing::makeSine (sr, 4.0, 1000.0, -14.0));
        CHECK (p.processor.getReadings().mixGainDb.load() == Catch::Approx (0.0f).margin (1e-3));
    }
}

TEST_CASE ("Processor state includes the reference list", "[state][library]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    testing::TempDir temp;
    const auto file = temp / "ref.wav";
    testing::writeWav (file, testing::makeSine (48000.0, 1.0, 1000.0, -20.0), 48000.0);

    juce::MemoryBlock saved;
    {
        BenchmarkProcessor source;
        source.prepareToPlay (48000.0, 512);
        source.getReferenceLibrary().addFiles ({ file });
        source.getStateInformation (saved);
    }

    BenchmarkProcessor restored;
    restored.prepareToPlay (48000.0, 512);
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    REQUIRE (restored.getReferenceLibrary().waitUntilLoaded (20000));

    REQUIRE (restored.getReferenceLibrary().size() == 1);
    CHECK (restored.getReferenceLibrary()[0].file == file);
    CHECK (restored.getReferenceLibrary().getActiveIndex() == 0);
    CHECK (restored.getReferenceLibrary()[0].status == benchmark::ReferenceLibrary::Status::ready);

    // Saving again doesn't duplicate the list.
    juce::MemoryBlock again;
    restored.getStateInformation (again);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
    REQUIRE (xml != nullptr);
    CHECK (xml->getNumChildElements() > 0);

    int referenceLists = 0;
    for (auto* child : xml->getChildIterator())
        if (child->hasTagName ("References"))
            ++referenceLists;
    CHECK (referenceLists == 1);
}

TEST_CASE ("State restored from a background thread applies parameters immediately", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    testing::TempDir temp;
    const auto file = temp / "ref.wav";
    testing::writeWav (file, testing::makeSine (48000.0, 1.0, 1000.0, -20.0), 48000.0);

    juce::MemoryBlock saved;
    {
        BenchmarkProcessor source;
        source.prepareToPlay (48000.0, 512);
        setParam (source, ParamIDs::listenRef, 1.0f);
        setParam (source, ParamIDs::loudnessMatch, 0.0f);
        source.getReferenceLibrary().addFiles ({ file });
        source.getStateInformation (saved);
    }

    BenchmarkProcessor restored;
    restored.prepareToPlay (48000.0, 512);

    std::thread host ([&] { restored.setStateInformation (saved.getData(), (int) saved.getSize()); });
    host.join();

    // Like a host or validator reading values back straight away.
    CHECK (juce::exactlyEqual (getParam (restored, ParamIDs::listenRef), 1.0f));
    CHECK (juce::exactlyEqual (getParam (restored, ParamIDs::loudnessMatch), 0.0f));

    // The reference list follows once the message thread runs.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    REQUIRE (restored.getReferenceLibrary().waitUntilLoaded (20000));
    REQUIRE (restored.getReferenceLibrary().size() == 1);
    CHECK (restored.getReferenceLibrary()[0].file == file);
}
