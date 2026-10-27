#include <catch2/catch_test_macros.hpp>

#include "util/RealtimeHandoff.h"

#include <juce_core/juce_core.h>

#include <thread>

using benchmark::RealtimeHandoff;

namespace
{
    struct Payload
    {
        static constexpr uint64_t aliveMagic = 0x5eedf00dcafebeefull;

        explicit Payload (int v) : value (v) {}
        ~Payload() { magic = 0; }

        int value;
        volatile uint64_t magic = aliveMagic;
    };
}

TEST_CASE ("Handoff publishes and acquires objects", "[handoff]")
{
    RealtimeHandoff<Payload> handoff;

    CHECK (handoff.acquire() == nullptr);
    handoff.release();

    handoff.publish (std::make_shared<Payload> (1));
    const auto* p = handoff.acquire();
    REQUIRE (p != nullptr);
    CHECK (p->value == 1);
    handoff.release();

    handoff.publish (nullptr);
    CHECK (handoff.acquire() == nullptr);
    handoff.release();
}

TEST_CASE ("Objects in use by the audio thread are kept alive", "[handoff]")
{
    RealtimeHandoff<Payload> handoff;
    auto first = std::make_shared<Payload> (1);
    std::weak_ptr<Payload> watch = first;

    handoff.publish (std::move (first));
    const auto* inUse = handoff.acquire();

    handoff.publish (std::make_shared<Payload> (2));
    handoff.collectGarbage();

    CHECK_FALSE (watch.expired());        // still referenced by the audio thread
    CHECK (inUse->value == 1);
    CHECK (handoff.getNumRetired() == 1);

    handoff.release();
    handoff.collectGarbage();

    CHECK (watch.expired());
    CHECK (handoff.getNumRetired() == 0);
}

TEST_CASE ("Handoff survives concurrent publishing", "[handoff][stress]")
{
    RealtimeHandoff<Payload> handoff;
    handoff.publish (std::make_shared<Payload> (0));

    std::atomic<bool> stop { false };
    std::atomic<int> failures { 0 };
    std::atomic<int64_t> reads { 0 };

    std::thread audio ([&]
    {
        while (! stop.load())
        {
            if (const auto* p = handoff.acquire())
            {
                // Touch the object for a while, as a processBlock would.
                for (int i = 0; i < 50; ++i)
                    if (p->magic != Payload::aliveMagic)
                        failures.fetch_add (1);
            }

            handoff.release();
            reads.fetch_add (1);
        }
    });

    // Make sure the two threads really overlap, however the scheduler behaves.
    while (reads.load() == 0)
        std::this_thread::yield();

    for (int i = 1; i <= 20000 || reads.load() < 20000; ++i)
    {
        handoff.publish (std::make_shared<Payload> (i));

        if ((i % 16) == 0)
            handoff.collectGarbage();
    }

    stop = true;
    audio.join();
    handoff.collectGarbage();

    CHECK (failures.load() == 0);
    CHECK (reads.load() > 0);
    CHECK (handoff.getNumRetired() == 0);
}
