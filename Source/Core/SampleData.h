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

/** Holds the samples of every track: variant 0 is the track's own sample, variants 1..15 are the
    per-hit samples that individual steps can select.

    The message thread owns every SampleData via reference-counted pointers. The audio thread only
    reads raw pointers from atomics and takes extra references for voices that are playing, so it
    never triggers a deallocation: replaced samples are parked in a retire list and freed by
    collectGarbage() once nothing but this pool references them and a grace period has passed. */
class SampleSlots
{
public:
    SampleSlots() = default;

    // Owner thread (the message thread in the plugin) ----------------------------
    void set (int track, SampleData::Ptr sample, int variant = 0)
    {
        const auto i = index (track, variant);

        if (owned[i] != nullptr)
            retired.push_back ({ owned[i], juce::Time::getMillisecondCounter() });

        owned[i] = std::move (sample);
        current[i].store (owned[i].get(), std::memory_order_release);
    }

    SampleData::Ptr get (int track, int variant = 0) const { return owned[index (track, variant)]; }

    void collectGarbage()
    {
        const auto now = juce::Time::getMillisecondCounter();

        retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const Retired& r) {
                           return r.sample->getReferenceCount() == 1 && now - r.retiredAt > 2000;
                       }),
                       retired.end());
    }

    // Audio thread -----------------------------------------------------------
    SampleData* acquire (int track, int variant = 0) const noexcept { return current[index (track, variant)].load (std::memory_order_acquire); }

private:
    static size_t index (int track, int variant) noexcept
    {
        return (size_t) (juce::jlimit (0, kNumTracks - 1, track) * kSampleVariants + juce::jlimit (0, kSampleVariants - 1, variant));
    }

    struct Retired
    {
        SampleData::Ptr sample;
        juce::uint32 retiredAt;
    };

    std::array<std::atomic<SampleData*>, kNumSampleSlots> current {};
    std::array<SampleData::Ptr, kNumSampleSlots> owned;
    std::vector<Retired> retired;
};

} // namespace bf
