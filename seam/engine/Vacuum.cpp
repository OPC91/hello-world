#include "Vacuum.h"

#include <algorithm>
#include <cmath>

namespace seam::engine
{
    void Vacuum::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        const auto size = static_cast<size_t> (4.0 * sampleRate);   // a beat at 15 BPM: plenty
        ringL.assign (size, 0.0f);
        ringR.assign (size, 0.0f);
        fadeOutSamples = std::max (16, static_cast<int> (0.0015 * sampleRate));
        fadeInSamples = std::max (4, static_cast<int> (0.0003 * sampleRate));
        reset();
    }

    void Vacuum::reset() noexcept
    {
        std::fill (ringL.begin(), ringL.end(), 0.0f);
        std::fill (ringR.begin(), ringR.end(), 0.0f);
        writeIndex = 0;
        readPosition = 0.0;
        inTape = false;
        gain = 1.0f;
        fadeInLeft = 0;
    }

    double Vacuum::gapStart (double ppq, const PhrasePosition& pos, double gapQuarters) noexcept
    {
        const double phraseLength = pos.phraseBars * pos.quartersPerBar;
        const double seam = PhraseTracker::gridFloor (ppq / phraseLength) * phraseLength + phraseLength;
        return seam - gapQuarters;
    }

    void Vacuum::process (float* left, float* right, int n, const double* ppq, const float* effective, const PhrasePosition& pos,
                          double gapQuarters, bool tapeStop) noexcept
    {
        if (ringL.empty() || ! pos.valid)
            return;

        const auto size = ringL.size();
        const double spq = pos.samplesPerQuarter;
        for (int i = 0; i < n; ++i)
        {
            ringL[writeIndex] = left[i];
            ringR[writeIndex] = right[i];

            const double gap = gapStart (ppq[i], pos, gapQuarters);
            const double tape = gap - pos.quartersPerBeat;
            const bool armed = effective[i] > 0.0f;

            float target = 1.0f;
            bool tapeNow = false;
            if (armed && ppq[i] >= gap)
                target = 0.0f;
            else if (armed && tapeStop && ppq[i] >= tape)
                tapeNow = true;

            // Tape stop: read behind the write head at a speed falling from 1 to 0 over the beat.
            if (tapeNow)
            {
                if (! inTape)
                {
                    inTape = true;
                    readPosition = static_cast<double> (writeIndex);
                }
                const double progress = std::clamp ((ppq[i] - tape) / (gap - tape), 0.0, 1.0);
                const auto a = static_cast<size_t> (readPosition) % size;
                const auto b = (a + 1) % size;
                const auto frac = static_cast<float> (readPosition - std::floor (readPosition));
                left[i] = ringL[a] + (ringL[b] - ringL[a]) * frac;
                right[i] = ringR[a] + (ringR[b] - ringR[a]) * frac;
                readPosition += 1.0 - progress;
                if (readPosition >= static_cast<double> (size))
                    readPosition -= static_cast<double> (size);
            }
            else if (inTape && target == 0.0f)
            {
                // Hold the last tape sample into the gap's fade so there is no step.
                const auto a = static_cast<size_t> (readPosition) % size;
                left[i] = ringL[a];
                right[i] = ringR[a];
            }
            else
            {
                inTape = false;
            }

            // Gap: fade out over 1.5 ms from the gap start; back in over 0.3 ms from the seam's first sample.
            if (target == 0.0f)
            {
                const auto into = (ppq[i] - gap) * spq;
                gain = static_cast<float> (std::max (0.0, 1.0 - (into + 1.0) / fadeOutSamples));
                fadeInLeft = fadeInSamples;
            }
            else if (gain < 1.0f || fadeInLeft > 0)
            {
                inTape = false;
                gain = 1.0f - static_cast<float> (fadeInLeft) / static_cast<float> (fadeInSamples + 1);
                fadeInLeft = std::max (0, fadeInLeft - 1);
                if (fadeInLeft == 0) gain = 1.0f;
            }

            if (gain < 1.0f)
            {
                left[i] *= gain;
                right[i] *= gain;
            }
            writeIndex = (writeIndex + 1) % size;
        }
    }
}
