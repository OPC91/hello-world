#pragma once

#include "AnalysisBands.h"

#include <array>
#include <cstdint>
#include <limits>

namespace benchmark::analysis
{
    inline constexpr float noValue = -std::numeric_limits<float>::infinity();

    struct StereoBand
    {
        float sideToMidDb = noValue;  // side energy relative to mid energy
        float width = 0.0f;           // side / (mid + side) energy, 0 = mono, 0.5 = uncorrelated, 1 = out of phase
        float correlation = 1.0f;     // -1 .. +1
    };

    /** Tonal and stereo view over one averaging window. */
    struct SpectralView
    {
        bool valid = false;
        std::array<float, numCurvePoints> curve;          // 1/3-octave smoothed, dB relative to own loudness
        std::array<float, numThirdOctaveBands> bands;     // dB relative to own loudness
        std::array<float, numTonalRanges> ranges;         // dB relative to own loudness
        std::array<StereoBand, numStereoBands> stereo;

        SpectralView() { curve.fill (noValue); bands.fill (noValue); ranges.fill (noValue); }
    };

    struct DynamicsView
    {
        float peakDb = noValue;          // sample peak, dBFS
        float rmsDb = noValue;           // over non-silent audio
        float crestDb = noValue;         // peak - RMS
        float integratedLufs = noValue;
        float plr = noValue;             // peak-to-loudness ratio: peak dBFS - integrated LUFS
        float loudnessRange = 0.0f;      // LU (EBU Tech 3342)
        float shortTerm = noValue;

        std::array<float, historyLength> history;   // short-term LUFS, oldest first
        int historyCount = 0;

        DynamicsView() { history.fill (noValue); }
    };

    struct SourceAnalysis
    {
        double secondsAnalysed = 0.0;   // non-silent audio included in the average
        SpectralView average, live;
        DynamicsView dynamics;
    };

    struct AnalysisSnapshot
    {
        SourceAnalysis mix, reference;
        double sampleRate = 0.0;
        uint32_t version = 0;
    };
}
