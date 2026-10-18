#include "DynamicsAnalyser.h"

#include <algorithm>
#include <cmath>

namespace benchmark::analysis
{
    void DynamicsAnalyser::prepare (double sampleRate)
    {
        meter.prepare (sampleRate, 2);
        reset();
    }

    void DynamicsAnalyser::reset() noexcept
    {
        meter.reset();
        lastMeterUpdate = meter.getUpdateCounter();
        sumSquares = 0.0;
        counted = 0;
        peak = 0.0f;
        history.fill (noValue);
        historyWrite = historyCount = 0;
    }

    void DynamicsAnalyser::process (const float* left, const float* right, int numFrames) noexcept
    {
        for (int start = 0; start < numFrames; start += gateBlock)
        {
            const auto n = std::min (gateBlock, numFrames - start);
            const float* ptrs[2] = { left + start, right + start };
            meter.process (ptrs, 2, n);

            double blockSquares = 0.0;
            float blockPeak = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const auto l = left[start + i], r = right[start + i];
                blockSquares += (double) l * l + (double) r * r;
                blockPeak = std::max ({ blockPeak, std::abs (l), std::abs (r) });
            }

            // Silence (stopped transport, paused reference) is left out.
            if (blockSquares / (2.0 * n) >= 1.0e-7)
            {
                sumSquares += blockSquares;
                counted += (uint64_t) n * 2;
                peak = std::max (peak, blockPeak);
            }

            // One history value per meter update (every 100 ms).
            if (meter.getUpdateCounter() != lastMeterUpdate)
            {
                lastMeterUpdate = meter.getUpdateCounter();
                history[(size_t) historyWrite] = meter.getShortTerm();
                historyWrite = (historyWrite + 1) % historyLength;
                historyCount = std::min (historyCount + 1, historyLength);
            }
        }
    }

    void DynamicsAnalyser::fill (DynamicsView& view) const
    {
        view = DynamicsView();

        const auto toDb = [] (double gain) { return gain > 0.0 ? (float) (20.0 * std::log10 (gain)) : noValue; };

        if (counted > 0)
        {
            view.peakDb = toDb (peak);
            view.rmsDb = toDb (std::sqrt (sumSquares / (double) counted));
            view.crestDb = view.peakDb - view.rmsDb;
        }

        view.integratedLufs = meter.getIntegrated();
        view.shortTerm = meter.getShortTerm();
        view.loudnessRange = meter.getLoudnessRange();

        if (std::isfinite (view.peakDb) && std::isfinite (view.integratedLufs))
            view.plr = view.peakDb - view.integratedLufs;

        view.historyCount = historyCount;
        for (int i = 0; i < historyCount; ++i)
        {
            const auto index = (historyWrite - historyCount + i + historyLength) % historyLength;
            view.history[(size_t) i] = history[(size_t) index];
        }
    }
}
