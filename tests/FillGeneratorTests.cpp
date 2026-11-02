#include "TestHelpers.h"
#include "engine/fill/FillGenerator.h"

#include <catch2/generators/catch_generators.hpp>

using namespace seam::test;
using Catch::Approx;

namespace
{
    KitSummary builtInSummary()
    {
        static const auto summary = []
        {
            const auto built = makeBuiltInBreak (44100.0);
            BreakKit kit;
            kit.sampleRate = built.sampleRate;
            // Label the slices with the known hits (keeps these tests independent of the slicer).
            const auto& hits = builtInBreakHits();
            for (size_t i = 0; i < hits.size(); ++i)
            {
                Slice s;
                s.start = static_cast<size_t> (hits[i].time * 44100.0);
                const size_t end = i + 1 < hits.size() ? static_cast<size_t> (hits[i + 1].time * 44100.0) : built.left.size();
                s.length = end - s.start;
                s.type = hits[i].type;
                kit.slices.push_back (s);
            }
            kit.indexByType();
            return KitSummary::of (kit);
        }();
        return summary;
    }

    FillRequest request (FillStyle style, float complexity, int bars, std::uint64_t seed)
    {
        FillRequest r;
        r.style = style;
        r.complexity = complexity;
        r.bars = bars;
        r.seed = seed;
        return r;
    }
}

TEST_CASE ("Fills are deterministic and stay inside the fill", "[fill]")
{
    const auto style = static_cast<FillStyle> (GENERATE (0, 1, 2, 3));
    const int bars = GENERATE (1, 2, 4);
    const float complexity = GENERATE (0.0f, 0.5f, 1.0f);
    const auto kit = builtInSummary();

    for (std::uint64_t seed = 1; seed <= 12; ++seed)
    {
        const auto a = generateFill (kit, request (style, complexity, bars, seed));
        const auto b = generateFill (kit, request (style, complexity, bars, seed));
        REQUIRE (a.events.size() == b.events.size());
        for (size_t i = 0; i < a.events.size(); ++i)
        {
            CHECK (a.events[i].ppq == b.events[i].ppq);
            CHECK (a.events[i].slice == b.events[i].slice);
        }

        CHECK (a.length == Approx (bars * 4.0));
        REQUIRE_FALSE (a.events.empty());
        for (size_t i = 0; i < a.events.size(); ++i)
        {
            const auto& e = a.events[i];
            CHECK (e.slice >= 0);
            CHECK (e.slice < kit.numSlices);
            CHECK (e.gain > 0.0f);
            CHECK (e.gain <= 1.0f);
            if (e.reverse)
                CHECK (e.ppq == Approx (a.length));   // reversed snares end exactly on the seam
            else
            {
                CHECK (e.ppq >= 0.0);
                CHECK (e.ppq < a.length);
            }
            if (i > 0)
                CHECK (a.events[i - 1].ppq <= e.ppq);
        }
    }
}

TEST_CASE ("Snare rolls tighten towards the seam and never loosen", "[fill]")
{
    const auto kit = builtInSummary();
    for (int style = 0; style < 4; ++style)
        for (std::uint64_t seed = 1; seed <= 10; ++seed)
        {
            const auto fill = generateFill (kit, request (static_cast<FillStyle> (style), 1.0f, 2, seed));
            std::vector<double> times;
            for (const auto& e : fill.events)
                if (e.role == FillRole::roll)
                    times.push_back (e.ppq);
            REQUIRE (times.size() >= 4);
            for (size_t i = 2; i < times.size(); ++i)
                CHECK (times[i] - times[i - 1] <= times[i - 1] - times[i - 2] + 1.0e-9);
            CHECK (times.front() >= fill.length - 4.0 - 1.0e-9);   // inside the last bar
        }
}

TEST_CASE ("Roll vocabulary: 1/16 up to 1/32 triplets, by style and complexity", "[fill]")
{
    const auto kit = builtInSummary();
    auto fastest = [&] (FillStyle style, float c)
    {
        const auto fill = generateFill (kit, request (style, c, 1, 3));
        double smallest = 1.0;
        for (size_t i = 1; i < fill.events.size(); ++i)
            if (fill.events[i].role == FillRole::roll && fill.events[i - 1].role == FillRole::roll)
                smallest = std::min (smallest, fill.events[i].ppq - fill.events[i - 1].ppq);
        return smallest;
    };
    CHECK (fastest (FillStyle::neuro, 1.0f) == Approx (1.0 / 12.0));   // 1/32T
    CHECK (fastest (FillStyle::jumpUp, 1.0f) == Approx (1.0 / 12.0));
    CHECK (fastest (FillStyle::liquid, 1.0f) == Approx (0.125));       // Liquid stops at 1/32
    CHECK (fastest (FillStyle::neuro, 0.0f) > 0.125 - 1.0e-9);         // low complexity: slower roll
}

