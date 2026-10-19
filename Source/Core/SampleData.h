#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pattern.h"

namespace bf
{

/** An immutable, ready-to-play sample. Created on a background thread, never modified after
    publication, and only freed on the message thread. */
class SampleData final : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    SampleData (juce::AudioBuffer<float> b, double rate, juce::String displayName, juce::String filePath = {})
        : buffer (std::move (b)), sampleRate (rate), name (std::move (displayName)), path (std::move (filePath)) {}

    const juce::AudioBuffer<float> buffer; // mono or stereo
    const double sampleRate;
    const juce::String name;
    const juce::String path;               // empty for built-in kit sounds
};

/** Holds the sample assigned to each track.

    The message thread owns every SampleData via reference-counted pointers. The audio thread only
    reads raw pointers from atomics and takes extra references for voices that are playing, so it
    never triggers a deallocation: replaced samples are parked in a retire list and freed by
    collectGarbage() once nothing but this pool references them and a grace period has passed. */
class SampleSlots
{
public:
    SampleSlots() = default;

    // Owner thread (the message thread in the plugin) ----------------------------
    void set (int track, SampleData::Ptr sample)
    {
        if (owned[(size_t) track] != nullptr)
            retired.push_back ({ owned[(size_t) track], juce::Time::getMillisecondCounter() });

        owned[(size_t) track] = std::move (sample);
        current[(size_t) track].store (owned[(size_t) track].get(), std::memory_order_release);
    }

    SampleData::Ptr get (int track) const { return owned[(size_t) track]; }

    void collectGarbage()
    {
        const auto now = juce::Time::getMillisecondCounter();

        retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const Retired& r) {
                           return r.sample->getReferenceCount() == 1 && now - r.retiredAt > 2000;
                       }),
                       retired.end());
    }

    // Audio thread -----------------------------------------------------------
    SampleData* acquire (int track) const noexcept { return current[(size_t) track].load (std::memory_order_acquire); }

private:
    struct Retired
    {
        SampleData::Ptr sample;
        juce::uint32 retiredAt;
    };

    std::array<std::atomic<SampleData*>, kNumTracks> current {};
    std::array<SampleData::Ptr, kNumTracks> owned;
    std::vector<Retired> retired;
};

} // namespace bf
