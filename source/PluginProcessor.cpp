#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

namespace benchmark
{
    namespace StateIDs
    {
        static const juce::Identifier root { "BenchmarkState" };
        static const juce::Identifier version { "stateVersion" };
        static const juce::Identifier editorWidth { "editorWidth" };
        static const juce::Identifier editorHeight { "editorHeight" };
    }

    BenchmarkProcessor::BenchmarkProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          parameters (*this, nullptr, StateIDs::root, createParameterLayout())
    {
        bypassValue = parameters.getRawParameterValue (ParamIDs::bypass);
    }

    //==============================================================================
    void BenchmarkProcessor::prepareToPlay (double, int)
    {
    }

    void BenchmarkProcessor::releaseResources()
    {
    }

    bool BenchmarkProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto& out = layouts.getMainOutputChannelSet();

        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
            return false;

        // The plugin is an insert on the master bus: input and output must match.
        return layouts.getMainInputChannelSet() == out;
    }

    void BenchmarkProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        midi.clear();

        const auto numInputs = getTotalNumInputChannels();
        const auto numOutputs = getTotalNumOutputChannels();

        for (auto ch = numInputs; ch < numOutputs; ++ch)
            buffer.clear (ch, 0, buffer.getNumSamples());

        if (bypassValue->load (std::memory_order_relaxed) >= 0.5f)
            return;

        // Phase 1: the host signal ("Mix") passes through unchanged.
    }

    juce::AudioProcessorParameter* BenchmarkProcessor::getBypassParameter() const
    {
        return parameters.getParameter (ParamIDs::bypass);
    }

    //==============================================================================
    juce::AudioProcessorEditor* BenchmarkProcessor::createEditor()
    {
        return new BenchmarkEditor (*this);
    }

    //==============================================================================
    void BenchmarkProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto state = parameters.copyState();
        state.setProperty (StateIDs::version, stateVersion, nullptr);

        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void BenchmarkProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto xml = getXmlFromBinary (data, sizeInBytes);

        if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
            return; // Unknown or corrupt data: keep current state.

        parameters.replaceState (juce::ValueTree::fromXml (*xml));
    }

    juce::Point<int> BenchmarkProcessor::getSavedEditorSize() const
    {
        return { (int) parameters.state.getProperty (StateIDs::editorWidth, 0),
                 (int) parameters.state.getProperty (StateIDs::editorHeight, 0) };
    }

    void BenchmarkProcessor::setSavedEditorSize (juce::Point<int> size)
    {
        parameters.state.setProperty (StateIDs::editorWidth, size.x, nullptr);
        parameters.state.setProperty (StateIDs::editorHeight, size.y, nullptr);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new benchmark::BenchmarkProcessor();
}
