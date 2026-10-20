#pragma once

#include <juce_events/juce_events.h>

#include "SampleData.h"
#include "SampleRef.h"

namespace bf
{

/** Loads track samples (and per-hit sample variants) on a background thread and installs them
    into SampleSlots on the message thread. Decoding, resampling and kit rendering never happen on
    the audio thread.

    Missing or unreadable files never crash or silence a track: the track falls back to a
    built-in sound (a per-hit sample falls back to the track's sample), keeps its original
    reference (so saving the project doesn't lose the path) and reports a warning the UI can show. */
class SampleManager : public juce::ChangeBroadcaster
{
public:
    /** Renders a built-in sound id at a sample rate (called on the loader thread). */
    using BuiltInRenderer = std::function<juce::AudioBuffer<float> (const juce::String& id, double sampleRate)>;
    /** Built-in sound to use for a track when its file can't be loaded. */
    using FallbackProvider = std::function<SampleRef (int track)>;

    SampleManager (SampleSlots&, BuiltInRenderer, FallbackProvider);
    ~SampleManager() override;

    /** Sets the rate samples are prepared at; reloads everything if it changed. */
    void setSampleRate (double);
    double getSampleRate() const noexcept { return sampleRate; }

    /** Requests a sample for a track (variant 0) or one of its per-hit samples (1..15). Loading
        is asynchronous; results are applied by handleCompletedJobs(). Before a sample rate is
        known the request is just remembered. */
    void load (int track, const SampleRef&, int variant = 0);

    /** Applies finished loads. Call regularly on the message thread. */
    void handleCompletedJobs();

    /** For tests and offline use: blocks until all queued loads finished, then applies them. */
    bool waitAndApply (int timeoutMs);

    const SampleRef& getRef (int track, int variant = 0) const noexcept        { return refs[slot (track, variant)]; }
    bool hasWarning (int track, int variant = 0) const noexcept                { return warnings[slot (track, variant)].isNotEmpty(); }
    const juce::String& getWarning (int track, int variant = 0) const noexcept { return warnings[slot (track, variant)]; }
    bool isLoading (int track, int variant = 0) const noexcept                 { return pending[slot (track, variant)] > 0; }

    static size_t slot (int track, int variant) noexcept
    {
        return (size_t) (juce::jlimit (0, kNumTracks - 1, track) * kSampleVariants + juce::jlimit (0, kSampleVariants - 1, variant));
    }

private:
    struct Result
    {
        int track = 0;
        int variant = 0;
        std::uint32_t generation = 0;
        SampleData::Ptr data;
        juce::String error;
        SampleRef loadedRef;
    };

    void startJob (int track, int variant, const SampleRef& ref, bool isFallback);
    Result runJob (int track, int variant, std::uint32_t generation, SampleRef ref, double rate) const;

    SampleSlots& slots;
    BuiltInRenderer renderBuiltIn;
    FallbackProvider fallbackFor;

    double sampleRate = 0.0;
    std::array<SampleRef, kNumSampleSlots> refs;
    std::array<juce::String, kNumSampleSlots> warnings;
    std::array<std::uint32_t, kNumSampleSlots> generation {};
    std::array<int, kNumSampleSlots> pending {};

    juce::CriticalSection resultLock;
    std::vector<Result> results;

    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("BreakForge Loader").withNumberOfThreads (1) };
};

} // namespace bf
