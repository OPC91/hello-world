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
