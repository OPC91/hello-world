#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace benchmark
{
    /** A loop in samples at the host rate; packs into one atomic word so it never tears. */
    struct LoopPoints
    {
        int64_t start = 0, end = 0;

        bool isValid() const noexcept { return end > start && start >= 0; }

        uint64_t pack() const noexcept
        {
            if (! isValid() || end > (int64_t) std::numeric_limits<uint32_t>::max())
                return 0;
            return ((uint64_t) start << 32) | (uint64_t) end;
        }

        static LoopPoints unpack (uint64_t packed) noexcept
        {
            return { (int64_t) (packed >> 32), (int64_t) (packed & 0xffffffffu) };
        }
    };

    /**
        Lock-free reference transport state shared between the UI / message
        thread and the audio thread.
    */
    struct TransportState
    {
        static constexpr int64_t noSeek = std::numeric_limits<int64_t>::min();

        // UI -> audio
        std::atomic<int64_t> seekRequest { noSeek };   // Free mode only
        std::atomic<bool> freePlaying { true };
        std::atomic<uint64_t> loop { 0 };              // packed LoopPoints of the active reference, 0 = none
        std::atomic<int64_t> syncOffset { 0 };         // reference position minus host position, Sync mode

        // audio -> UI
        std::atomic<int64_t> playhead { 0 };
        std::atomic<bool> referencePlaying { false };
        std::atomic<bool> hostPlaying { false };
        std::atomic<bool> hostPositionAvailable { false };

        void requestSeek (int64_t position) noexcept { seekRequest.store (position); }
        void setLoop (LoopPoints points) noexcept { loop.store (points.pack()); }
        void clearLoop() noexcept { loop.store (0); }
    };
}
