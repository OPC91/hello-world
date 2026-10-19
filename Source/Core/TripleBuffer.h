#pragma once

#include <array>
#include <atomic>
#include <memory>

namespace bf
{

/** Lock-free single-writer / single-reader triple buffer.

    The writer (message thread) fills writeBuffer() with a complete new value and calls publish().
    The reader (audio thread) calls read() and always gets the most recently published complete
    value, without ever blocking, allocating or seeing a half-written object. */
template <typename T>
class TripleBuffer
{
public:
    TripleBuffer()
    {
        for (auto& b : buffers)
            b = std::make_unique<T>();
    }

    explicit TripleBuffer (const T& initial) : TripleBuffer()
    {
        for (auto& b : buffers)
            *b = initial;
    }

    // Writer side ------------------------------------------------------------
    T& writeBuffer() noexcept { return *buffers[(size_t) back]; }

    void publish() noexcept
    {
        back = middle.exchange (back | dirtyBit, std::memory_order_acq_rel) & indexMask;
    }

    void publish (const T& value)
    {
        writeBuffer() = value;
        publish();
    }

    // Reader side ------------------------------------------------------------
    const T& read() noexcept
    {
        if (middle.load (std::memory_order_relaxed) & dirtyBit)
            front = middle.exchange (front, std::memory_order_acq_rel) & indexMask;

        return *buffers[(size_t) front];
    }

    /** True if a newer value has been published since the last read(). */
    bool hasNewData() const noexcept { return (middle.load (std::memory_order_relaxed) & dirtyBit) != 0; }

private:
    static constexpr int dirtyBit = 4;
    static constexpr int indexMask = 3;

    std::array<std::unique_ptr<T>, 3> buffers;
    std::atomic<int> middle { 1 };
    int front = 0; // reader-owned
    int back = 2;  // writer-owned
};

} // namespace bf
