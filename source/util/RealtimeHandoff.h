#pragma once

#include <atomic>
#include <memory>
#include <vector>

namespace benchmark
{
    /**
        Publishes an immutable object from the message thread to the audio thread
        without locks, allocations or deallocations on the audio thread.

        The audio thread protects the object it is using with a single hazard
        pointer. The message thread keeps every replaced object alive until the
        audio thread is no longer using it, then frees it in collectGarbage().

        Exactly one audio thread may call acquire()/release(), and exactly one
        other thread (normally the message thread) may call publish()/collectGarbage().
    */
    template <typename T>
    class RealtimeHandoff
    {
    public:
        RealtimeHandoff() = default;
        ~RealtimeHandoff() = default;

        RealtimeHandoff (const RealtimeHandoff&) = delete;
        RealtimeHandoff& operator= (const RealtimeHandoff&) = delete;

        //==============================================================================
        // Message thread

        void publish (std::shared_ptr<const T> next)
        {
            if (next == current)
                return;

            if (current != nullptr)
                retired.push_back (std::move (current));

            current = std::move (next);
            active.store (current.get(), std::memory_order_seq_cst);
            collectGarbage();
        }

        /** Frees replaced objects that the audio thread is no longer using. */
        void collectGarbage()
        {
            const auto* inUse = hazard.load (std::memory_order_seq_cst);

            std::erase_if (retired, [inUse] (const std::shared_ptr<const T>& p) { return p.get() != inUse; });
        }

        const std::shared_ptr<const T>& getPublished() const noexcept { return current; }
        size_t getNumRetired() const noexcept { return retired.size(); }

        //==============================================================================
        // Audio thread

        /** Returns the current object (or nullptr). It stays valid until release(). */
        const T* acquire() noexcept
        {
            auto* p = active.load (std::memory_order_seq_cst);

            for (;;)
            {
                hazard.store (p, std::memory_order_seq_cst);
                auto* check = active.load (std::memory_order_seq_cst);

                if (check == p)
                    return p;

                p = check;
            }
        }

        void release() noexcept
        {
            hazard.store (nullptr, std::memory_order_seq_cst);
        }

    private:
        std::shared_ptr<const T> current;
        std::vector<std::shared_ptr<const T>> retired;

        std::atomic<const T*> active { nullptr };
        std::atomic<const T*> hazard { nullptr };
    };

    /** RAII helper for the audio thread. */
    template <typename T>
    class ScopedHandoffAccess
    {
    public:
        explicit ScopedHandoffAccess (RealtimeHandoff<T>& h) noexcept : handoff (h), object (h.acquire()) {}
        ~ScopedHandoffAccess() { handoff.release(); }

        ScopedHandoffAccess (const ScopedHandoffAccess&) = delete;
        ScopedHandoffAccess& operator= (const ScopedHandoffAccess&) = delete;

        const T* get() const noexcept { return object; }

    private:
        RealtimeHandoff<T>& handoff;
        const T* object;
    };
}
