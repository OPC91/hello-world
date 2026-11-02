#include "TransitionChain.h"

#include <algorithm>
#include <cmath>

namespace seam::engine
{
    namespace
    {
        constexpr double minCutoff = 20.0;
        constexpr double designMaxCutoff = 700.0;   // where the high-pass ends up at 100% (with amount 100%)
        constexpr double maxCaptureSeconds = 1.0;    // longest stutter cell: an 1/8 at 30 BPM
        constexpr float throwWet = 0.85f;
        constexpr double pi = 3.14159265358979323846;
    }

    double TransitionChain::cutoffFor (float effective, float amount) noexcept
    {
        const double top = std::max (minCutoff, designMaxCutoff * std::clamp (static_cast<double> (amount), 0.0, 1.5));
        return minCutoff * std::pow (top / minCutoff, std::pow (std::clamp (static_cast<double> (effective), 0.0, 1.0), 1.4));
    }

    double TransitionChain::stutterDivisionFor (float effective) noexcept
    {
        if (effective < 0.35f) return 0.0;
        if (effective < 0.6f)  return 0.5;    // 1/8
        if (effective < 0.85f) return 0.25;   // 1/16
        return 0.125;                         // 1/32
    }

    float TransitionChain::duckShape (double phase) noexcept
    {
        // Transient passes (first 12% of the beat), dip to full depth at 30%, recover by 90%.
        if (phase < 0.12 || phase >= 0.9) return 0.0f;
        if (phase < 0.3) return static_cast<float> (0.5 - 0.5 * std::cos (pi * (phase - 0.12) / 0.18));
        return static_cast<float> (0.5 + 0.5 * std::cos (pi * (phase - 0.3) / 0.6));
    }

    void TransitionChain::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        reverb.prepare (sampleRate);
        const auto capture = static_cast<size_t> (std::ceil (maxCaptureSeconds * sampleRate));
        captureL.assign (capture, 0.0f);
        captureR.assign (capture, 0.0f);

