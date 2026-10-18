#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace benchmark
{
    /**
        Single-producer / single-consumer stereo sample FIFO. The audio thread
        pushes, a background thread pops. Wait-free on both sides; when full the
        producer drops samples and counts them, never blocks.
    */
    class StereoFifo
    {
    public:
        explicit StereoFifo (int capacityFrames)
            : fifo (capacityFrames), buffer (2, capacityFrames)
        {
            buffer.clear();
        }

        /** Producer. right may equal left for mono. Returns frames written. */
        int push (const float* left, const float* right, int numFrames) noexcept
        {
            int start1, size1, start2, size2;
            fifo.prepareToWrite (numFrames, start1, size1, start2, size2);

            copyIn (left, right, 0, start1, size1);
            copyIn (left, right, size1, start2, size2);

            const auto written = size1 + size2;
            fifo.finishedWrite (written);

            if (written < numFrames)
                dropped.fetch_add ((uint64_t) (numFrames - written), std::memory_order_relaxed);

            return written;
        }

        /** Consumer. Returns frames read into left/right (each at least maxFrames long). */
        int pop (float* left, float* right, int maxFrames) noexcept
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead (maxFrames, start1, size1, start2, size2);

            copyOut (left, right, 0, start1, size1);
            copyOut (left, right, size1, start2, size2);

            const auto read = size1 + size2;
            fifo.finishedRead (read);
            return read;
        }

        /** Consumer: discards everything currently queued. */
        void discardAll() noexcept { fifo.finishedRead (fifo.getNumReady()); }

        int getNumReady() const noexcept { return fifo.getNumReady(); }
        int getCapacity() const noexcept { return fifo.getTotalSize(); }
        uint64_t getDroppedFrames() const noexcept { return dropped.load (std::memory_order_relaxed); }

    private:
        void copyIn (const float* l, const float* r, int offset, int start, int size) noexcept
        {
            if (size <= 0)
                return;
            std::copy (l + offset, l + offset + size, buffer.getWritePointer (0, start));
            std::copy (r + offset, r + offset + size, buffer.getWritePointer (1, start));
        }

        void copyOut (float* l, float* r, int offset, int start, int size) const noexcept
        {
            if (size <= 0)
                return;
            std::copy (buffer.getReadPointer (0, start), buffer.getReadPointer (0, start) + size, l + offset);
            std::copy (buffer.getReadPointer (1, start), buffer.getReadPointer (1, start) + size, r + offset);
        }

        juce::AbstractFifo fifo;
        juce::AudioBuffer<float> buffer;
        std::atomic<uint64_t> dropped { 0 };
    };
}
