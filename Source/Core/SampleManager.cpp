#include "SampleManager.h"
#include "AudioFileLoader.h"

namespace bf
{

SampleManager::SampleManager (SampleSlots& s, BuiltInRenderer r, FallbackProvider f)
    : slots (s), renderBuiltIn (std::move (r)), fallbackFor (std::move (f))
{
}

SampleManager::~SampleManager()
{
    pool.removeAllJobs (true, 10000);
}

void SampleManager::setSampleRate (double newRate)
{
    if (newRate <= 0.0 || juce::approximatelyEqual (newRate, sampleRate))
        return;

    sampleRate = newRate;

    for (int t = 0; t < kNumTracks; ++t)
        if (! refs[(size_t) t].isEmpty())
            startJob (t, refs[(size_t) t], false);
}

void SampleManager::load (int track, const SampleRef& ref)
{
    if (track < 0 || track >= kNumTracks)
        return;

    refs[(size_t) track] = ref;
    warnings[(size_t) track].clear();

    if (ref.isEmpty())
    {
        ++generation[(size_t) track];
        slots.set (track, nullptr);
        sendChangeMessage();
        return;
    }

    if (sampleRate > 0.0)
        startJob (track, ref, false);

    sendChangeMessage();
}

void SampleManager::startJob (int track, const SampleRef& ref, bool isFallback)
{
    // A newer request supersedes anything still in flight for this track, except that a
    // fallback keeps the generation of the request it is standing in for.
    const auto gen = isFallback ? generation[(size_t) track] : ++generation[(size_t) track];
    const double rate = sampleRate;
    ++pending[(size_t) track];

    pool.addJob ([this, track, gen, ref, rate] {
        auto result = runJob (track, gen, ref, rate);
        const juce::ScopedLock sl (resultLock);
        results.push_back (std::move (result));
    });
}

SampleManager::Result SampleManager::runJob (int track, std::uint32_t gen, SampleRef ref, double rate) const
{
    Result r;
    r.track = track;
    r.generation = gen;
    r.loadedRef = ref;

    juce::AudioBuffer<float> audio;

    if (ref.isBuiltIn())
    {
        audio = renderBuiltIn != nullptr ? renderBuiltIn (ref.builtInId, rate) : juce::AudioBuffer<float>();

        if (audio.getNumSamples() == 0)
            r.error = "Unknown built-in sound: " + ref.builtInId;
    }
    else
    {
        audio = loadAudioFile (ref.file, rate, r.error);
    }

    if (audio.getNumSamples() > 0)
        r.data = new SampleData (std::move (audio), rate, ref.getDisplayName(), ref.isFile() ? ref.file.getFullPathName() : juce::String());

    return r;
}

void SampleManager::handleCompletedJobs()
{
    std::vector<Result> done;

    {
        const juce::ScopedLock sl (resultLock);
        done.swap (results);
    }

    bool changed = false;

    for (auto& r : done)
    {
        const auto t = (size_t) r.track;
        pending[t] = juce::jmax (0, pending[t] - 1);
        changed = true;

        if (r.generation != generation[t])
            continue; // superseded by a newer request

        if (r.data != nullptr)
        {
            slots.set (r.track, r.data);
            continue;
        }

        // Failed: warn, and fall back to a built-in sound so the track still plays.
        warnings[t] = r.error;
        const auto fallback = fallbackFor != nullptr ? fallbackFor (r.track) : SampleRef();

        if (! r.loadedRef.isBuiltIn() && fallback.isBuiltIn())
            startJob (r.track, fallback, true);
        else
            slots.set (r.track, nullptr);
    }

    if (changed)
        sendChangeMessage();
}

bool SampleManager::waitAndApply (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    for (;;)
    {
        handleCompletedJobs();

        bool busy = false;
        for (auto p : pending)
            busy = busy || p > 0;

        if (! busy)
            return true;

        if (juce::Time::getMillisecondCounter() > deadline)
            return false;

        juce::Thread::sleep (2);
    }
}

} // namespace bf
