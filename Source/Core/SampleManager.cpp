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
        for (int v = 0; v < kSampleVariants; ++v)
            if (! refs[slot (t, v)].isEmpty())
                startJob (t, v, refs[slot (t, v)], false);
}

void SampleManager::load (int track, const SampleRef& ref, int variant)
{
    if (track < 0 || track >= kNumTracks || variant < 0 || variant >= kSampleVariants)
        return;

    const auto i = slot (track, variant);
    refs[i] = ref;
    warnings[i].clear();

    if (ref.isEmpty())
    {
        ++generation[i];
        slots.set (track, nullptr, variant);
        sendChangeMessage();
        return;
    }

    if (sampleRate > 0.0)
        startJob (track, variant, ref, false);

    sendChangeMessage();
}

void SampleManager::startJob (int track, int variant, const SampleRef& ref, bool isFallback)
{
    // A newer request supersedes anything still in flight for this slot, except that a
    // fallback keeps the generation of the request it is standing in for.
    const auto i = slot (track, variant);
    const auto gen = isFallback ? generation[i] : ++generation[i];
    const double rate = sampleRate;
    ++pending[i];

    pool.addJob ([this, track, variant, gen, ref, rate] {
        auto result = runJob (track, variant, gen, ref, rate);
        const juce::ScopedLock sl (resultLock);
        results.push_back (std::move (result));
    });
}

SampleManager::Result SampleManager::runJob (int track, int variant, std::uint32_t gen, SampleRef ref, double rate) const
{
    Result r;
    r.track = track;
    r.variant = variant;
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
        const auto i = slot (r.track, r.variant);
        pending[i] = juce::jmax (0, pending[i] - 1);
        changed = true;

        if (r.generation != generation[i])
            continue; // superseded by a newer request

        if (r.data != nullptr)
        {
            slots.set (r.track, r.data, r.variant);
            continue;
        }

        // Failed: warn. A track falls back to a built-in sound so it still plays; an empty per-hit
        // slot makes its hits use the track's sample instead.
        warnings[i] = r.error;
        const auto fallback = r.variant == 0 && fallbackFor != nullptr ? fallbackFor (r.track) : SampleRef();

        if (! r.loadedRef.isBuiltIn() && fallback.isBuiltIn())
            startJob (r.track, 0, fallback, true);
        else
            slots.set (r.track, nullptr, r.variant);
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
