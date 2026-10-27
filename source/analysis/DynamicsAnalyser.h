#pragma once

#include "AnalysisSnapshot.h"
#include "dsp/LoudnessMeter.h"

namespace benchmark::analysis
{
    /**
        Dynamics of one stereo source since the last reset: sample peak, RMS of
        non-silent audio, crest factor, integrated loudness, PLR and loudness
        range, plus a 60 s short-term loudness history. Analysis thread only.
    */
    class DynamicsAnalyser
    {
    public:
        static constexpr int gateBlock = 1024;

        void prepare (double sampleRate);
        void reset() noexcept;
        void process (const float* left, const float* right, int numFrames) noexcept;
        void fill (DynamicsView& view) const;

    private:
        dsp::LoudnessMeter meter;
        uint32_t lastMeterUpdate = 0;

        double sumSquares = 0.0;
        uint64_t counted = 0;
        float peak = 0.0f;

        std::array<float, historyLength> history {};
        int historyWrite = 0, historyCount = 0;
    };
}
