#pragma once

#include "AnalysisSnapshot.h"
#include "DynamicsAnalyser.h"
#include "SpectralAnalyser.h"
#include "util/AudioFifo.h"

#include <juce_core/juce_core.h>

#include <mutex>

namespace benchmark::analysis
{
    /**
        Runs the comparison analysis on its own thread.

        The audio thread pushes mix and reference samples into lock-free FIFOs
        (pushMix / pushReference are wait-free). The analysis thread drains them
        about 30 times a second, updates the analysers and publishes a snapshot
        that the UI copies with getSnapshot().
    */
    class AnalysisEngine final : private juce::Thread
    {
    public:
        static constexpr int fifoCapacity = 1 << 18; // > 1 s at 192 kHz
        static constexpr int intervalMs = 33;

        AnalysisEngine();
        ~AnalysisEngine() override;

        void startThread();
        void stopThread();

        /** Any thread. Takes effect on the analysis thread (which resets). */
        void setSampleRate (double sampleRate) noexcept { requestedRate.store (sampleRate); }

        // Audio thread (wait-free). right may equal left for mono.
        void pushMix (const float* left, const float* right, int numFrames) noexcept      { mix.fifo.push (left, right, numFrames); }
        void pushReference (const float* left, const float* right, int numFrames) noexcept { reference.fifo.push (left, right, numFrames); }

        /** Any thread. */
        void requestReset (bool resetMix, bool resetReference) noexcept;

        /** UI thread: copies the latest results. */
        void getSnapshot (AnalysisSnapshot& destination) const;
        uint32_t getVersion() const noexcept { return version.load(); }

        /** Drains the FIFOs and publishes results now. Called by the thread; tests may call it directly. */
        bool processPending();

    private:
        struct Source
        {
            StereoFifo fifo { fifoCapacity };
            SpectralAnalyser spectral;
            DynamicsAnalyser dynamics;
            std::atomic<bool> resetRequested { false };
        };

        void run() override;
        void configure (double sampleRate);
        bool drain (Source& source);
        void publish();

        Source mix, reference;
        std::atomic<double> requestedRate { 0.0 };
        double currentRate = 0.0;
        std::vector<float> scratchL, scratchR;

        std::mutex processingMutex;            // one consumer at a time
        mutable std::mutex snapshotMutex;
        AnalysisSnapshot working, published;
        std::atomic<uint32_t> version { 0 };
    };
}
