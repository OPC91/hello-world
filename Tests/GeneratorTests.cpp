#include <catch2/catch_test_macros.hpp>

#include "Core/FactoryPatterns.h"
#include "Core/PatternGenerator.h"
#include "Core/Params.h"
#include "Core/PatternTools.h"

#include <set>
#include <vector>

namespace
{
    using bf::PatternGenerator::Options;

    constexpr int kick = 0, snare = 1, ghost = 2, closedHat = 3;

    int stepOf (const bf::Pattern& p, int sixteenth) { return sixteenth * bf::PatternTools::stepsPerSixteenth (p); }

    bool on (const bf::Pattern& p, int track, int sixteenth)
    {
        const int s = stepOf (p, sixteenth);
        return s < p.length && p.tracks[(size_t) track].steps[(size_t) s].on;
    }

    /** The DnB validity rules every generated pattern must satisfy. */
    void requireValidDnB (const bf::Pattern& p, bf::SubStyle style)
    {
        const int sixteenths = bf::PatternTools::lengthInSixteenths (p);
        const auto anchors = bf::PatternGenerator::snareAnchors (style);

        for (int bar = 0; bar * 16 < sixteenths; ++bar)
        {
            // Kick on the downbeat of every bar, never on the backbeat.
            REQUIRE (on (p, kick, bar * 16));

            for (int a : anchors)
            {
                if (bar * 16 + a >= sixteenths)
                    continue;

                REQUIRE (on (p, snare, bar * 16 + a));
                REQUIRE_FALSE (on (p, kick, bar * 16 + a));
            }

            // No snare on the downbeat.
            REQUIRE_FALSE (on (p, snare, bar * 16));
        }

        // Halftime keeps beats 2 and 4 free of the main snare.
        if (style == bf::SubStyle::Halftime)
        {
            REQUIRE_FALSE (on (p, snare, 4));
            REQUIRE_FALSE (on (p, snare, 12));
        }

        // Realistic dynamics: never a wall of flat 127s, and ghosts are actually soft.
        std::set<int> velocities;
        int hits = 0;

        for (int t = 0; t < bf::kNumTracks; ++t)
        {
            for (int s = 0; s < p.length; ++s)
            {
                const auto& st = p.tracks[(size_t) t].steps[(size_t) s];
                if (! st.on)
                    continue;

                ++hits;
                velocities.insert (st.velocity);
                REQUIRE (st.velocity >= 1);
                REQUIRE (st.velocity <= 127);
                REQUIRE (st.ratchet >= 1);
                REQUIRE (st.ratchet <= 4);
                REQUIRE (std::abs (st.nudge) <= 0.5f);

                if (t == ghost)
                {
                    REQUIRE (st.velocity <= 60);
                    REQUIRE_FALSE (on (p, snare, s / bf::PatternTools::stepsPerSixteenth (p)));
                }
            }

            // Nothing beyond the pattern length.
            for (int s = p.length; s < bf::kMaxSteps; ++s)
                REQUIRE_FALSE (p.tracks[(size_t) t].steps[(size_t) s].on);
        }

        if (sixteenths >= 16) // a 16-step pattern at 1/32 is only half a bar
        {
            REQUIRE (hits >= 6);
            REQUIRE (velocities.size() >= 3);
        }
    }
}

TEST_CASE ("Every generated pattern is valid DnB", "[generator]")
{
    for (int style = 0; style < bf::kNumSubStyles; ++style)
    {
        for (float variation : { 0.0f, 0.35f, 0.7f, 1.0f })
        {
            for (int length : { 16, 32 })
            {
                for (auto res : { bf::Resolution::Sixteenth, bf::Resolution::ThirtySecond })
                {
                    for (std::uint32_t seed = 1; seed <= 25; ++seed)
                    {
                        Options o;
                        o.style = (bf::SubStyle) style;
                        o.variation = variation;
                        o.length = length;
                        o.resolution = res;
                        o.seed = seed * 7919u;

                        INFO ("style " << style << " variation " << variation << " length " << length
                                       << " res " << (int) res << " seed " << o.seed);
                        const auto p = bf::PatternGenerator::generate (o);
                        REQUIRE (p.length == length);
                        REQUIRE (p.resolution == res);
                        requireValidDnB (p, o.style);
                    }
                }
            }
        }
    }
}

TEST_CASE ("Generation is deterministic per seed and varies across seeds", "[generator]")
{
    Options o;
    o.style = bf::SubStyle::Jungle;
    o.variation = 0.6f;
    o.seed = 42;

    REQUIRE (bf::PatternGenerator::generate (o) == bf::PatternGenerator::generate (o));

    std::vector<bf::Pattern> distinct;
    for (std::uint32_t seed = 1; seed <= 20; ++seed)
    {
        o.seed = seed;
        const auto p = bf::PatternGenerator::generate (o);
        if (std::find (distinct.begin(), distinct.end(), p) == distinct.end())
            distinct.push_back (p);
    }
    REQUIRE (distinct.size() > 10);
}

