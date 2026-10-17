#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace benchmark
{
    class BenchmarkProcessor final : public juce::AudioProcessor
    {
    public:
        BenchmarkProcessor();
        ~BenchmarkProcessor() override = default;

        //==============================================================================
        void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorParameter* getBypassParameter() const override;

        //==============================================================================
        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return "Benchmark"; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        //==============================================================================
        juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }

        // Editor size is remembered with the session.
        juce::Point<int> getSavedEditorSize() const;
        void setSavedEditorSize (juce::Point<int> size);

        static constexpr int stateVersion = 1;

    private:
        juce::AudioProcessorValueTreeState parameters;
        std::atomic<float>* bypassValue = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BenchmarkProcessor)
    };
}
