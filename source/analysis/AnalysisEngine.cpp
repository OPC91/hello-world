#include "AnalysisEngine.h"

namespace benchmark::analysis
{
    AnalysisEngine::AnalysisEngine()
        : juce::Thread ("Benchmark analysis")
    {
        scratchL.resize (4096);
        scratchR.resize (4096);
    }

    AnalysisEngine::~AnalysisEngine()
    {
        stopThread();
    }

    void AnalysisEngine::startThread()
    {
        juce::Thread::startThread (juce::Thread::Priority::low);
    }

    void AnalysisEngine::stopThread()
    {
        signalThreadShouldExit();
        notify();
        juce::Thread::stopThread (2000);
    }

    void AnalysisEngine::requestReset (bool resetMix, bool resetReference) noexcept
    {
        if (resetMix)
            mix.resetRequested.store (true);
        if (resetReference)
            reference.resetRequested.store (true);
    }

    void AnalysisEngine::run()
    {
        while (! threadShouldExit())
        {
            processPending();
            wait (intervalMs);
        }
    }

    void AnalysisEngine::configure (double sampleRate)
    {
        currentRate = sampleRate;

        for (auto* s : { &mix, &reference })
        {
            s->spectral.prepare (sampleRate);
            s->dynamics.prepare (sampleRate);
            s->fifo.discardAll(); // queued audio was at the old rate
            s->resetRequested.store (false);
        }
    }

    bool AnalysisEngine::drain (Source& source)
    {
        if (source.resetRequested.exchange (false))
        {
            source.spectral.reset();
            source.dynamics.reset();
        }

        bool any = false;

        for (;;)
        {
            const auto n = source.fifo.pop (scratchL.data(), scratchR.data(), (int) scratchL.size());
            if (n == 0)
                break;

            source.spectral.process (scratchL.data(), scratchR.data(), n);
            source.dynamics.process (scratchL.data(), scratchR.data(), n);
            any = true;
        }

        return any;
    }

    bool AnalysisEngine::processPending()
    {
        const std::lock_guard<std::mutex> lock (processingMutex);

        const auto rate = requestedRate.load();
        if (rate <= 0.0)
            return false;

        auto changed = false;

        if (std::abs (rate - currentRate) > 1e-6)
        {
            configure (rate);
            changed = true;
        }

        const auto resetPending = mix.resetRequested.load() || reference.resetRequested.load();
        changed |= drain (mix);
        changed |= drain (reference);
        changed |= resetPending;

        if (changed)
            publish();

        return changed;
    }

    void AnalysisEngine::publish()
    {
        working.sampleRate = currentRate;

        const auto fillSource = [] (Source& s, SourceAnalysis& out)
        {
            out.secondsAnalysed = s.spectral.getSecondsAnalysed();
            s.spectral.fillViews (out.average, out.live);
            s.dynamics.fill (out.dynamics);
        };

        fillSource (mix, working.mix);
        fillSource (reference, working.reference);
        working.version = version.load() + 1;

        {
            const std::lock_guard<std::mutex> lock (snapshotMutex);
            published = working;
        }

        version.store (working.version);
    }

    void AnalysisEngine::getSnapshot (AnalysisSnapshot& destination) const
    {
        const std::lock_guard<std::mutex> lock (snapshotMutex);
        destination = published;
    }
}
