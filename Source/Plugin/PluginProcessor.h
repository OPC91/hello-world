#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/Engine.h"
#include "Core/Params.h"
#include "Core/TripleBuffer.h"

/** Message-thread-only per-track settings that aren't automatable parameters. */
struct TrackSettings
{
    juce::String name;
    bool generatorLocked = false;
    bool sampleLocked = false;
};

class BreakForgeProcessor final : public juce::AudioProcessor,
                                  private juce::Timer
{
public:
    BreakForgeProcessor();
    ~BreakForgeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Message-thread model access ------------------------------------------
    const bf::SequencerSnapshot& getModel() const noexcept { return model; }

    /** Applies an edit to the sequencer model and publishes it to the audio thread. */
    template <typename Fn>
    void editModel (Fn&& fn)
    {
        const juce::ScopedLock sl (modelLock);
        fn (model);
        publishModel();
    }

    std::array<TrackSettings, bf::kNumTracks>& getTrackSettings() noexcept { return trackSettings; }

    /** Pattern currently selected for editing (follows the playing pattern). */
    int getEditPattern() const noexcept { return engine.getActivePattern(); }
    void selectPattern (int index);

    bf::Engine& getEngine() noexcept { return engine; }
    bf::SampleSlots& getSampleSlots() noexcept { return sampleSlots; }

    /** Notifies the editor about model changes that didn't come from it (state load etc.). */
    juce::ChangeBroadcaster modelChanged;

    juce::AudioProcessorValueTreeState apvts;

private:
    static BusesProperties makeBuses();
    void timerCallback() override;
    void publishModel();
    void installTemporarySamples (double sampleRate);

    bf::ParamCache paramCache;
    bf::EngineParams engineParams;
    bf::SampleSlots sampleSlots;
    bf::Engine engine { sampleSlots };

    // The model is edited on the message thread, but hosts may save/restore state from other
    // threads, so writers serialise on this lock. The audio thread never takes it.
    juce::CriticalSection modelLock;
    bf::SequencerSnapshot model;
    bf::TripleBuffer<bf::SequencerSnapshot> snapshot;
    std::array<TrackSettings, bf::kNumTracks> trackSettings;

    bf::TrackOutputs trackOutputs;
    double preparedSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakForgeProcessor)
};