TEST_CASE ("Complexity makes fills denser", "[fill]")
{
    const auto kit = builtInSummary();
    for (int style = 0; style < 4; ++style)
    {
        size_t sparse = 0, dense = 0;
        for (std::uint64_t seed = 1; seed <= 30; ++seed)
        {
            sparse += generateFill (kit, request (static_cast<FillStyle> (style), 0.0f, 2, seed)).events.size();
            dense += generateFill (kit, request (static_cast<FillStyle> (style), 1.0f, 2, seed)).events.size();
        }
        INFO ("style " << style);
        CHECK (static_cast<double> (dense) > static_cast<double> (sparse) * 1.3);
    }
}

TEST_CASE ("New fill: a new seed gives a different fill", "[fill]")
{
    const auto kit = builtInSummary();
    int different = 0;
    for (std::uint64_t seed = 1; seed <= 10; ++seed)
    {
        const auto a = generateFill (kit, request (FillStyle::jungle, 0.7f, 2, seed));
        const auto b = generateFill (kit, request (FillStyle::jungle, 0.7f, 2, seed + 1));
        bool same = a.events.size() == b.events.size();
        for (size_t i = 0; same && i < a.events.size(); ++i)
            same = a.events[i].ppq == b.events[i].ppq && a.events[i].slice == b.events[i].slice;
        different += same ? 0 : 1;
    }
    CHECK (different == 10);
}

TEST_CASE ("Styles use their vocabulary", "[fill]")
{
    const auto kit = builtInSummary();
    auto count = [&] (FillStyle style, FillRole role)
    {
        int n = 0;
        for (std::uint64_t seed = 1; seed <= 40; ++seed)
            for (const auto& e : generateFill (kit, request (style, 0.7f, 4, seed)).events)
                n += e.role == role ? 1 : 0;
        return n;
    };
    // Jump-Up switches to half-time far more than Jungle; Jungle chops ghosts more than Liquid.
    CHECK (count (FillStyle::jumpUp, FillRole::halfTime) > 2 * count (FillStyle::jungle, FillRole::halfTime));
    CHECK (count (FillStyle::jungle, FillRole::ghostChop) > count (FillStyle::liquid, FillRole::ghostChop));
    CHECK (count (FillStyle::liquid, FillRole::reverse) > count (FillStyle::jungle, FillRole::reverse));
}

TEST_CASE ("Once a fill switches to half-time it stays half-time until the roll", "[fill]")
{
    const auto kit = builtInSummary();
    int switched = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed)
    {
        const auto fill = generateFill (kit, request (FillStyle::jumpUp, 0.5f, 4, seed));
        bool inHalfTime = false;
        for (const auto& e : fill.events)
        {
            if (e.role == FillRole::halfTime) inHalfTime = true;
            if (inHalfTime && e.role != FillRole::roll && e.role != FillRole::reverse)
                CHECK (e.role == FillRole::halfTime);
            if (e.role == FillRole::roll) break;
        }
        switched += inHalfTime ? 1 : 0;
    }
    CHECK (switched > 10);
}

TEST_CASE ("Fills work with whatever slices the break has", "[fill]")
{
    KitSummary snaresOnly;
    snaresOnly.numSlices = 2;
    snaresOnly.byType[static_cast<size_t> (SliceType::snare)] = { 0, 1 };
    snaresOnly.lengthSeconds = { 0.2, 0.2 };
    const auto fill = generateFill (snaresOnly, request (FillStyle::neuro, 1.0f, 2, 5));
    CHECK_FALSE (fill.events.empty());
    for (const auto& e : fill.events)
        CHECK ((e.slice == 0 || e.slice == 1));

    const auto none = generateFill (KitSummary {}, request (FillStyle::neuro, 1.0f, 2, 5));
    CHECK (none.events.empty());
}

TEST_CASE ("Odd time signatures stretch the fill to the bar", "[fill]")
{
    const auto kit = builtInSummary();
    auto r = request (FillStyle::jungle, 0.8f, 2, 9);
    r.quartersPerBar = 3.5; // 7/8
    const auto fill = generateFill (kit, r);
    CHECK (fill.length == Approx (7.0));
    for (const auto& e : fill.events)
        if (! e.reverse)
            CHECK (e.ppq < 7.0);
}
