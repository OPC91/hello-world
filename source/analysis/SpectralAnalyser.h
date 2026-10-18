#pragma once

#include "AnalysisSnapshot.h"

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

namespace benchmark::analysis
{
    /**
        FFT analysis of one stereo source for tonal balance and stereo width.

        Hann-windowed frames with 50 % overlap; the FFT size scales with the
        sample rate so bins stay ~6 Hz wide. Two accumulations run side by side:
        "average" (every non-silent frame since reset) and "live" (exponential,
        ~1.5 s time constant). Silent frames are skipped so pauses don't dilute
        the average.

        Tonal values are band powers in dB relative to the source's own
        K-weighted power, so two sources compare at matched loudness.
        Runs on the analysis thread (allocates in prepare() only).
    */
    class SpectralAnalyser
    {
    public:
        static constexpr double liveTimeConstantSeconds = 1.5;
        static constexpr double silenceThreshold = 1.0e-7; // mean power, about -70 dBFS

        static int fftOrderFor (double sampleRate) noexcept;

        void prepare (double sampleRate);
        void reset() noexcept;
        void process (const float* left, const float* right, int numFrames) noexcept;

        void fillViews (SpectralView& average, SpectralView& live) const;

        double getSecondsAnalysed() const noexcept { return (double) averageFrames * hopSize / sampleRate; }
        int getFftSize() const noexcept { return fftSize; }

    private:
        struct BinRange
        {
            int first = 0, last = 0;   // inclusive
            float bandwidthHz = 0.0f;
        };

        struct Accumulator
        {
            std::vector<double> power;                       // per bin, (|L|^2 + |R|^2) / 2
            std::array<double, numStereoBands> left {}, right {}, cross {};
        };

        void analyseFrame() noexcept;
        BinRange makeRange (float low, float high) const;
        float rangeDb (const std::vector<double>& power, const BinRange& range, double offsetDb) const noexcept;
        void fillView (SpectralView& view, const Accumulator& acc, bool valid) const;

        double sampleRate = 48000.0;
        int fftOrder = 13, fftSize = 8192, hopSize = 4096, numBins = 4097;
        float binWidth = 48000.0f / 8192.0f;
        double liveAlpha = 0.1;

        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, pendingL, pendingR, fftL, fftR;
        int pendingCount = 0;

        std::vector<float> kWeight;                         // |K(f)|^2 per bin
        std::vector<int> stereoBandOfBin;                   // -1 outside 20 Hz - 20 kHz
        std::array<BinRange, numCurvePoints> curveRanges;
        std::array<BinRange, numThirdOctaveBands> bandRanges;
        std::array<BinRange, numTonalRanges> tonalRangeBins;

        Accumulator average, live;
        uint64_t averageFrames = 0;
        bool liveValid = false;
    };
}
