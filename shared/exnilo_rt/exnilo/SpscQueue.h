#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace exnilo
{
   #if defined (_MSC_VER)
    #pragma warning (push)
    #pragma warning (disable : 4324) // padding from alignas: intended, keeps head and tail on separate cache lines
   #endif

    /** Bounded single-producer / single-consumer queue. Lock-free and allocation-free after construction,
        so it is safe to push or pop on the audio thread. Holds Capacity - 1 items (Capacity must be a power of 2). */
    template <typename T, std::size_t Capacity>
    class SpscQueue
    {
        static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

    public:
        /** Producer thread only. Returns false (and drops nothing) when the queue is full. */
        bool push (const T& value) noexcept
        {
            const auto tail = tailIndex.load (std::memory_order_relaxed);
            const auto next = (tail + 1) & mask;
            if (next == headIndex.load (std::memory_order_acquire))
                return false;
            buffer[tail] = value;
            tailIndex.store (next, std::memory_order_release);
            return true;
        }

        /** Consumer thread only. Returns false when the queue is empty. */
        bool pop (T& value) noexcept
        {
            const auto head = headIndex.load (std::memory_order_relaxed);
            if (head == tailIndex.load (std::memory_order_acquire))
                return false;
            value = buffer[head];
            headIndex.store ((head + 1) & mask, std::memory_order_release);
            return true;
        }

        /** Exact when called from the producer thread (the consumer can only make room). */
        bool full() const noexcept
        {
            return ((tailIndex.load (std::memory_order_relaxed) + 1) & mask) == headIndex.load (std::memory_order_acquire);
        }

        /** Exact when called from the consumer thread. */
        bool empty() const noexcept
        {
            return headIndex.load (std::memory_order_relaxed) == tailIndex.load (std::memory_order_acquire);
        }

        static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

    private:
        static constexpr std::size_t mask = Capacity - 1;
        std::array<T, Capacity> buffer {};
        alignas (64) std::atomic<std::size_t> headIndex { 0 };
        alignas (64) std::atomic<std::size_t> tailIndex { 0 };
    };

   #if defined (_MSC_VER)
    #pragma warning (pop)
   #endif
}
