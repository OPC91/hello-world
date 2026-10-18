#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "analysis/AnalysisEngine.h"
#include "dsp/BandSolo.h"
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

    /** Progress of an alignment, for the UI (message thread). */
    struct AlignmentStatus
    {
        enum class State { idle, listening, analysing, done, failed };
        State state = State::idle;
        float progress = 0.0f;
        juce::String message;
    };

    class BenchmarkProcessor final : public juce::AudioProcessor,
                                     private juce::AsyncUpdater,
                                     private juce::Timer
    {
    public:
        BenchmarkProcessor();
        ~BenchmarkProcessor() override;

        //==============================================================================
        void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;
        using AudioProcessor::processBlockBypassed;

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
        analysis::AnalysisEngine& getAnalysis() noexcept { return analysisEngine; }
        double getCurrentSampleRate() const noexcept { return hostSampleRate.load(); }
        const LiveReadings& getReadings() const noexcept { return readings; }

        //==============================================================================
        // Alignment (message thread): capture the session playing in Sync mode, then
        // find where it sits in the active reference and set its Sync offset.
        static constexpr double alignmentCaptureSeconds = 15.0;
        bool startAlignment();
        void cancelAlignment();
        const AlignmentStatus& getAlignmentStatus() const noexcept { return alignmentStatus; }

        /** Restarts the mix's integrated loudness measurement (thread-safe). */
        void resetMixLoudness() noexcept { resetMixRequested.store (true); }

        // Editor size is remembered with the session.
        juce::Point<int> getSavedEditorSize() const;
        void setSavedEditorSize (juce::Point<int> size);

        static constexpr int stateVersion = 1;
        static constexpr double gainSmoothingSeconds = 0.25;

    private:
        void handleAsyncUpdate() override;
        void timerCallback() override;
        void processInternal (juce::AudioBuffer<float>&, bool hostBypassed) noexcept;
        void captureForAlignment (const juce::AudioBuffer<float>&, int numChannels) noexcept;
        void updateLatency();
        void launchAlignmentAnalysis();
        void processChunk (float* const* channels, int numChannels, int numSamples, const ReferenceAudio* reference) noexcept;
        void updateTransport() noexcept;

        juce::AudioProcessorValueTreeState parameters;
        std::atomic<float>* bypassValue = nullptr;
        std::atomic<float>* listenReferenceValue = nullptr;
        std::atomic<float>* loudnessMatchValue = nullptr;
        std::atomic<float>* syncModeValue = nullptr;
        std::atomic<float>* bandSoloValue = nullptr;
        std::atomic<float>* crossoverModeValue = nullptr;

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
        dsp::BandSolo bandSolo;
        int64_t lastHostTime = 0;
        bool lastHostTimeValid = false, lastHostPlaying = false;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> mixGain, referenceGain;
        const ReferenceAudio* lastReference = nullptr;
        uint32_t lastMeterUpdate = 0;

        analysis::AnalysisEngine analysisEngine;

        // Alignment capture: armed on the message thread, filled by the audio thread.
        enum CaptureState { captureIdle = 0, captureArmed, captureRunning, captureComplete };
        std::vector<float> captureBuffer;
        std::atomic<int> captureState { captureIdle };
        std::atomic<int> captureWritten { 0 };
        int64_t captureStartTime = 0, captureExpectedTime = 0;
        std::atomic<int64_t> captureStartPublished { 0 };
        juce::ThreadPool alignmentPool { juce::ThreadPoolOptions {}.withThreadName ("Benchmark alignment").withNumberOfThreads (1) };
        std::shared_ptr<std::atomic<bool>> alignmentCancel;
        juce::Uuid alignmentReferenceId;
        AlignmentStatus alignmentStatus;

        std::atomic<bool> resetMixRequested { false };
        LiveReadings readings;

        JUCE_DECLARE_WEAK_REFERENCEABLE (BenchmarkProcessor)
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BenchmarkProcessor)
    };
}
