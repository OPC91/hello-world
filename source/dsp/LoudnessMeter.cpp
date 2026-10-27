#include "LoudnessMeter.h"

#include <algorithm>
#include <cmath>

namespace benchmark::dsp
{
    //==============================================================================
    LoudnessHistogram::LoudnessHistogram()
        : counts ((size_t) numBins, 0u), powerSums ((size_t) numBins, 0.0)
    {
    }

    void LoudnessHistogram::reset() noexcept
    {
        std::fill (counts.begin(), counts.end(), 0u);
        std::fill (powerSums.begin(), powerSums.end(), 0.0);
        totalCount = 0;
        totalPower = 0.0;
    }

    int LoudnessHistogram::binIndexForLoudness (double lufs) const noexcept
    {
        const auto index = (int) std::floor ((lufs - minLufs) / binWidth);
        return std::clamp (index, 0, numBins - 1);
    }

    void LoudnessHistogram::add (double power) noexcept
    {
        const auto lufs = LoudnessMeter::powerToLufs (power);

        if (! (lufs > minLufs)) // absolute gate (also rejects NaN / -inf)
            return;

        const auto index = (size_t) binIndexForLoudness (lufs);
        ++counts[index];
        powerSums[index] += power;
        ++totalCount;
        totalPower += power;
    }

    double LoudnessHistogram::gateLoudness (double relativeGateLU) const noexcept
    {
        return LoudnessMeter::powerToLufs (totalPower / (double) totalCount) - relativeGateLU;
    }

    double LoudnessHistogram::gatedMeanPower (double relativeGateLU) const noexcept
    {
        if (totalCount == 0)
            return 0.0;

        const auto gate = gateLoudness (relativeGateLU);
        const auto gateBin = binIndexForLoudness (gate);

        // The gate's own bin is included when its centre lies above the gate.
        const auto gateBinCentre = minLufs + ((double) gateBin + 0.5) * binWidth;
        const auto firstBin = gateBinCentre > gate ? gateBin : gateBin + 1;

        double power = 0.0;
        uint64_t count = 0;

        for (auto i = (size_t) std::max (firstBin, 0); i < (size_t) numBins; ++i)
        {
            power += powerSums[i];
            count += counts[i];
        }

        return count > 0 ? power / (double) count : 0.0;
    }

    double LoudnessHistogram::percentileAboveGate (double relativeGateLU, double percentile) const noexcept
    {
        if (totalCount == 0)
            return 0.0;

        const auto gate = gateLoudness (relativeGateLU);
        const auto firstBin = std::max (binIndexForLoudness (gate) + 1, 0);

        uint64_t count = 0;
        for (auto i = (size_t) firstBin; i < (size_t) numBins; ++i)
            count += counts[i];

        if (count == 0)
            return gate;

        // Nearest-rank on the sorted gated values.
        const auto rank = (uint64_t) std::llround (percentile * (double) (count - 1));
        uint64_t seen = 0;

        for (auto i = (size_t) firstBin; i < (size_t) numBins; ++i)
        {
            seen += counts[i];

            if (seen > rank)
                return minLufs + ((double) i + 0.5) * binWidth;
        }

        return maxLufs;
    }

    //==============================================================================
    double LoudnessMeter::powerToLufs (double power) noexcept
    {
        return power > 0.0 ? -0.691 + 10.0 * std::log10 (power)
                           : -std::numeric_limits<double>::infinity();
    }

    double LoudnessMeter::lufsToPower (double lufs) noexcept
    {
        return std::pow (10.0, (lufs + 0.691) / 10.0);
    }

    void LoudnessMeter::prepare (double newSampleRate, int newNumChannels)
    {
        sampleRate = newSampleRate;
        numChannels = std::clamp (newNumChannels, 1, maxChannels);
        subBlockLength = std::max (1, (int) std::lround (sampleRate * 0.1));

        for (auto& f : filters)
            f.prepare (sampleRate);

        reset();
    }

    void LoudnessMeter::reset() noexcept
    {
        for (auto& f : filters)
            f.reset();

        subBlockEnergy = 0.0;
        samplesInSubBlock = 0;
        recentSubBlocks.fill (0.0);
        ringPosition = 0;
        subBlocksSeen = 0;

        integratedHistogram.reset();
        rangeHistogram.reset();

        momentary = shortTerm = integrated = minusInfinity;
        maxMomentary = maxShortTerm = minusInfinity;
        loudnessRange = 0.0f;
        ++updateCounter;
    }

    void LoudnessMeter::process (const float* const* channels, int channelCount, int numSamples) noexcept
    {
        const auto chans = std::min ({ channelCount, numChannels, maxChannels });
        int i = 0;

        while (i < numSamples)
        {
            const auto todo = std::min (numSamples - i, subBlockLength - samplesInSubBlock);

            for (int ch = 0; ch < chans; ++ch)
            {
                auto& filter = filters[(size_t) ch];
                const auto* in = channels[ch] + i;
                double energy = 0.0;

                for (int n = 0; n < todo; ++n)
                {
                    const auto y = filter.process ((double) in[n]);
                    energy += y * y;
                }

                subBlockEnergy += energy;
            }

            samplesInSubBlock += todo;
            i += todo;

            if (samplesInSubBlock == subBlockLength)
                finishSubBlock();
        }
    }

    double LoudnessMeter::sumRecentSubBlocks (int count) const noexcept
    {
        double sum = 0.0;
        for (int k = 0; k < count; ++k)
        {
            const auto index = (ringPosition - 1 - k + subBlocksPerShortTerm) % subBlocksPerShortTerm;
            sum += recentSubBlocks[(size_t) index];
        }
        return sum;
    }

    void LoudnessMeter::finishSubBlock() noexcept
    {
        recentSubBlocks[(size_t) ringPosition] = subBlockEnergy;
        ringPosition = (ringPosition + 1) % subBlocksPerShortTerm;
        ++subBlocksSeen;

        subBlockEnergy = 0.0;
        samplesInSubBlock = 0;

        const auto momentaryPower = sumRecentSubBlocks (subBlocksPerMomentary) / (subBlockLength * subBlocksPerMomentary);
        const auto shortTermPower = sumRecentSubBlocks (subBlocksPerShortTerm) / (subBlockLength * subBlocksPerShortTerm);

        momentary = (float) powerToLufs (momentaryPower);
        shortTerm = (float) powerToLufs (shortTermPower);

        // Gating blocks (400 ms, 75 % overlap) only once a full block exists.
        if (subBlocksSeen >= (uint64_t) subBlocksPerMomentary)
        {
            maxMomentary = std::max (maxMomentary, momentary);
            integratedHistogram.add (momentaryPower);

            const auto gated = integratedHistogram.gatedMeanPower (10.0);
            integrated = gated > 0.0 ? (float) powerToLufs (gated) : minusInfinity;
        }

        // Loudness range uses complete 3 s short-term values (EBU Tech 3342).
        if (subBlocksSeen >= (uint64_t) subBlocksPerShortTerm)
        {
            maxShortTerm = std::max (maxShortTerm, shortTerm);
            rangeHistogram.add (shortTermPower);

            if (rangeHistogram.getCount() > 0)
                loudnessRange = (float) (rangeHistogram.percentileAboveGate (20.0, 0.95)
                                         - rangeHistogram.percentileAboveGate (20.0, 0.10));
        }

        ++updateCounter;
    }

    double LoudnessMeter::getElapsedSeconds() const noexcept
    {
        return ((double) subBlocksSeen * subBlockLength + samplesInSubBlock) / sampleRate;
    }
}
