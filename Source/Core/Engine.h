#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Params.h"
#include "SampleData.h"
#include "Scheduler.h"
#include "Snapshot.h"
#include "Voice.h"

namespace bf
{

/** Host transport snapshot for one block. */
struct TransportInfo
{
    bool isPlaying = false;
    bool hasPosition = false;
    double bpm = 174.0;
    double ppqStart = 0.0;
};

/** Optional per-track stereo outputs (multi-out). Null pointers mean "mix into main". */
struct TrackOutputs
{
    std::array<std::array<float*, 2>, kNumTracks> channels {};
};

/** The real-time drum engine: sequencer + groove + sampler voices. Shared by the plugin's
    audio thread and the offline renderer. process() never allocates, locks or does I/O. */
class Engine
{
public:
    explicit Engine (SampleSlots& slots);

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Renders one block. `midi` holds the incoming MIDI and is replaced with the outgoing MIDI. */
    void process (juce::AudioBuffer<float>& main, const TrackOutputs* trackOuts, juce::MidiBuffer& midi,
                  const TransportInfo&, const EngineParams&, const SequencerSnapshot&) noexcept;

    // Thread-safe controls (any thread) ------------------------------------
    void requestPattern (int index) noexcept { requestedPattern.store (index); }
    void audition (int track, float velocity) noexcept;

    int getActivePattern() const noexcept       { return activePatternForUi.load(); }
    /** Fractional step of the playing pattern, or -1 if the sequencer is not running. */
    float getPlayheadStep() const noexcept      { return playheadStep.load(); }
    /** Bit mask of tracks that triggered since the last call (for UI activity LEDs). */
    std::uint32_t takeTriggeredTracks() noexcept { return triggeredTracks.exchange (0); }

    /** Builds the groove context used by the scheduler (exposed for export and tests). */
    static GrooveContext makeGrooveContext (const EngineParams&, const SequencerSnapshot&) noexcept;

    static constexpr int voicesPerTrack = 6;

private:
    void processChunk (juce::AudioBuffer<float>& main, const TrackOutputs* trackOuts, int start, int length,
                       const juce::MidiBuffer& midiIn, juce::MidiBuffer& midiOut, const TransportInfo&,
                       const EngineParams&, const SequencerSnapshot&) noexcept;

    void triggerHit (const HitEvent&, int offset, const EngineParams&, const SequencerSnapshot&, int chunkStart,
                     double samplesPerPpq, juce::MidiBuffer& midiOut) noexcept;
    void handlePatternNote (const juce::MidiMessage&, MidiPatternMode) noexcept;
    void flushNoteOffs (juce::MidiBuffer& midiOut, int chunkStart, int length) noexcept;
    void allNotesOff (juce::MidiBuffer& midiOut, int offset) noexcept;

    SampleSlots& slots;
    double sampleRate = 44100.0;
    int maxBlock = 512;

    Scheduler scheduler;
    std::array<Scheduler::BlockHit, Scheduler::maxHitsPerBlock> hits {};
    juce::MidiBuffer scratchMidi;
    std::array<std::array<Voice, voicesPerTrack>, kNumTracks> voices {};
    std::uint64_t voiceAge = 0;

    juce::AudioBuffer<float> trackScratch; // kNumTracks * 2 channels
    std::array<juce::SmoothedValue<float>, kNumTracks> trackGain;

    // Pattern selection
    int activePattern = 0;
    int lastParamPattern = -1;
    bool patternSilenced = false;
    std::array<int, kNumPatterns> heldPatternOrder {};
    int numHeldPatterns = 0;
    std::atomic<int> requestedPattern { -1 };

    // MIDI out
    struct PendingOff { int note = -1; double samplesLeft = 0.0; };
    std::array<PendingOff, 128> pendingOffs {};
    bool wasPlaying = false;
    bool midiOutWasOn = false;

    // UI auditions (single producer: message thread)
    struct Audition { int track; float velocity; };
    juce::AbstractFifo auditionFifo { 32 };
    std::array<Audition, 32> auditionQueue {};

    std::atomic<int> activePatternForUi { 0 };
    std::atomic<float> playheadStep { -1.0f };
    std::atomic<std::uint32_t> triggeredTracks { 0 };
};

} // namespace bf
