#include "Break.h"

#include "engine/dsp/Svf.h"

#include <exnilo/Random.h>

#include <algorithm>
#include <cmath>

namespace seam::engine
{
    const char* sliceTypeName (SliceType type) noexcept
    {
        switch (type)
        {
            case SliceType::kick:  return "kick";
            case SliceType::snare: return "snare";
            case SliceType::ghost: return "ghost";
            case SliceType::hat:   return "hat";
        }
        return "?";
    }

    void BreakKit::indexByType()
    {
        for (auto& list : byType)
            list.clear();
        for (size_t i = 0; i < slices.size(); ++i)
            byType[static_cast<size_t> (slices[i].type)].push_back (static_cast<int> (i));
    }

    KitSummary KitSummary::of (const BreakKit& kit)
    {
        KitSummary s;
        s.byType = kit.byType;
        s.numSlices = static_cast<int> (kit.slices.size());
        for (const auto& slice : kit.slices)
            s.lengthSeconds.push_back (static_cast<double> (slice.length) / kit.sampleRate);
        return s;
    }

    const std::vector<BuiltInHit>& builtInBreakHits()
    {
        // One bar of two-step at 170 BPM (times in quarter notes, converted below).
        static const std::vector<BuiltInHit> hits = []
        {
            const double q = 60.0 / 170.0;
            return std::vector<BuiltInHit> {
                { 0.00 * q, SliceType::kick,  0.95f },
                { 0.50 * q, SliceType::hat,   0.40f },
                { 1.00 * q, SliceType::snare, 0.90f },
                { 1.50 * q, SliceType::hat,   0.35f },
                { 1.75 * q, SliceType::ghost, 0.28f },
                { 2.50 * q, SliceType::kick,  0.90f },
                { 3.00 * q, SliceType::snare, 0.90f },
                { 3.375 * q, SliceType::ghost, 0.26f },
                { 3.50 * q, SliceType::hat,   0.40f },
                { 3.75 * q, SliceType::ghost, 0.30f },
            };
        }();
        return hits;
    }

    BreakKit makeBuiltInBreak (double sampleRate)
    {
        BreakKit kit;
        kit.sampleRate = sampleRate;
        kit.name = "SEAM built-in break";
        const auto length = static_cast<size_t> (std::round (4.0 * 60.0 / 170.0 * sampleRate));
        kit.left.assign (length, 0.0f);

        exnilo::Random rng (0x5ea4b4ea);
        const double pi = 3.14159265358979323846;

        for (const auto& hit : builtInBreakHits())
        {
            const auto start = static_cast<size_t> (std::round (hit.time * sampleRate));
            dsp::Svf band, high;
            band.setCoefficients (2600.0, 0.7, sampleRate);
            high.setCoefficients (8000.0, 0.7, sampleRate);
            double phase = 0.0;
            const double seconds = hit.type == SliceType::hat ? 0.06 : (hit.type == SliceType::kick ? 0.22 : 0.2);
            const auto n = std::min (static_cast<size_t> (seconds * sampleRate), length - start);

            for (size_t i = 0; i < n; ++i)
            {
                const double t = static_cast<double> (i) / sampleRate;
                const auto white = static_cast<float> (rng.uniform (-1.0, 1.0));
                double v = 0.0;
                switch (hit.type)
                {
                    case SliceType::kick:
                    {
                        const double f = 48.0 + 120.0 * std::exp (-t / 0.025);
                        phase += 2.0 * pi * f / sampleRate;
                        v = std::sin (phase) * std::exp (-t / 0.11) + 0.15 * white * std::exp (-t / 0.002);
                        break;
                    }
                    case SliceType::snare:
                    case SliceType::ghost:
                    {
                        phase += 2.0 * pi * 190.0 / sampleRate;
                        const double decay = hit.type == SliceType::ghost ? 0.035 : 0.06;
                        v = 0.45 * std::sin (phase) * std::exp (-t / 0.04)
                            + 1.1 * static_cast<double> (band.tick (white).band) * std::exp (-t / decay) * 2.5;
                        break;
                    }
                    case SliceType::hat:
                        v = 1.4 * static_cast<double> (high.tick (white).high) * std::exp (-t / 0.018);
                        break;
                }
                const double attack = std::min (1.0, static_cast<double> (i) / (0.0005 * sampleRate));
                kit.left[start + i] += static_cast<float> (v * hit.level * attack);
            }
        }

        // Normalise to -1 dBFS.
        float peak = 0.0f;
        for (const float s : kit.left)
            peak = std::max (peak, std::abs (s));
        if (peak > 0.0f)
            for (auto& s : kit.left)
                s *= 0.89f / peak;
        kit.right = kit.left;
        return kit;
    }
}
