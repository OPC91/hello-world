#include "SeamEngine.h"

#include <algorithm>
#include <cmath>

namespace seam::engine
{
    namespace
    {
        constexpr float bigDrop = 0.05f;          // decreases larger than this wait for the downbeat
        constexpr double graceQuarters = 0.25;    // ...unless they arrive within a 1/16 after it
        constexpr double mixSeconds = 0.005;      // dry/wet crossfade
        constexpr double riseSeconds = 0.03;      // smoothing when the knob goes up
    }

    void SeamEngine::prepare (double newSampleRate, int)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        chain.prepare (sampleRate);
        mixStep = static_cast<float> (1.0 / (mixSeconds * sampleRate));
        riseCoeff = std::exp (-1.0 / (riseSeconds * sampleRate));
        reset();
    }

    void SeamEngine::reset()
    {
        tracker.reset();
        position = {};
        lastTelemetry = {};
        chain.reset();
        manual = 0.0;
        dropPending = false;
        previousEffective = 0.0f;
        haveBar = false;
        mix = 0.0f;
    }

    float SeamEngine::autoRampAt (double ppq, const PhrasePosition& pos, const Settings& s) noexcept
    {
        if (! s.autoMode || ! pos.valid)
            return 0.0f;
        const int bars = std::clamp (s.autoBars, 1, pos.phraseBars);
        const double phraseLength = pos.phraseBars * pos.quartersPerBar;
        const double phraseStart = PhraseTracker::gridFloor (ppq / phraseLength) * phraseLength;
        const double rampLength = bars * pos.quartersPerBar;
        const double rampStart = phraseStart + phraseLength - rampLength;
        if (ppq < rampStart)
            return 0.0f;
        const double x = std::clamp ((ppq - rampStart) / rampLength, 0.0, 1.0);
        return static_cast<float> (s.autoCurve == 1 ? std::pow (x, 2.5) : x);
    }

    void SeamEngine::updateManual (float target, double ppq, bool atDownbeat) noexcept
    {
        if (atDownbeat && dropPending)
        {
            manual = target;   // the snap: clean on the downbeat
            dropPending = false;
            return;
        }

        if (target >= manual)
        {
            dropPending = false;
            manual = target + (manual - target) * riseCoeff;
            if (target - manual < 1.0e-6)
                manual = target;
            return;
        }

        const double drop = manual - target;
        const double intoBar = ppq - std::floor (ppq / position.quartersPerBar + 1.0e-9) * position.quartersPerBar;
        if (drop <= bigDrop)
            manual = target;   // small moves (fine adjustments, slow fades) follow at once
        else if (intoBar < graceQuarters)
            manual = target;   // just after a downbeat: a late automation point still counts as on the downbeat
        else
            dropPending = true;
    }

    void SeamEngine::process (float* const* channels, int numChannels, int numSamples, const TransportSnapshot& transport,
                              const Settings& settings) noexcept
    {
        if (numSamples <= 0 || numChannels <= 0)
            return;

        auto t = transport;
        t.numSamples = numSamples;
        position = tracker.update (t, settings.phraseBars);

        float* left = channels[0];
        float* right = numChannels > 1 ? channels[1] : nullptr;
        chunkWasBypassed = true;

        if (position.jumped)
        {
            haveBar = false;
            if (dropPending) { manual = std::min (manual, static_cast<double> (settings.transition)); dropPending = false; }
        }

        if (! position.valid)
        {
            processStopped (left, right, numSamples, settings);
        }
        else
        {
            for (int offset = 0; offset < numSamples; offset += chunkSize)
            {
                const int n = std::min (chunkSize, numSamples - offset);
                if (right != nullptr)
                {
                    processChunk (left + offset, right + offset, offset, n, settings);
                }
                else
                {
                    std::copy (left + offset, left + offset + n, monoScratch.begin());
                    processChunk (left + offset, monoScratch.data(), offset, n, settings);
                    for (int i = 0; i < n; ++i)
                        left[offset + i] = 0.5f * (left[offset + i] + monoScratch[static_cast<size_t> (i)]);
                }
            }
        }

        Telemetry tel;
        tel.playing = position.valid;
        tel.phraseBars = position.phraseBars;
        tel.barsPerBeat = position.quartersPerBeat / position.quartersPerBar;
        if (position.valid)
        {
            tel.barsToSeam = position.barsToSeam;
            tel.barInPhrase = position.barInPhrase;
            const double beats = (position.blockStartPpq - position.phraseStartPpq) / position.quartersPerBeat;
            tel.beatPhase = static_cast<float> (beats - std::floor (beats));
        }
        tel.effective = previousEffective;
        tel.bypassed = chunkWasBypassed;
        lastTelemetry = tel;
    }

    void SeamEngine::processStopped (float* left, float* right, int numSamples, const Settings& settings) noexcept
    {
        // Stopped: nothing tempo-synced, no tails. Fade any remaining effect out within a few ms, then pass through.
        manual = settings.transition;
        dropPending = false;
        previousEffective = 0.0f;

        if (mix <= 0.0f)
            return;   // untouched

        chunkWasBypassed = false;
        for (int offset = 0; offset < numSamples && mix > 0.0f; offset += chunkSize)
        {
            const int n = std::min (chunkSize, numSamples - offset);
            float* l = left + offset;
            float* r = right != nullptr ? right + offset : monoScratch.data();
            if (right == nullptr)
                std::copy (l, l + n, monoScratch.begin());
            std::copy (l, l + n, dryL.begin());
            std::copy (r, r + n, dryR.begin());
            std::fill (effective.begin(), effective.begin() + n, 0.0f);
            chain.process (l, r, n, effective.data(), ppqs.data(), 0.0, false, {});
            for (int i = 0; i < n; ++i)
            {
                const auto k = static_cast<size_t> (i);
                mix = std::max (0.0f, mix - mixStep);
                l[i] = dryL[k] * (1.0f - mix) + l[i] * mix;
                r[i] = dryR[k] * (1.0f - mix) + r[i] * mix;
            }
            if (right == nullptr)
                for (int i = 0; i < n; ++i)
                    l[i] = 0.5f * (l[i] + r[i]);
        }

        if (mix <= 0.0f)
            chain.reset();
    }

    void SeamEngine::processChunk (float* left, float* right, int offset, int n, const Settings& settings) noexcept
    {
        // 1. Effective transition per sample: knob (with the downbeat snap) or the auto ramp, whichever is higher.
        std::array<int, 8> drops {};
        int dropCount = 0;
        bool allZero = true;

        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<size_t> (i);
            const double ppq = position.ppqAt (static_cast<double> (offset + i));
            ppqs[k] = ppq;

            const auto bar = static_cast<long long> (std::floor (ppq / position.quartersPerBar + 1.0e-9));
            const bool atDownbeat = haveBar && bar != previousBar;
            previousBar = bar;
            haveBar = true;

            updateManual (settings.transition, ppq, atDownbeat);
            const float e = std::max (static_cast<float> (manual), autoRampAt (ppq, position, settings));
            effective[k] = e;
            if (e < previousEffective - bigDrop && dropCount < static_cast<int> (drops.size()))
                drops[static_cast<size_t> (dropCount++)] = i;
            previousEffective = e;
            allZero = allZero && e <= 0.0f;
        }

        // 2. True bypass: nothing to do and nothing ringing -> do not touch the audio.
        if (allZero && mix <= 0.0f && chain.isQuiet())
            return;

        chunkWasBypassed = false;
        std::copy (left, left + n, dryL.begin());
        std::copy (right, right + n, dryR.begin());

        // 3. The chain, split at every drop so the tails are cut exactly on the downbeat sample.
        const ChainAmounts amounts { settings.amtFilter, settings.amtReverb, settings.amtStutter, settings.amtDuck };
        int start = 0;
        for (int d = 0; d <= dropCount; ++d)
        {
            const int end = d < dropCount ? drops[static_cast<size_t> (d)] : n;
            if (end > start)
                chain.process (left + start, right + start, end - start, effective.data() + start, ppqs.data() + start,
                               position.samplesPerQuarter, true, amounts);
            if (d < dropCount)
                chain.cutTails();
            start = end;
        }

        // 4. Dry/wet crossfade in and out of the chain.
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<size_t> (i);
            const bool active = effective[k] > 0.0f || ! chain.isQuiet();
            mix = active ? std::min (1.0f, mix + mixStep) : std::max (0.0f, mix - mixStep);
            left[i] = dryL[k] * (1.0f - mix) + left[i] * mix;   // exact at both ends of the fade
            right[i] = dryR[k] * (1.0f - mix) + right[i] * mix;
        }

        if (mix <= 0.0f && allZero)
        {
            // Fully back to dry: start from a clean slate next time, and hand the dry signal back exactly.
            chain.reset();
            std::copy (dryL.begin(), dryL.begin() + n, left);
            std::copy (dryR.begin(), dryR.begin() + n, right);
        }
    }
}
