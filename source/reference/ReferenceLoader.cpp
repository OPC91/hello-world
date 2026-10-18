#include "ReferenceLoader.h"

namespace benchmark
{
    ReferenceLoader::ReferenceLoader()
        : pool (juce::ThreadPoolOptions {}
                    .withThreadName ("Benchmark reference loader")
                    .withNumberOfThreads (2)
                    .withDesiredThreadPriority (juce::Thread::Priority::background))
    {
    }

    ReferenceLoader::~ReferenceLoader()
    {
        alive->store (false);

        for (int i = 0; i < pool.getNumJobs(); ++i)
            if (auto* job = pool.getJob (i))
                job->signalJobShouldExit();

        pool.removeAllJobs (true, 10000);
    }

    std::shared_ptr<ReferenceLoader::Ticket> ReferenceLoader::load (const juce::File& file, double targetSampleRate, Completion onComplete)
    {
        auto ticket = std::make_shared<Ticket>();

        pool.addJob ([file, targetSampleRate, ticket, onComplete = std::move (onComplete), aliveFlag = alive]() mutable
        {
            auto result = ReferenceDecoder::decode (file, targetSampleRate, ticket->cancelled, &ticket->progress);

            if (result.cancelled || ticket->cancelled.load())
                return;

            juce::MessageManager::callAsync ([ticket, aliveFlag, onComplete = std::move (onComplete), result = std::move (result)]() mutable
            {
                if (aliveFlag->load() && ! ticket->cancelled.load())
                    onComplete (ticket, std::move (result));
            });
        });

        return ticket;
    }

    bool ReferenceLoader::waitForAll (int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (pool.getNumJobs() > 0)
        {
            if (juce::Time::getMillisecondCounter() > end)
                return false;

            juce::Thread::sleep (5);
        }

        return true;
    }
}
