#pragma once

#include "exnilo/SpscQueue.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace exnilo
{
    /** Hands immutable objects (e.g. the melody to play) from non-realtime threads to the audio thread
        without the audio thread ever locking, allocating or freeing.

        - Any non-realtime thread calls publish(). Publishers are serialised by a mutex the audio thread never touches.
        - The audio thread calls acquire() once per block; it swaps in the newest published object and moves the
          one it replaces into a garbage queue.
        - Non-realtime code calls collectGarbage() regularly (publish() also does) to delete retired objects.

        Destroy the exchange only when the audio thread can no longer call acquire(). */
    template <typename T>
    class RealtimeExchange
    {
    public:
        RealtimeExchange() = default;
        RealtimeExchange (const RealtimeExchange&) = delete;
        RealtimeExchange& operator= (const RealtimeExchange&) = delete;

        ~RealtimeExchange()
        {
            delete pending.exchange (nullptr);
            delete active;
            collectGarbage();
        }

        /** Non-realtime threads. An object published earlier but never picked up is deleted here. */
        void publish (std::unique_ptr<T> next)
        {
            const std::lock_guard<std::mutex> guard (publisherLock);
            delete pending.exchange (next.release(), std::memory_order_acq_rel);
            collectGarbageLocked();
        }

        /** Non-realtime threads. Deletes objects the audio thread has finished with. */
        void collectGarbage()
        {
            const std::lock_guard<std::mutex> guard (publisherLock);
            collectGarbageLocked();
        }

        /** Audio thread. Returns the current object (nullptr until something is published). `changed` is set
            when a new object was swapped in by this call. Never blocks, allocates or frees. */
        T* acquire (bool& changed) noexcept
        {
            changed = false;
            if (pending.load (std::memory_order_acquire) == nullptr)
                return active;
            if (active != nullptr && garbage.full())
                return active; // try again next block once the message thread has emptied the garbage

            if (T* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                if (active != nullptr)
                    garbage.push (active);
                active = next;
                changed = true;
            }
            return active;
        }

    private:
        void collectGarbageLocked()
        {
            T* retired = nullptr;
            while (garbage.pop (retired))
                delete retired;
        }

        std::atomic<T*> pending { nullptr };
        T* active = nullptr; // owned by the audio thread
        SpscQueue<T*, 64> garbage;
        std::mutex publisherLock;
    };
}
