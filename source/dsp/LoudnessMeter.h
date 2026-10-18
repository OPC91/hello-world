#pragma once

#include "KWeighting.h"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace benchmark::dsp
{
    /**
        Fixed-memory histogram of gating-block powers, used for gated integrated
        loudness and loudness range without storing every block.
        Resolution 0.01 LU over -70 ... +10 LUFS; each bin also keeps its exact
        power sum so the gated mean is not quantised.
    */
    class LoudnessHistogram
    {
    public:
        LoudnessHistogram();

        void reset() noexcept;
        void add (double power) noexcept; // power = sum of channel mean squares

        uint64_t getCount() const noexcept { return totalCount; }

        /** Mean power of all blocks above the relative gate (gateLU below the abs-gated mean). */
        double gatedMeanPower (double relativeGateLU) const noexcept;

        /** Loudness value at the given percentile (0..1) of blocks above the relative gate. */
        double percentileAboveGate (double relativeGateLU, double percentile) const noexcept;

    private:
        static constexpr double minLufs = -70.0;
        static constexpr double maxLufs = 10.0;
        static constexpr double binWidth = 0.01;
        static constexpr int numBins = 8000;

        int binIndexForLoudness (double lufs) const noexcept;
        double gateLoudness (double relativeGateLU) const noexcept;

        std::vector<uint32_t> counts;
        std::vector<double> powerSums;
        uint64_t totalCount = 0;
        double totalPower = 0.0;
    };

    /**
        ITU-R BS.1770-4 / EBU R128 loudness meter for mono or stereo signals.

        Momentary (400 ms), short-term (3 s), gated integrated loudness and
        loudness range (EBU Tech 3342). Values are updated every 100 ms.

        prepare() allocates; reset() and process() are real-time safe.
    */
    class LoudnessMeter
    {
    public:
        static constexpr float minusInfinity = -std::numeric_limits<float>::infinity();
        static constexpr int maxChannels = 2;

        static double powerToLufs (double power) noexcept;
        static double lufsToPower (double lufs) noexcept;

        void prepare (double sampleRate, int numChannels);
        void reset() noexcept;

        /** Feeds samples. numChannels must equal the prepared channel count. */
        void process (const float* const* channels, int numChannels, int numSamples) noexcept;

        float getMomentary() const noexcept { return momentary; }
        float getShortTerm() const noexcept { return shortTerm; }
        float getIntegrated() const noexcept { return integrated; }
        float getLoudnessRange() const noexcept { return loudnessRange; }
        float getMaxMomentary() const noexcept { return maxMomentary; }
        float getMaxShortTerm() const noexcept { return maxShortTerm; }

        /** Seconds of signal fed since the last reset. */
        double getElapsedSeconds() const noexcept;

        /** Increments every time the readings are updated (every 100 ms). */
        uint32_t getUpdateCounter() const noexcept { return updateCounter; }

    private:
        void finishSubBlock() noexcept;
        double sumRecentSubBlocks (int count) const noexcept;

        static constexpr int subBlocksPerMomentary = 4;
        static constexpr int subBlocksPerShortTerm = 30;

        double sampleRate = 48000.0;
        int numChannels = 2;
        int subBlockLength = 4800;

        std::array<KWeightingFilter, maxChannels> filters;

        double subBlockEnergy = 0.0;
        int samplesInSubBlock = 0;
        std::array<double, subBlocksPerShortTerm> recentSubBlocks {};
        int ringPosition = 0;
        uint64_t subBlocksSeen = 0;

        LoudnessHistogram integratedHistogram, rangeHistogram;

        float momentary = minusInfinity, shortTerm = minusInfinity, integrated = minusInfinity;
        float loudnessRange = 0.0f, maxMomentary = minusInfinity, maxShortTerm = minusInfinity;
        uint32_t updateCounter = 0;
    };
}
