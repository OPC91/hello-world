#include "FillPlayer.h"

#include <algorithm>
#include <cmath>

namespace seam::engine
{
    void FillPlayer::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        fadeIn = std::max (4, static_cast<int> (0.0003 * sampleRate));
        chokeFade = std::max (16, static_cast<int> (0.002 * sampleRate));
        seamFade = std::max (16, static_cast<int> (0.0013 * sampleRate));
        reset();
    }

    void FillPlayer::reset() noexcept
    {
        for (auto& v : voices)
            v = {};
    }

    void FillPlayer::killAll() noexcept
    {
        for (auto& v : voices)
            if (v.active && v.release < 0)
                v.release = chokeFade;
    }

    bool FillPlayer::isActive() const noexcept
    {
        return std::any_of (voices.begin(), voices.end(), [] (const Voice& v) { return v.active; });
    }

    double FillPlayer::windowStart (double ppq, const PhrasePosition& pos, const FillPattern& pattern) noexcept
    {
        const double phraseLength = pos.phraseBars * pos.quartersPerBar;
        const double phraseStart = PhraseTracker::gridFloor (ppq / phraseLength) * phraseLength;
        return phraseStart + phraseLength - std::min (pattern.length, phraseLength);
    }

    void FillPlayer::start (const BreakKit& kit, const FillEvent& e, double seamPpq, double hostRate, long long reverseSkip) noexcept
    {
        const auto& slice = kit.slices[static_cast<size_t> (e.slice)];
        if (slice.length < 2)
            return;

        // Choke: a new main-lane hit fades the previous one (reversed hits only choke each other).
        for (auto& v : voices)
            if (v.active && v.fxLane == e.reverse && v.release < 0)
                v.release = chokeFade;

        // Free voice, or steal the one closest to finishing its fade.
        Voice* target = nullptr;
        for (auto& v : voices)
            if (! v.active) { target = &v; break; }
        if (target == nullptr)
        {
            target = &voices[0];
            for (auto& v : voices)
                if (v.release >= 0 && (target->release < 0 || v.release < target->release))
                    target = &v;
        }

        Voice& v = *target;
        v = {};
        v.active = true;
        v.reverse = e.reverse;
        v.fxLane = e.reverse;
        v.kit = &kit;
        v.sliceStart = slice.start;
        v.sliceLength = std::min (slice.length, kit.left.size() - std::min (kit.left.size(), slice.start));
        v.rate = kit.sampleRate / hostRate;
        v.gain = e.gain;
        v.seamPpq = seamPpq;
        // Reversed: start at the slice's end (minus any part that does not fit before the seam) and run backwards,
        // so the hit's attack lands exactly on its end time.
        v.position = e.reverse ? static_cast<double> (v.sliceLength) - 2.0 - static_cast<double> (reverseSkip) * v.rate : 0.0;
        if (v.position < 0.0)
            v.active = false;
    }

    void FillPlayer::render (const BreakKit* kit, const FillPattern* pattern, const PhrasePosition& pos, int offset, int n,
                             const float* effective, float level, bool replace, Output& out) noexcept
    {
        const bool playable = kit != nullptr && pattern != nullptr && ! kit->empty() && ! pattern->events.empty()
                              && pattern->kitId == kit->id && pos.valid;

        // --- 1. Which events start in this chunk? Event range [from, to) never overlaps the previous chunk's.
        int triggerCount = 0;
        if (playable)
        {
            const double phraseLength = pos.phraseBars * pos.quartersPerBar;
            const double fillLength = std::min (pattern->length, phraseLength);
            // The same epsilon as PhrasePosition::sampleAt, so the block an event belongs to always agrees with the
            // sample it lands on (an event a hair before a sample boundary goes to the later sample either way).
            const double epsilon = 1.0e-6 * pos.quartersPerSample;
            const double from = (offset == 0 ? pos.eventFromPpq : pos.ppqAt (offset)) - epsilon;
            const double to = pos.ppqAt (offset + n) - epsilon;
            const double secondsToQuarters = (pos.samplesPerQuarter > 0.0 ? sampleRate / pos.samplesPerQuarter : 0.0);

            const auto firstPhrase = static_cast<long long> (PhraseTracker::gridFloor (from / phraseLength));
            const auto lastPhrase = static_cast<long long> (PhraseTracker::gridFloor (to / phraseLength));
            for (long long k = firstPhrase; k <= lastPhrase; ++k)
            {
                const double seam = static_cast<double> (k + 1) * phraseLength;
                const double fillStart = seam - fillLength;
                for (size_t i = 0; i < pattern->events.size() && triggerCount < maxTriggersPerChunk; ++i)
                {
                    const auto& e = pattern->events[i];
                    if (e.slice < 0 || e.slice >= static_cast<int> (kit->slices.size()) || e.ppq > fillLength + 1.0e-9)
                        continue;

                    double at = fillStart + e.ppq;
                    long long skip = 0;
                    if (e.reverse)
                    {
                        // Starts one slice-length before its end; clipped to the window start if longer than the fill.
                        const double seconds = static_cast<double> (kit->slices[static_cast<size_t> (e.slice)].length) / kit->sampleRate;
                        // ...and finishes just before the seam's fade, so its peak (the original attack) is not softened.
                        const double durationQuarters = seconds * secondsToQuarters;
                        at -= durationQuarters + (seamFade + 2) / pos.samplesPerQuarter;
                        if (at < fillStart)
                        {
                            skip = static_cast<long long> ((fillStart - at) * pos.samplesPerQuarter);
                            at = fillStart;
                        }
                    }
                    if (at < from || at >= to)
                        continue;

                    const long long s = std::clamp (pos.sampleAt (at) - offset, 0LL, static_cast<long long> (n - 1));
                    if (effective[s] <= 0.0f)
                        continue;   // the fill only plays while TRANSITION is up
                    triggers[static_cast<size_t> (triggerCount++)] = { static_cast<int> (s), static_cast<int> (i), seam, skip };
                }
            }
            // Order by sample (insertion sort: a handful of items).
            for (int i = 1; i < triggerCount; ++i)
                for (int j = i; j > 0 && triggers[static_cast<size_t> (j)].sample < triggers[static_cast<size_t> (j - 1)].sample; --j)
                    std::swap (triggers[static_cast<size_t> (j)], triggers[static_cast<size_t> (j - 1)]);
        }

        // --- 2. Render.
        const double phraseLength = pos.phraseBars * pos.quartersPerBar;
        const double fillLength = pattern != nullptr ? std::min (pattern->length, phraseLength) : 0.0;
        const double rampQuarters = pos.samplesPerQuarter > 0.0 ? 128.0 / pos.samplesPerQuarter : 0.0;
        int next = 0;

        for (int i = 0; i < n; ++i)
        {
            while (next < triggerCount && triggers[static_cast<size_t> (next)].sample == i)
            {
                const auto& t = triggers[static_cast<size_t> (next++)];
                const auto& e = pattern->events[static_cast<size_t> (t.event)];
                start (*kit, e, t.seamPpq, sampleRate, t.reverseSkip);
                const auto type = kit->slices[static_cast<size_t> (e.slice)].type;
                out.snareHit = out.snareHit || (! e.reverse && type == SliceType::snare);
            }

            const double ppq = pos.valid ? pos.ppqAt (offset + i) : 0.0;
            // Fill level: fades in over the first 25% of TRANSITION, scaled by the Advanced "Fill" amount.
            const float fillLevel = std::min (1.0f, effective[i] * 4.0f) * level;

            float l = 0.0f, r = 0.0f;
            for (auto& v : voices)
            {
                if (! v.active)
                    continue;
                const double samplesToSeam = pos.valid ? (v.seamPpq - ppq) * pos.samplesPerQuarter : 1.0e9;
                const auto index = static_cast<long long> (v.position);
                if (samplesToSeam <= 0.0 || v.position < 0.0 || index + 1 >= static_cast<long long> (v.sliceLength) || v.release == 0)
                {
                    v.active = false;
                    continue;
                }
                const double frac = v.position - static_cast<double> (index);
                const auto a = v.sliceStart + static_cast<size_t> (index);
                const float sl = static_cast<float> (v.kit->left[a] + (v.kit->left[a + 1] - v.kit->left[a]) * frac);
                const float sr = static_cast<float> (v.kit->right[a] + (v.kit->right[a + 1] - v.kit->right[a]) * frac);

                float env = std::min (1.0f, static_cast<float> (v.age + 1) / static_cast<float> (fadeIn));
                if (! v.reverse)   // reversed hits end on their attack: no fade there
                    env *= static_cast<float> (std::min (1.0, (static_cast<double> (v.sliceLength) - v.position) / (chokeFade * v.rate)));
                env *= static_cast<float> (std::min (1.0, samplesToSeam / seamFade));
                if (v.release >= 0)
                    env *= static_cast<float> (v.release--) / static_cast<float> (chokeFade);

                const float g = env * v.gain * fillLevel;
                l += sl * g;
                r += sr * g;
                v.position += v.reverse ? -v.rate : v.rate;
                ++v.age;
            }
            out.left[i] += l;
            out.right[i] += r;

            // Replace: duck the incoming drums inside the window, back to full by the seam.
            if (replace && playable && fillLength > 0.0)
            {
                const double ws = windowStart (ppq, pos, *pattern);
                const double seam = ws + fillLength;
                if (ppq >= ws)
                {
                    const double ramp = std::min ({ 1.0, (ppq - ws) / rampQuarters, (seam - ppq) / rampQuarters });
                    out.dryGain[i] *= 1.0f - std::min (1.0f, fillLevel) * static_cast<float> (std::max (0.0, ramp));
                }
            }
        }
    }
}
