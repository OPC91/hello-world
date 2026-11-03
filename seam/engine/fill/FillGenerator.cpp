#include "FillGenerator.h"

#include <exnilo/Random.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace seam::engine
{
    namespace
    {
        constexpr double eps = 1.0e-9;

        enum class Body { chop, ghostStutter, halfTime };

        struct StyleTraits
        {
            const char* name;
            std::array<double, 3> bodyWeights;   ///< chop, ghost stutter, half-time
            double ghostDensity;                 ///< ghost probability per free 16th at full complexity
            double reverseChance;
            int maxRollStep;                     ///< index into rollSteps reached at full complexity
            float rollFloor, rollCeiling;        ///< roll gain ramp
            bool amen;                           ///< amen-style chop pattern
        };

        const StyleTraits& traitsOf (FillStyle style)
        {
            static const StyleTraits traits[] {
                { "Liquid",  { 0.60, 0.15, 0.25 }, 0.45, 0.70, 2, 0.30f, 0.80f, false },
                { "Neuro",   { 0.25, 0.35, 0.40 }, 0.35, 0.35, 3, 0.45f, 1.00f, false },
                { "Jungle",  { 0.55, 0.35, 0.10 }, 0.70, 0.30, 3, 0.40f, 0.95f, true  },
                { "Jump-Up", { 0.30, 0.15, 0.55 }, 0.30, 0.80, 3, 0.50f, 1.00f, false },
            };
            return traits[static_cast<size_t> (std::clamp (static_cast<int> (style), 0, 3))];
        }

        struct Builder
        {
            const KitSummary& kit;
            exnilo::Random rng;
            std::vector<FillEvent> events;

            /** A slice of the wanted type, falling back to the closest type the kit has. */
            int pick (SliceType type)
            {
                static const std::array<std::array<SliceType, 4>, numSliceTypes> fallbacks {{
                    { SliceType::kick,  SliceType::snare, SliceType::ghost, SliceType::hat },
                    { SliceType::snare, SliceType::ghost, SliceType::kick,  SliceType::hat },
                    { SliceType::ghost, SliceType::snare, SliceType::hat,   SliceType::kick },
                    { SliceType::hat,   SliceType::ghost, SliceType::snare, SliceType::kick },
                }};
                for (const auto t : fallbacks[static_cast<size_t> (type)])
                {
                    const auto& list = kit.byType[static_cast<size_t> (t)];
                    if (! list.empty())
                        return list[static_cast<size_t> (rng.uniformInt (0, static_cast<int> (list.size()) - 1))];
                }
                return kit.numSlices > 0 ? rng.uniformInt (0, kit.numSlices - 1) : -1;
            }

            void add (double ppq, SliceType type, float gain, FillRole role)
            {
                const int slice = pick (type);
                if (slice >= 0)
                    events.push_back ({ ppq, slice, std::clamp (gain, 0.05f, 1.0f), false, role });
            }
        };

        /** Bar-relative two-step (or amen-style) skeleton, in quarter notes of a 4/4 bar. */
        void chopBar (Builder& b, const StyleTraits& traits, double barStart, double from, double to, double qpb, double c)
        {
            struct Hit { double at; SliceType type; float gain; };
            std::vector<Hit> hits;
            if (traits.amen)
                hits = { { 0.0, SliceType::kick, 0.95f }, { 0.5, SliceType::kick, 0.8f }, { 1.0, SliceType::snare, 0.95f },
                         { 1.75, SliceType::ghost, 0.45f }, { 2.25, SliceType::kick, 0.85f }, { 2.5, SliceType::kick, 0.85f },
                         { 3.0, SliceType::snare, 0.95f } };
            else
                hits = { { 0.0, SliceType::kick, 0.95f }, { 1.0, SliceType::snare, 0.95f }, { 2.5, SliceType::kick, 0.9f },
                         { 3.0, SliceType::snare, 0.95f } };

            // Random edits grow with complexity: a moved kick, an extra snare on the last 16th.
            if (b.rng.chance (0.5 * c)) hits.push_back ({ 3.75, SliceType::snare, 0.7f });
            if (b.rng.chance (0.4 * c)) hits.push_back ({ 1.5, SliceType::kick, 0.7f });

            const double scale = qpb / 4.0;   // stretch the 4/4 template over other bar lengths
            std::vector<bool> taken (static_cast<size_t> (std::ceil (qpb / 0.25)) + 1, false);
            for (const auto& h : hits)
            {
                const double at = barStart + h.at * scale;
                if (at >= from - eps && at < to - eps)
                {
                    b.add (at, h.type, h.gain, FillRole::groove);
                    taken[static_cast<size_t> (std::round (h.at * scale / 0.25))] = true;
                }
            }

            // Ghosts and hats on the free 16ths.
            for (size_t i = 0; i < taken.size() - 1; ++i)
            {
                const double at = barStart + static_cast<double> (i) * 0.25;
                if (taken[i] || at < from - eps || at >= to - eps)
                    continue;
                if (b.rng.chance (traits.ghostDensity * c))
                    b.add (at, SliceType::ghost, static_cast<float> (b.rng.uniform (0.3, 0.5)), FillRole::groove);
                else if (i % 2 == 1 && b.rng.chance (0.25 + 0.5 * c))
                    b.add (at, SliceType::hat, static_cast<float> (b.rng.uniform (0.4, 0.6)), FillRole::groove);
            }
        }

        void halfTimeBar (Builder& b, double barStart, double from, double to, double qpb, double c)
        {
            const double half = qpb / 2.0;
            auto put = [&] (double rel, SliceType t, float g)
            {
                const double at = barStart + rel;
                if (at >= from - eps && at < to - eps)
                    b.add (at, t, g, FillRole::halfTime);
            };
            put (0.0, SliceType::kick, 1.0f);
            put (half, SliceType::snare, 1.0f);
            if (b.rng.chance (0.3 + 0.5 * c)) put (half + 0.75, SliceType::kick, 0.8f);
            for (double t = 0.5; t < qpb - eps; t += 1.0)
                if (b.rng.chance (0.3 + 0.4 * c))
                    put (t, SliceType::hat, 0.5f);
            if (b.rng.chance (0.6 * c)) put (half - 0.25, SliceType::ghost, 0.4f);
            if (b.rng.chance (0.6 * c)) put (qpb - 0.25, SliceType::ghost, 0.45f);
        }

        void ghostStutterBar (Builder& b, const StyleTraits& traits, double barStart, double from, double to, double qpb, double c)
        {
            // A sparse skeleton with bursts of 1/32 ghost chops.
            auto put = [&] (double at, SliceType t, float g, FillRole role)
            {
                if (at >= from - eps && at < to - eps)
                    b.add (at, t, g, role);
            };
            put (barStart, SliceType::kick, 0.95f, FillRole::groove);
            put (barStart + qpb * 0.25, SliceType::snare, 0.95f, FillRole::groove);
            put (barStart + qpb * 0.75, SliceType::snare, 0.95f, FillRole::groove);

            const int bursts = 1 + static_cast<int> (std::round (c * 2.0 + (traits.amen ? 1.0 : 0.0)));
            for (int k = 0; k < bursts; ++k)
            {
                const int slot = b.rng.uniformInt (1, std::max (1, static_cast<int> (qpb / 0.5) - 1));
                const double start = barStart + slot * 0.5;
                const double length = c > 0.6 ? 0.5 : 0.25;
                float gain = 0.35f;
                for (double t = start; t < start + length - eps; t += 0.125)
                {
                    if (t < from - eps || t >= to - eps)
                        continue;
                    // Do not stack on an existing skeleton hit.
                    const bool clash = std::any_of (b.events.begin(), b.events.end(), [t] (const FillEvent& e) { return std::abs (e.ppq - t) < eps; });
                    if (! clash)
                        b.add (t, SliceType::ghost, gain, FillRole::ghostChop);
                    gain += 0.06f;
                }
            }
        }

        /** Snare roll over [start, end): the subdivision tightens section by section (never loosens), and the
            hits get louder towards the seam. Returns the fastest step reached. */
        int roll (Builder& b, const StyleTraits& traits, double start, double end, double c)
        {
            const int top = std::clamp (static_cast<int> (std::round (traits.maxRollStep * (0.35 + 0.65 * c))), 0, 3);
            const int sections = top + 1;
            const double length = end - start;
            for (double t = start; t < end - eps;)
            {
                const double progress = (t - start) / length;
                const int section = std::min (sections - 1, static_cast<int> (progress * sections + eps));
                const float gain = traits.rollFloor + (traits.rollCeiling - traits.rollFloor) * static_cast<float> (progress);
                b.add (t, gain < 0.45f ? SliceType::ghost : SliceType::snare, gain, FillRole::roll);
                t += rollSteps[section];
            }
            return top;
        }
    }

    const char* rollStepName (double quarters) noexcept
    {
        if (std::abs (quarters - 0.25) < eps) return "1/16";
        if (std::abs (quarters - 1.0 / 6.0) < eps) return "1/16T";
        if (std::abs (quarters - 0.125) < eps) return "1/32";
        if (std::abs (quarters - 1.0 / 12.0) < eps) return "1/32T";
        return "?";
    }

    FillPattern generateFill (const KitSummary& kit, const FillRequest& request)
    {
        FillPattern pattern;
        pattern.bars = std::clamp (request.bars, 1, 8);
        pattern.quartersPerBar = request.quartersPerBar > 0.0 ? request.quartersPerBar : 4.0;
        pattern.length = pattern.bars * pattern.quartersPerBar;
        pattern.seed = request.seed;

        const auto& traits = traitsOf (request.style);
        pattern.description = traits.name;
        if (kit.numSlices == 0)
        {
            pattern.description += ": no break loaded";
            return pattern;
        }

        const auto styleSalt = static_cast<std::uint64_t> (static_cast<unsigned> (request.style));
        Builder b { kit, exnilo::Random (request.seed * std::uint64_t { 0x9E3779B97F4A7C15ull } + styleSalt), {} };
        const double c = std::clamp (static_cast<double> (request.complexity), 0.0, 1.0);
        const double qpb = pattern.quartersPerBar;
        const double length = pattern.length;

        // The roll: whole beats at the end of the fill, longer with complexity (half a bar to a full bar).
        const double beat = qpb >= 3.0 ? 1.0 : 0.5;
        double rollLength = std::round ((qpb * (0.5 + 0.5 * c)) / beat) * beat;
        if (request.style == FillStyle::liquid && c < 0.4)
            rollLength = beat;   // Liquid at low complexity: just a short lift into the reverse
        rollLength = std::clamp (rollLength, beat, qpb);
        const double rollStart = length - rollLength;

        // Body: every bar (or part of a bar) before the roll.
        std::vector<std::string> parts;
        bool switchedToHalfTime = false;
        for (int bar = 0; bar < pattern.bars; ++bar)
        {
            const double barStart = bar * qpb;
            const double to = std::min (barStart + qpb, rollStart);
            if (to <= barStart + eps)
                break;

            Body body = switchedToHalfTime ? Body::halfTime
                                           : static_cast<Body> (b.rng.weightedIndex (traits.bodyWeights));
            if (body == Body::halfTime && ! switchedToHalfTime)
            {
                switchedToHalfTime = true;
                parts.push_back ("half-time switch");
            }
            else if (body == Body::ghostStutter && std::find (parts.begin(), parts.end(), "ghost chops") == parts.end())
                parts.push_back ("ghost chops");
            else if (body == Body::chop && std::find (parts.begin(), parts.end(), "chopped groove") == parts.end())
                parts.push_back ("chopped groove");

            switch (body)
            {
                case Body::chop:         chopBar (b, traits, barStart, barStart, to, qpb, c); break;
                case Body::ghostStutter: ghostStutterBar (b, traits, barStart, barStart, to, qpb, c); break;
                case Body::halfTime:     halfTimeBar (b, barStart, barStart, to, qpb, c); break;
            }
        }

        const int top = roll (b, traits, rollStart, length, c);
        parts.push_back (std::string (rollStepName (rollSteps[top])) + " roll");

        if (b.rng.chance (std::min (1.0, traits.reverseChance + 0.2 * c)) && kit.numSlices > 0)
        {
            const int slice = b.pick (SliceType::snare);
            b.events.push_back ({ length, slice, 0.85f, true, FillRole::reverse });
            parts.push_back ("reverse snare");
        }

        std::stable_sort (b.events.begin(), b.events.end(), [] (const FillEvent& x, const FillEvent& y) { return x.ppq < y.ppq; });
        pattern.events = std::move (b.events);

        pattern.description += ": ";
        for (size_t i = 0; i < parts.size(); ++i)
            pattern.description += (i > 0 ? ", " : "") + parts[i];
        return pattern;
    }
}
