#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Core/DrumSynth.h"

BreakForgeProcessor::BreakForgeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BreakForgeParams", createLayout())
{
    masterVolume = apvts.getRawParameterValue ("masterVolume");
}

juce::AudioProcessorValueTreeState::ParameterLayout BreakForgeProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "masterVolume", 1 }, "Master Volume",
                                                             juce::NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

bool BreakForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void BreakForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    engine.setKick (bf::DrumSynth::renderKick (sampleRate, {}));
}

void BreakForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    bf::TransportInfo transport;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            transport.isPlaying = pos->getIsPlaying();
            transport.bpm = pos->getBpm().orFallback (174.0);
            transport.ppqStart = pos->getPpqPosition().orFallback (0.0);
        }
    }

    engine.process (buffer, transport);
    buffer.applyGain (juce::Decibels::decibelsToGain (masterVolume->load(), -48.0f));
}

void BreakForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BreakForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BreakForgeProcessor::createEditor()
{
    return new BreakForgeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BreakForgeProcessor();
}
