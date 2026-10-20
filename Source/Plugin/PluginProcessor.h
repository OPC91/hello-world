#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/Engine.h"
#include "Core/Exporter.h"
#include "Core/KitLibrary.h"
#include "Core/MasterBus.h"
#include "Core/Params.h"
#include "Core/PatternGenerator.h"
#include "Core/SampleManager.h"
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
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

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

    /** Applies an edit to the sequencer model and publishes it to the audio thread.
        Pass undoable = false for the 2nd and later edits of one mouse gesture. */
    template <typename Fn>
    void editModel (Fn&& fn, bool undoable = true)
    {
        const juce::ScopedLock sl (modelLock);

        if (undoable)
            pushUndoState();

        fn (model);
        publishModel();
        modelChanged.sendChangeMessage();
    }

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undo();
    void redo();

    // Generator -------------------------------------------------------------
    /** Generates a new pattern for the edit slot in the background (locked tracks are kept). */
    void generatePattern();
    /** Re-rolls one track of the edit pattern. */
    void generateTrack (int track);
    /** Writes a one-bar fill based on the edit pattern into the next slot. Returns that slot. */
    int generateFill();

    std::array<TrackSettings, bf::kNumTracks>& getTrackSettings() noexcept { return trackSettings; }

    /** Pattern selected for editing. Set immediately by the UI, and follows the playing pattern
        when MIDI notes or automation switch it on the audio thread. */
    int getEditPattern() const noexcept { return editPattern; }
    void selectPattern (int index);

    // Groove ----------------------------------------------------------------
    /** Selects a groove template and sets the swing knob to the template's swing. */
    void applyGrooveTemplate (int index);
    /** New humanize seed: playback stays repeatable, but with a different random feel. */
    void rerollHumanize();
    /** Saves the active template (with the current swing) to the user groove folder. */
    bool saveUserGroove (const juce::String& name);
    /** Loads a user groove file into the "User" template slot and selects it. */
    bool loadUserGroove (const juce::File&);
    void addGhostNotes();

    /** Sets a parameter from the message thread as a complete host gesture. */
    void setParameterValue (const juce::String& id, float plainValue);
    float getParameterValue (const juce::String& id) const;

    // Samples ---------------------------------------------------------------
    void loadSample (int track, const bf::SampleRef&);

    /** Makes one step of the edit pattern play its own sample (turning the step on). Reuses a
        per-hit slot that already holds the file. Returns the variant used, or -1 if all 15
        per-hit slots of the track are taken by other hits. */
    int assignHitSample (int track, int step, const bf::SampleRef&);
    /** Makes a step play the track's sample again. */
    void clearHitSample (int track, int step);
    /** True if any step of any pattern uses this per-hit sample variant. */
    bool isHitSampleUsed (int track, int variant) const;
    bf::SampleManager& getSampleManager() noexcept { return sampleManager; }

    // Export ----------------------------------------------------------------
    enum class ExportKind { Midi, AudioMix, AudioStems };

    /** Renders the edit pattern in the background (with swing and humanize, at the host tempo
        and rate) and calls back on the message thread with the written files. */
    void exportPattern (ExportKind, int bars, std::function<void (juce::Array<juce::File>, juce::String error)> onDone);

    // Kits ------------------------------------------------------------------
    const std::vector<bf::KitInfo>& getKits() const noexcept { return kits; }
    const juce::String& getCurrentKit() const noexcept { return currentKit; }
    /** Loads the kit's first sample for each track's role, skipping sample-locked tracks. */
    void selectKit (const juce::String& name);
    /** Picks a different sample of the current kit for the track's role. */
    void randomizeSample (int track);
    /** Randomizes every track whose sample isn't locked. */
    void randomizeAllSamples();
    /** Rescans the user Kits folder on a background thread. */
    void rescanKits();
    /** Broadcast when the kit list or the current kit changes. */
    juce::ChangeBroadcaster kitsChanged;

    bf::Engine& getEngine() noexcept { return engine; }
    bf::SampleSlots& getSampleSlots() noexcept { return sampleSlots; }

    /** Notifies the editor about model changes that didn't come from it (state load etc.). */
    juce::ChangeBroadcaster modelChanged;

    juce::AudioProcessorValueTreeState apvts;

private:
    static BusesProperties makeBuses();
    void timerCallback() override;
    void publishModel();
    void pushUndoState();
    bf::PatternGenerator::Options makeGeneratorOptions();

    bf::ParamCache paramCache;
    bf::EngineParams engineParams;
    bf::SampleSlots sampleSlots;
    bf::Engine engine { sampleSlots };
    bf::SampleManager sampleManager;
    std::atomic<double> pendingSampleRate { 0.0 };

    // The model is edited on the message thread, but hosts may save/restore state from other
    // threads, so writers serialise on this lock. The audio thread never takes it.
    juce::CriticalSection modelLock;
    bf::SequencerSnapshot model;
    bf::TripleBuffer<bf::SequencerSnapshot> snapshot;
    std::array<TrackSettings, bf::kNumTracks> trackSettings;
    std::vector<bf::PatternBank> undoStack, redoStack;
    int editPattern = 0;
    int lastEnginePattern = 0;
    static constexpr size_t maxUndo = 64;

    bf::TrackOutputs trackOutputs;
    bf::MasterBus masterBus;
    std::array<bf::LatencyDelay, bf::kNumTracks> auxDelays;
    std::shared_ptr<bool> aliveFlag = std::make_shared<bool> (true);

    const bf::KitInfo* findKit (const juce::String&) const;
    std::vector<bf::KitInfo> kits;
    juce::String currentKit { "Clean" };
    juce::Random random;

    /** Kit scans, pattern generation and export renders. Joined in the destructor. */
    juce::ThreadPool backgroundPool { juce::ThreadPoolOptions{}.withThreadName ("BreakForge Worker").withNumberOfThreads (1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakForgeProcessor)
};
