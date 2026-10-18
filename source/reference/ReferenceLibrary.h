#pragma once

#include "ReferenceAudio.h"
#include "ReferenceLoader.h"
#include "util/RealtimeHandoff.h"
#include "util/TransportState.h"

#include <array>
#include <optional>

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace benchmark
{
    /**
        The list of up to 12 references and which one is active.

        Lives on the message thread. Loading happens on ReferenceLoader's threads;
        the active reference's audio reaches the audio thread via RealtimeHandoff.
        Keeps a thread-safe snapshot of its state for getStateInformation().
    */
    /** A loop region in seconds (sample-rate independent, as saved in sessions). */
    struct LoopRegion
    {
        double start = 0.0, end = 0.0;

        double getLength() const noexcept { return end - start; }
        bool operator== (const LoopRegion&) const = default;
    };

    class ReferenceLibrary final : public juce::ChangeBroadcaster,
                                   private juce::Timer
    {
    public:
        enum class Status
        {
            waiting,  // queued until the host sample rate is known
            loading,
            ready,
            missing,  // file not found; path kept so it can be relocated
            failed
        };

        struct Entry
        {
            juce::Uuid id;
            juce::File file;
            juce::String name;
            Status status = Status::waiting;
            juce::String message;

            ReferenceInfo info;
            bool hasInfo = false;

            std::shared_ptr<const ReferenceAudio> audio; // null while loading or when unloaded to save memory
            std::shared_ptr<const WaveformOverview> overview; // kept when audio is unloaded

            std::optional<LoopRegion> loop;
            bool loopEnabled = false;
            std::array<std::optional<LoopRegion>, 3> markers;
            std::shared_ptr<ReferenceLoader::Ticket> ticket;
            uint64_t lastUsed = 0;

            float getProgress() const noexcept { return ticket != nullptr ? ticket->progress.load() : 0.0f; }
        };

        struct AddResult
        {
            int added = 0, duplicates = 0, unsupported = 0, overLimit = 0;
        };

        static constexpr int maxReferences = 12;
        static constexpr int numMarkers = 3;
        static constexpr double minLoopSeconds = 0.05;
        static constexpr size_t defaultMemoryBudget = (size_t) 1536 * 1024 * 1024;

        explicit ReferenceLibrary (RealtimeHandoff<ReferenceAudio>& audioHandoff, TransportState* transport = nullptr);
        ~ReferenceLibrary() override;

        //==============================================================================
        AddResult addFiles (const juce::Array<juce::File>& files);
        void remove (int index);
        void relocate (int index, const juce::File& newFile);
        void setActive (int index);

        int size() const noexcept { return (int) entries.size(); }
        const Entry& operator[] (int index) const { return entries[(size_t) index]; }
        int getActiveIndex() const noexcept { return activeIndex; }
        const Entry* getActive() const noexcept;
        bool isAnyLoading() const noexcept;

        //==============================================================================
        // Loops and markers (per reference, saved with the session)

        /** Sets (and enables) or clears the loop. Too-short loops are rejected. */
        void setLoop (int index, std::optional<LoopRegion> region);
        void setLoopEnabled (int index, bool enabled);

        /** Saves the current loop into a marker slot, or clears the slot. */
        void setMarker (int index, int slot, std::optional<LoopRegion> region);

        /** Makes a marker the active loop and seeks to its start (Free mode). */
        void recallMarker (int index, int slot);

        /** Host sample rate. Loading waits until this is known; changing it reloads audio. */
        void setTargetSampleRate (double newRate);
        double getTargetSampleRate() const noexcept { return targetSampleRate; }

        void setMemoryBudget (size_t bytes);
        size_t getLoadedBytes() const noexcept;

        //==============================================================================
        static const juce::Identifier stateType;

        juce::ValueTree createState() const;
        void restoreState (const juce::ValueTree& state);

        /** Safe to call from any thread. */
        juce::ValueTree getStateSnapshot() const;

        /** User-facing notifications (skipped files, load errors). Message thread. */
        std::function<void (const juce::String&)> onMessage;

        /** Waits for background loads and delivers their results (tests only). */
        bool waitUntilLoaded (int timeoutMs);

    private:
        void timerCallback() override;

        Entry* findEntry (const juce::Uuid& id) noexcept;
        void startLoad (Entry& entry);
        void loadFinished (const juce::Uuid& id, const std::shared_ptr<ReferenceLoader::Ticket>& ticket, ReferenceDecoder::Result result);
        void publishActive();
        void publishLoop();
        void enforceMemoryBudget();
        void stateChanged();
        void notify (const juce::String& text);

        RealtimeHandoff<ReferenceAudio>& handoff;
        TransportState* transport = nullptr;
        ReferenceLoader loader;

        std::vector<Entry> entries;
        int activeIndex = -1;
        double targetSampleRate = 0.0;
        size_t memoryBudget = defaultMemoryBudget;
        uint64_t useCounter = 0;

        juce::CriticalSection snapshotLock;
        juce::ValueTree snapshot;

        JUCE_DECLARE_WEAK_REFERENCEABLE (ReferenceLibrary)
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceLibrary)
    };
}