TEST_CASE ("Variation controls distance from the core pattern", "[generator]")
{
    const auto countHits = [] (float variation) {
        int total = 0;
        for (std::uint32_t seed = 1; seed <= 40; ++seed)
        {
            Options o;
            o.style = bf::SubStyle::Liquid;
            o.variation = variation;
            o.seed = seed;
            const auto p = bf::PatternGenerator::generate (o);
            for (int t : { kick, snare })
                for (const auto& s : p.tracks[(size_t) t].steps)
                    total += s.on ? 1 : 0;
        }
        return total;
    };

    // At zero variation, kick and snare are exactly the core two-step every time.
    REQUIRE (countHits (0.0f) == 40 * 4);
    REQUIRE (countHits (1.0f) > countHits (0.35f));
    REQUIRE (countHits (0.35f) > countHits (0.0f));
}

TEST_CASE ("Locked tracks survive Generate and per-track Generate touches one track", "[generator]")
{
    Options o;
    o.seed = 5;
    auto base = bf::PatternGenerator::generate (o);
    base.tracks[closedHat].steps[3].velocity = 11; // a hand edit to keep
    base.tracks[closedHat].steps[3].on = true;

    o.seed = 6;
    o.locked[(size_t) kick] = o.locked[(size_t) closedHat] = true;
    const auto next = bf::PatternGenerator::generate (o, base);
    REQUIRE (next.tracks[kick] == base.tracks[kick]);
    REQUIRE (next.tracks[closedHat] == base.tracks[closedHat]);

    auto p = base;
    Options single;
    single.seed = 99;
    single.variation = 1.0f;
    bf::PatternGenerator::generateTrack (p, ghost, single);

    for (int t = 0; t < bf::kNumTracks; ++t)
        if (t != ghost)
            REQUIRE (p.tracks[(size_t) t] == base.tracks[(size_t) t]);

    // Re-rolled ghosts still respect the (kept) snare.
    for (int s = 0; s < p.length; ++s)
        if (p.tracks[ghost].steps[(size_t) s].on)
            REQUIRE_FALSE (p.tracks[snare].steps[(size_t) s].on);
}

TEST_CASE ("Fills are one bar, keep the groove's first half and build at the end", "[generator][fill]")
{
    for (auto res : { bf::Resolution::Sixteenth, bf::Resolution::ThirtySecond })
    {
        for (std::uint32_t seed = 1; seed <= 30; ++seed)
        {
            Options o;
            o.seed = seed;
            o.resolution = res;
            o.length = 32;
            o.style = (bf::SubStyle) (seed % bf::kNumSubStyles);
            const auto source = bf::PatternGenerator::generate (o);
            const auto fill = bf::PatternGenerator::generateFill (source, o);

            REQUIRE (bf::PatternTools::lengthInSixteenths (fill) == 16);
            REQUIRE (on (fill, kick, 0));

            const int per16 = bf::PatternTools::stepsPerSixteenth (fill);
            for (int s = 0; s < 8 * per16; ++s)
                REQUIRE (fill.tracks[kick].steps[(size_t) s] == source.tracks[kick].steps[(size_t) s]);

            int lateSnares = 0;
            for (int s16 = 8; s16 < 16; ++s16)
                lateSnares += on (fill, snare, s16) ? 1 : 0;
            REQUIRE (lateSnares >= 4);
            REQUIRE (fill.accents[(size_t) (15 * per16)]);
        }
    }
}

TEST_CASE ("Factory patterns are musical and correctly anchored", "[generator][factory]")
{
    const auto& all = bf::FactoryPatterns::all();
    REQUIRE (all.size() == 6);

    for (const auto& e : all)
    {
        INFO (e.name);
        const auto& p = e.pattern;
        REQUIRE (on (p, kick, 0));
        REQUIRE ((on (p, snare, 4) || on (p, snare, 8)));

        std::set<int> velocities;
        for (const auto& t : p.tracks)
            for (int s = 0; s < p.length; ++s)
                if (t.steps[(size_t) s].on)
                    velocities.insert (t.steps[(size_t) s].velocity);

        REQUIRE (velocities.size() >= 3);              // not flat
        REQUIRE (velocities.count (127) <= 1);
    }

    const auto bank = bf::FactoryPatterns::makeBank();
    REQUIRE_FALSE (bank.patterns[0].isEmpty());
    REQUIRE_FALSE (bank.patterns[5].isEmpty());
    REQUIRE (bank.patterns[6].isEmpty());
}

// Hidden: prints a generated pattern per style as ASCII for eyeballing.
TEST_CASE ("Print generated patterns", "[.print]")
{
    const char* names[] = { "Kick ", "Snare", "Ghost", "CHat ", "OHat ", "Ride ", "Perc ", "FX   " };
    const auto styles = std::vector<const char*> { "Liquid", "Jump-Up", "Neuro", "Jungle", "Halftime", "Rollers", "Minimal" };

    for (int style = 0; style < (int) styles.size(); ++style)
    {
        Options o;
        o.style = (bf::SubStyle) style;
        o.seed = 2024;
        o.variation = 0.4f;
        const auto p = bf::PatternGenerator::generate (o);
        std::printf ("\n== %s ==\n", styles[(size_t) style]);
        for (int t = 0; t < bf::kNumTracks; ++t)
        {
            std::printf ("%s ", names[t]);
            for (int s = 0; s < p.length; ++s)
            {
                const auto& st = p.tracks[(size_t) t].steps[(size_t) s];
                const char c = ! st.on ? '.' : st.ratchet > 1 ? 'r' : st.velocity > 100 ? 'X' : st.velocity > 64 ? 'x' : 'o';
                std::printf ("%c%s", c, s % 4 == 3 ? " " : "");
            }
            std::printf ("\n");
        }
    }
}
