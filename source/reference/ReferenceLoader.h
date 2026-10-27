#pragma once

#include "ReferenceDecoder.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace benchmark
{
    /**
        Runs ReferenceDecoder jobs on a small background thread pool and reports
        results back on the message thread. Each job has a ticket for progress
        display and cancellation.
    */
    class ReferenceLoader
    {
    public:
        struct Ticket
        {
            std::atomic<bool> cancelled { false };
            std::atomic<float> progress { 0.0f };
        };

        using Completion = std::function<void (const std::shared_ptr<Ticket>&, ReferenceDecoder::Result)>;

        ReferenceLoader();
        ~ReferenceLoader();

        /** Starts a job. onComplete runs on the message thread unless the job was cancelled. */
        std::shared_ptr<Ticket> load (const juce::File& file, double targetSampleRate, Completion onComplete);

        /** Blocks until all queued jobs have finished (used by tests). */
        bool waitForAll (int timeoutMs);

    private:
        juce::ThreadPool pool;
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

        JUCE_DECLARE_NON_COPYABLE (ReferenceLoader)
    };
}