        cutoffLog2.setTime (0.003, sampleRate);
        resonance.setTime (0.02, sampleRate);
        throwGate.setTime (0.0, sampleRate);
        sendLevel.setTime (0.02, sampleRate);
        stutterMix.setTime (0.002, sampleRate);
        duckDepth.setTime (0.02, sampleRate);
        bandFast.setTimes (0.0003, 0.012, sampleRate);
        bandSlow.setTimes (0.010, 0.250, sampleRate);
        lowFast.setTimes (0.0003, 0.020, sampleRate);
        detectBand.setCoefficients (1800.0, 0.9, sampleRate);
        detectLow.setCoefficients (120.0, 0.7, sampleRate);
        rampSamples = std::max (8, static_cast<int> (0.0015 * sampleRate));
        reset();
    }

    void TransitionChain::reset() noexcept
    {
        for (auto& f : highPass) f.reset();
        detectBand.reset();
        detectLow.reset();
        bandFast.env = bandSlow.env = lowFast.env = 0.0f;
        cutoffLog2.value = static_cast<float> (std::log2 (minCutoff));
        resonance.value = 0.7071f;
        coefficientCountdown = 0;
        refractory = throwHold = 0;
        throwGate.value = sendLevel.value = 0.0f;
        reverb.reset();
        tailFade = 1.0f;
        cuttingTails = false;
        currentBeat = -1;
        capturedBeat = -2;
        division = 0.0;
        stutterMix.value = 0.0f;
        duckDepth.value = 0.0f;
    }

    void TransitionChain::throwSnare (double samplesPerQuarter) noexcept
    {
        throwHold = std::max (throwHold, static_cast<int> (samplesPerQuarter > 0.0 ? samplesPerQuarter * 0.25 : 0.06 * sampleRate));
    }

    void TransitionChain::cutTails() noexcept
    {
        if (cuttingTails)
            return;
        cuttingTails = true;
        tailFadeStep = static_cast<float> (1.0 / (0.010 * sampleRate));
        throwHold = 0;
        stutterMix.value = 0.0f;
        capturedBeat = -2;
    }

    bool TransitionChain::isQuiet() const noexcept
    {
        return reverb.isQuiet() && throwHold == 0 && throwGate.value == 0.0f && stutterMix.value == 0.0f && ! cuttingTails;
    }

    void TransitionChain::process (float* left, float* right, int n, const float* effective, const double* ppq,
                                   double samplesPerQuarter, bool tempoValid, const ChainAmounts& amounts, const ChainExtras& extras) noexcept
    {
        const auto captureSize = static_cast<long long> (captureL.size());
        const float stutterScale = std::clamp (amounts.stutter, 0.0f, 1.5f);

        for (int i = 0; i < n; ++i)
        {
            const float e = effective[i];
            const float dryL = left[i], dryR = right[i];

            // --- 1. High-pass sweep (coefficients refreshed every 4 samples).
            const float targetLog2 = static_cast<float> (std::log2 (cutoffFor (e, amounts.filter)));
            cutoffLog2.next (targetLog2);
            resonance.next (0.7071f + 0.85f * e * std::min (1.0f, amounts.filter));
            if (--coefficientCountdown <= 0)
            {
                coefficientCountdown = 4;
                const double fc = std::exp2 (static_cast<double> (cutoffLog2.value));
                for (auto& f : highPass)
                    f.setCoefficients (fc, resonance.value, sampleRate);
            }
            float l = highPass[0].tick (dryL).high;
            float r = highPass[1].tick (dryR).high;

            // --- 2. Snare detection on the input: bright onset that is not just a kick.
            const float mono = 0.5f * (dryL + dryR);
            const float band = bandFast.next (detectBand.tick (mono).band);
            const float slow = bandSlow.next (band);
            const float low = lowFast.next (detectLow.tick (mono).low);
            if (refractory > 0)
                --refractory;
            else if (band > 0.015f && band > 2.2f * slow && band > 0.6f * low)
            {
                throwSnare (tempoValid ? samplesPerQuarter : 0.0);
                refractory = static_cast<int> (0.08 * sampleRate);
            }

            // --- 3. Beat-locked stutter on the filtered signal.
            if (tempoValid && captureSize > 0)
            {
                const double pos = ppq[i];
                const auto beat = static_cast<long long> (std::floor (pos + 1.0e-9));
                const double phase = pos - static_cast<double> (beat);
                if (beat != currentBeat)
                {
                    currentBeat = beat;
                    division = cuttingTails ? 0.0 : stutterDivisionFor (e);   // locked for the whole beat
                }

                const float mixTarget = (division > 0.0 && ! cuttingTails)
                                            ? std::min (1.0f, std::clamp ((e - 0.3f) / 0.4f, 0.0f, 1.0f) * stutterScale)
                                            : 0.0f;
                float repeatL = l, repeatR = r, gate = 0.0f;

                if (division > 0.0)
                {
                    const double cellSamples = std::min (division * samplesPerQuarter, static_cast<double> (captureSize - 1));
                    const double intoBeat = phase * samplesPerQuarter + 1.0e-6;   // epsilon: host positions are rarely exact
                    if (intoBeat < cellSamples)
                    {
                        const auto w = static_cast<long long> (intoBeat);
                        captureL[static_cast<size_t> (w)] = l;
                        captureR[static_cast<size_t> (w)] = r;
                        if (w < 32)
                            capturedBeat = beat;   // capture started at the beat: safe to repeat
                    }
                    else if (capturedBeat == beat)
                    {
                        const double intoCell = std::fmod (intoBeat, cellSamples);
                        const auto rIndex = std::min (static_cast<long long> (intoCell), captureSize - 1);
                        repeatL = captureL[static_cast<size_t> (rIndex)];
                        repeatR = captureR[static_cast<size_t> (rIndex)];
                        const double edge = std::min (intoCell, cellSamples - intoCell);
                        gate = static_cast<float> (std::min (1.0, edge / rampSamples));
                    }
                }

                const float mix = stutterMix.next (mixTarget) * gate;
                l = l * (1.0f - mix) + repeatL * mix;   // exact repeat at mix 1
                r = r * (1.0f - mix) + repeatR * mix;
            }
            else
            {
                stutterMix.next (0.0f);
            }

            // --- 4. Ducking dip.
            const float depth = duckDepth.next (tempoValid ? std::clamp (std::pow (e, 1.5f) * 0.55f * amounts.duck, 0.0f, 0.9f) : 0.0f);
            if (depth > 0.0f && tempoValid)
            {
                const double phase = ppq[i] - std::floor (ppq[i] + 1.0e-9);
                const float g = 1.0f - depth * duckShape (phase);
                l *= g;
                r *= g;
            }

            // --- 2b. Reverb throw (send from the processed signal, wet added on top).
            if (throwHold > 0)
                --throwHold;
            const float gateTarget = throwHold > 0 && ! cuttingTails ? 1.0f : 0.0f;
            throwGate.coeff = gateTarget > throwGate.value ? static_cast<float> (std::exp (-1.0 / (0.001 * sampleRate)))
                                                           : static_cast<float> (std::exp (-1.0 / (0.04 * sampleRate)));
            const float send = throwGate.next (gateTarget)
                               * sendLevel.next (std::clamp (std::pow (e, 1.3f) * 0.9f * amounts.reverb, 0.0f, 1.4f));
            if (extras.inputGain != nullptr)
            {
                l *= extras.inputGain[i];
                r *= extras.inputGain[i];
            }
            const float sendInL = l + (extras.sendLeft != nullptr ? extras.sendLeft[i] : 0.0f);
            const float sendInR = r + (extras.sendRight != nullptr ? extras.sendRight[i] : 0.0f);
            float wetL = 0.0f, wetR = 0.0f;
            if (send > 0.0f || ! reverb.isQuiet())
                reverb.process (sendInL * send, sendInR * send, wetL, wetR);

            if (cuttingTails)
            {
                tailFade -= tailFadeStep;
                if (tailFade <= 0.0f)
                {
                    reverb.reset();
                    throwGate.value = 0.0f;
                    tailFade = 1.0f;
                    cuttingTails = false;
                    wetL = wetR = 0.0f;
                }
            }

            left[i] = l + wetL * throwWet * tailFade;
            right[i] = r + wetR * throwWet * tailFade;
        }
    }
}
