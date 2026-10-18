#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Crossfader.h"
#include "dsp/LoudnessMeter.h"
#include "dsp/ReferencePlayer.h"
#include "reference/ReferenceLibrary.h"
#include "util/RealtimeHandoff.h"
#include "util/TransportState.h"

namespace benchmark
{
    /** Values written by the audio thread and read by the UI. */
    struct LiveReadings
    {
        static constexpr float noValue = -std::numeric_limits<float>::infinity();

        std::atomic<float> mixMomentary { noValue };
        std::atomic<float> mixShortTerm { noValue };
        std::atomic<float> mixIntegrated { noValue };
        std::atomic<float> referenceShortTerm { noValue };
        std::atomic<float> referenceIntegrated { noValue }; // of the active reference, as played
        std::atomic<float> mixGainDb { 0.0f };              // loudness-match offsets being applied
        std::atomic<float> referenceGainDb { 0.0f };
        std::atomic<bool> referenceAvailable { false };
        std::atomic<float> crossfadePosition { 0.0f };      // 0 = Mix, 1 = Reference
    };

    class BenchmarkProcessor final : public juce::AudioProcessor,
                                     private juce::AsyncUpdater
    {
    public:
        BenchmarkProcessor();
        ~BenchmarkProcessor() override;

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
        ReferenceLibrary& getReferenceLibrary() noexcept { return library; }
        TransportState& getTransport() noexcept { return transport; }
        double getCurrentSampleRate() const noexcept { return hostSampleRate.load(); }
        const LiveReadings& getReadings() const noexcept { return readings; }

        /** Restarts the mix's integrated loudness measurement (thread-safe). */
        void resetMixLoudness() noexcept { resetMixRequested.store (true); }

        // Editor size is remembered with the session.
        juce::Point<int> getSavedEditorSize() const;
        void setSavedEditorSize (juce::Point<int> size);

        static constexpr int stateVersion = 1;
        static constexpr double gainSmoothingSeconds = 0.25;

    private:
        void handleAsyncUpdate() override;
        void processChunk (float* const* channels, int numChannels, int numSamples, const ReferenceAudio* reference) noexcept;
        void updateTransport() noexcept;

        juce::AudioProcessorValueTreeState parameters;
        std::atomic<float>* bypassValue = nullptr;
        std::atomic<float>* listenReferenceValue = nullptr;
        std::atomic<float>* loudnessMatchValue = nullptr;
        std::atomic<float>* syncModeValue = nullptr;

        // Declared before the library, which publishes into them.
        RealtimeHandoff<ReferenceAudio> referenceHandoff;
        TransportState transport;
        ReferenceLibrary library;

        std::atomic<double> hostSampleRate { 0.0 };

        // Audio-thread state.
        double currentSampleRate = 48000.0;
        int scratchCapacity = 0;
        juce::AudioBuffer<float> referenceScratch;
        dsp::LoudnessMeter mixMeter, referenceMeter;
        dsp::Crossfader crossfader;
        dsp::ReferencePlayer player;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> mixGain, referenceGain;
        const ReferenceAudio* lastReference = nullptr;
        uint32_t lastMeterUpdate = 0;

        std::atomic<bool> resetMixRequested { false };
        LiveReadings readings;

        JUCE_DECLARE_WEAK_REFERENCEABLE (BenchmarkProcessor)
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BenchmarkProcessor)
    };
}
