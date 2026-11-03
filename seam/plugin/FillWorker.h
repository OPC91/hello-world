#pragma once

#include "engine/SeamEngine.h"
#include "engine/fill/MidiExport.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace seam
{
    /** What the editor shows about the loaded break (a snapshot, copied under a lock). */
    struct BreakView
    {
        juce::String name;
        juce::String summary;
        bool builtIn = true;
        bool loading = false;
        juce::String error;
        std::vector<float> peaks;                     ///< max |x| per column
        std::vector<std::pair<float, engine::SliceType>> markers;   ///< slice start (0..1) and type
        juce::String fillDescription;
    };

    /** The background thread behind the fill engine. It never touches the audio thread except through the engine's
        lock-free publish calls.
          - Decodes and slices dropped breaks (WAV/AIFF/FLAC), or uses the built-in break.
          - Regenerates the fill pattern whenever Style, Complexity, Fill length, the time signature, the kit or the
            seed changes (polled every 15 ms, so automation is followed within a block or two).
          - Frees retired kits and patterns. */
    class FillWorker final : private juce::Thread
    {
    public:
        FillWorker (engine::SeamEngine&, juce::AudioProcessorValueTreeState&);
        ~FillWorker() override;

        void startWorking() { startThread (juce::Thread::Priority::low); }

        /** Load from the original file bytes (also how sessions restore an embedded break). Message thread. */
        void loadFromMemory (juce::MemoryBlock data, const juce::String& name, const juce::String& path);
        void useBuiltIn();

        /** "New fill": a new seed. Any thread. */
        void newFill() { seed.fetch_add (1); }
        std::uint64_t getSeed() const noexcept { return seed.load(); }
        void setSeed (std::uint64_t s) { seed.store (s); }

        /** Time signature from the audio thread (bar length in quarter notes). */
        void setQuartersPerBar (double q) noexcept { quartersPerBar.store (q); }

        BreakView getView() const;
        /** The current fill as a MIDI clip (C1 = slice 1). Empty if there is no fill. */
        std::vector<std::uint8_t> exportMidi (double bpm, int numerator, int denominator) const;
        /** Bumped whenever the view changes, so the editor can repaint only then. */
        int getViewVersion() const noexcept { return viewVersion.load(); }

    private:
        void run() override;
        void decodeAndSlice (const juce::MemoryBlock&, const juce::String& name);
        void installKit (std::unique_ptr<engine::BreakKit>, bool builtIn, const juce::String& error = {});
        void regenerateIfNeeded();

        engine::SeamEngine& engine;
        juce::AudioProcessorValueTreeState& params;
        juce::AudioFormatManager formats;

        // Load requests (message thread -> worker).
        juce::CriticalSection requestLock;
        std::unique_ptr<juce::MemoryBlock> pendingData;
        juce::String pendingName;
        bool pendingBuiltIn = false;

        // Worker-owned state.
        engine::KitSummary summary;
        std::uint64_t kitId = 0;
        double kitSampleRate = 44100.0;
        struct Request { int style = -1; int complexity = -1; int bars = -1; double qpb = -1.0; std::uint64_t seed = 0; std::uint64_t kit = 0; };
        Request lastRequest;

        std::atomic<std::uint64_t> seed { 1 };
        std::atomic<double> quartersPerBar { 4.0 };
        std::atomic<int> viewVersion { 0 };

        mutable juce::CriticalSection viewLock;
        BreakView view;
        std::shared_ptr<const engine::FillPattern> currentPattern;
        engine::KitSummary currentSummary;

        static std::atomic<std::uint64_t> nextKitId;
    };
}
