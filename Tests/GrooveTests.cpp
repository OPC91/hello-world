#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Core/Engine.h"
#include "Core/PatternTools.h"
#include "Core/UserLibrary.h"
#include "TestHelpers.h"

using Catch::Matchers::WithinAbs;

namespace
{
    std::vector<bf::HitEvent> hitsOf (const bf::Pattern& p, const bf::GrooveContext& ctx, double from, double to, double bpm = 174.0)
    {
        std::vector<bf::HitEvent> out;
        bf::scheduling::forEachHit (p, ctx, from, to, bpm, [&] (const bf::HitEvent& e) { out.push_back (e); });
        std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.ppq < b.ppq || (a.ppq == b.ppq && a.track < b.track); });
        return out;
    }
}

TEST_CASE ("Swing warp places the off-beat 16th at swing x 8th", "[groove][swing]")
{
    using bf::groove::warp;

    // Straight swing is the identity.
    for (double x = 0.0; x < 4.0; x += 0.0625)
        REQUIRE_THAT (warp (x, 0.5, 0.5), WithinAbs (x, 1e-12));

    // 66 %: the second 16th of each 8th moves to 0.66 of the 8th.
    REQUIRE_THAT (warp (0.25, 0.66, 0.5), WithinAbs (0.33, 1e-12));
    REQUIRE_THAT (warp (0.75, 0.66, 0.5), WithinAbs (0.83, 1e-12));
    // On-beat 8ths never move.
    REQUIRE_THAT (warp (0.5, 0.66, 0.5), WithinAbs (0.5, 1e-12));
    REQUIRE_THAT (warp (3.0, 0.75, 0.5), WithinAbs (3.0, 1e-12));
    // 32nd positions are warped proportionally (ratchets/1/32 mode stay in order).
    REQUIRE_THAT (warp (0.375, 0.66, 0.5), WithinAbs (0.33 + 0.125 * (0.17 / 0.25), 1e-12));

    // Monotonic for every swing amount, so swung events never swap order.
    for (double s = 0.5; s <= 0.75; s += 0.05)
    {
        double prev = -1.0;
        for (double x = 0.0; x < 2.0; x += 1.0 / 64.0)
        {
            const double w = bf::groove::applySwing (x, s, true);
            REQUIRE (w > prev);
            prev = w;
        }
    }
}

TEST_CASE ("Swing is computed in samples relative to the host tempo", "[groove][swing]")
{
    bf::Pattern p;
    p.tracks[0].steps[1].on = true; // off-beat 16th

    bf::GrooveContext ctx;
    ctx.swing.fill (0.62f);
    const double sr = 48000.0;

    for (double bpm : { 87.0, 140.0, 174.0, 180.0 })
    {
        const auto hits = hitsOf (p, ctx, 0.0, 4.0, bpm);
        REQUIRE (hits.size() == 1);

        const double spp = sr * 60.0 / bpm;
        const double delaySamples = (hits[0].ppq - 0.25) * spp;
        // Delay is (swing - 0.5) of an 8th note at this tempo, whatever the tempo is.
        REQUIRE_THAT (delaySamples, WithinAbs (((double) 0.62f - 0.5) * 0.5 * spp, 1e-6));
    }
}

TEST_CASE ("Optional 8th swing also delays the off-beat 8th", "[groove][swing]")
{
    bf::Pattern p;
    p.tracks[0].steps[2].on = true; // the "and"

    bf::GrooveContext ctx;
    ctx.swing.fill (0.6f);
    REQUIRE_THAT (hitsOf (p, ctx, 0.0, 1.0)[0].ppq, WithinAbs (0.5, 1e-12));

    ctx.swingEighths = true;
    REQUIRE_THAT (hitsOf (p, ctx, 0.0, 1.0)[0].ppq, WithinAbs (0.6, 1e-6));
}

TEST_CASE ("Per-track swing override swings hats over a straight kick", "[groove][swing]")
{
    bf::EngineParams params;
    params.swing = 0.5f;
    params.tracks[3].swingOverride = true;
    params.tracks[3].swing = 0.65f;

    bf::SequencerSnapshot snap;
    auto& p = snap.bank.patterns[0];
    p.tracks[0].steps[1].on = true; // kick off-beat
    p.tracks[3].steps[1].on = true; // hat off-beat

    const auto ctx = bf::Engine::makeGrooveContext (params, snap);
    const auto hits = hitsOf (p, ctx, 0.0, 1.0);
    REQUIRE (hits.size() == 2);
    REQUIRE (hits[0].track == 0);
    REQUIRE_THAT (hits[0].ppq, WithinAbs (0.25, 1e-12));
    REQUIRE (hits[1].track == 3);
    REQUIRE_THAT (hits[1].ppq, WithinAbs (0.325, 1e-6));
}

TEST_CASE ("Humanize stays within its range and is repeatable until re-rolled", "[groove][humanize]")
{
    bf::Pattern p;
    for (auto& s : p.tracks[3].steps)
    {
        s.on = true;
        s.velocity = 100;
    }

    bf::GrooveContext ctx;
    ctx.humanizeTimeMs = 20.0f;
    ctx.humanizeVel = 0.3f;
    ctx.seed = 77;

    const double bpm = 174.0;
    // The range edges sit between grid steps, further away than 20 ms, so the count is exact.
    const auto a = hitsOf (p, ctx, 0.625, 63.625, bpm);
    REQUIRE (a.size() == 252);

    double maxDev = 0.0;
    bool anyMoved = false;

    for (const auto& h : a)
    {
        const double grid = std::round (h.ppq * 4.0) / 4.0;
        const double devMs = std::abs (h.ppq - grid) * 60000.0 / bpm;
        maxDev = std::max (maxDev, devMs);
        anyMoved = anyMoved || devMs > 0.5;
        REQUIRE (h.velocity >= 100.0f / 127.0f * 0.7f - 1e-5f);
        REQUIRE (h.velocity <= 1.0f);
    }

    REQUIRE (maxDev <= 20.0 + 1e-6);
    REQUIRE (anyMoved);

    // Same seed -> identical playback.
    const auto b = hitsOf (p, ctx, 0.625, 63.625, bpm);
    REQUIRE (a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE (a[i].ppq == b[i].ppq);
        REQUIRE (a[i].velocity == b[i].velocity);
    }

    // New seed -> a different feel.
    ctx.seed = 78;
    const auto c = hitsOf (p, ctx, 0.625, 63.625, bpm);
    bool differs = false;
    for (size_t i = 0; i < std::min (a.size(), c.size()); ++i)
        differs = differs || a[i].ppq != c[i].ppq;
    REQUIRE (differs);
}

TEST_CASE ("Velocity depth maps velocity to gain", "[groove][velocity]")
{
    using bf::groove::velocityToGain;
    REQUIRE_THAT (velocityToGain (1.0f, 1.0f), WithinAbs (1.0, 1e-6));
    REQUIRE_THAT (velocityToGain (0.3f, 0.0f), WithinAbs (1.0, 1e-6)); // depth 0 ignores velocity
    REQUIRE_THAT (velocityToGain (0.5f, 1.0f), WithinAbs (0.25, 1e-6));

    float prev = -1.0f;
    for (float v = 0.0f; v <= 1.0f; v += 0.01f)
    {
        const float g = velocityToGain (v, 0.8f);
        REQUIRE (g >= prev);
        prev = g;
    }
}

TEST_CASE ("Groove templates shift timing and shape velocity", "[groove][template]")
{
    bf::Pattern p;
    p.tracks[1].steps[4].on = true; // backbeat snare
    p.tracks[1].steps[4].velocity = 127;
    p.tracks[3].steps[1].on = true;
    p.tracks[3].steps[1].velocity = 127;

    bf::GrooveContext ctx;
    ctx.grooveTemplate = bf::groove::factoryTemplate (3); // Amen Feel
    const auto hits = hitsOf (p, ctx, 0.0, 4.0);
    REQUIRE (hits.size() == 2);

    const auto& tmpl = bf::groove::factoryTemplate (3);
    REQUIRE_THAT (hits[0].ppq, WithinAbs (0.25 + tmpl.timing[1] * 0.25, 1e-9));
    REQUIRE_THAT (hits[0].velocity, WithinAbs (tmpl.velocity[1], 1e-6));
    REQUIRE_THAT (hits[1].ppq, WithinAbs (1.0 + tmpl.timing[4] * 0.25, 1e-9));
    REQUIRE (hits[1].ppq > 1.0); // the Amen backbeat sits late

    // Every factory template keeps offsets within half a step and sensible swing.
    for (int i = 0; i < bf::groove::numFactoryTemplates; ++i)
    {
        const auto& t = bf::groove::factoryTemplate (i);
        REQUIRE (t.swing >= 0.5f);
        REQUIRE (t.swing <= 0.75f);
        for (auto o : t.timing)   REQUIRE (std::abs (o) <= 0.5f);
        for (auto v : t.velocity) REQUIRE ((v > 0.0f && v <= 1.0f));
    }
}

TEST_CASE ("Accent row boosts velocity on every track", "[groove][accent]")
{
    bf::Pattern p;
    for (int t : { 0, 3 })
    {
        p.tracks[(size_t) t].steps[0].on = p.tracks[(size_t) t].steps[2].on = true;
        p.tracks[(size_t) t].steps[0].velocity = p.tracks[(size_t) t].steps[2].velocity = 80;
    }
    p.accents[2] = true;

    bf::GrooveContext ctx;
    ctx.accentAmount = 0.4f;
    const auto hits = hitsOf (p, ctx, 0.0, 1.0);
    REQUIRE (hits.size() == 4);
    REQUIRE_THAT (hits[0].velocity, WithinAbs (80.0 / 127.0, 1e-6));
    REQUIRE_THAT (hits[2].velocity, WithinAbs (80.0 / 127.0 * 1.4, 1e-6));
    REQUIRE_THAT (hits[3].velocity, WithinAbs (80.0 / 127.0 * 1.4, 1e-6));
}

TEST_CASE ("Ghost-note helper adds soft snares in DnB positions", "[groove][ghost]")
{
    for (std::uint32_t seed : { 1u, 2u, 3u, 99u, 1234u })
    {
        bf::Pattern p;
        p.length = 32;
        p.tracks[1].steps[4].on = p.tracks[1].steps[12].on = true;
        p.tracks[1].steps[20].on = p.tracks[1].steps[28].on = true;

        bf::PatternTools::addGhostNotes (p, 2, 1, seed);
        const auto& ghosts = p.tracks[2].steps;

        // The core ghosts (the "a" of 2 and the "and" of 4) are always there, in both bars.
        REQUIRE (ghosts[7].on);
        REQUIRE (ghosts[23].on);
        REQUIRE (ghosts[14].on);
        REQUIRE (ghosts[30].on);

        int count = 0;
        for (int i = 0; i < 32; ++i)
        {
            if (! ghosts[(size_t) i].on)
                continue;

            ++count;
            REQUIRE (ghosts[(size_t) i].velocity <= 46);          // ghosts, not hits
            REQUIRE (ghosts[(size_t) i].velocity >= 20);
            REQUIRE_FALSE (p.tracks[1].steps[(size_t) i].on);      // never on top of the backbeat
            REQUIRE (i % 16 != 0);                                  // never on the downbeat
        }

        REQUIRE (count >= 4);
        REQUIRE (count <= 14);
    }
}

TEST_CASE ("Pattern shift and resolution changes are reversible", "[pattern]")
{
    bf::Pattern p;
    p.tracks[0].steps[0].on = true;
    p.tracks[3].steps[5].on = true;
    p.tracks[3].steps[5].nudge = 0.2f;
    p.accents[5] = true;

    auto q = p;
    bf::PatternTools::shift (q, 3);
    REQUIRE (q.tracks[0].steps[3].on);
    REQUIRE (q.tracks[3].steps[8].on);
    REQUIRE (q.accents[8]);
    bf::PatternTools::shift (q, -3);
    REQUIRE (q == p);

    auto r = p;
    bf::PatternTools::setLengthAndResolution (r, 32, bf::Resolution::ThirtySecond);
    REQUIRE (r.tracks[3].steps[10].on);
    REQUIRE_THAT (r.tracks[3].steps[10].nudge, WithinAbs (0.4, 1e-6));
    bf::PatternTools::setLengthAndResolution (r, 16, bf::Resolution::Sixteenth);
    REQUIRE (r == p);

    // Off-grid 32nds become a nudged 16th, or a roll if the 16th is taken.
    bf::Pattern fine;
    fine.resolution = bf::Resolution::ThirtySecond;
    fine.length = 32;
    fine.tracks[3].steps[2].on = fine.tracks[3].steps[3].on = true;
    fine.tracks[3].steps[7].on = true;
    bf::PatternTools::setLengthAndResolution (fine, 16, bf::Resolution::Sixteenth);
    REQUIRE (fine.tracks[3].steps[1].ratchet == 2);
    REQUIRE (fine.tracks[3].steps[3].on);
    REQUIRE_THAT (fine.tracks[3].steps[3].nudge, WithinAbs (0.5, 1e-6));
}

TEST_CASE ("User grooves round-trip through the library folder", "[groove][template]")
{
    const auto dir = juce::File::createTempFile ("bf_lib");
    bf::UserLibrary::setRootOverride (dir);

    bf::GrooveTemplate t = bf::groove::factoryTemplate (3);
    t.swing = 0.58f;
    t.timing[5] = -0.12f;
    REQUIRE (bf::UserLibrary::saveGroove ("My Break", t));

    const auto files = bf::UserLibrary::listGrooves();
    REQUIRE (files.size() == 1);

    bf::GrooveTemplate loaded;
    REQUIRE (bf::UserLibrary::loadGroove (files[0], loaded));
    REQUIRE_THAT (loaded.swing, WithinAbs (0.58, 1e-4));
    for (size_t i = 0; i < 16; ++i)
    {
        REQUIRE_THAT (loaded.timing[i], WithinAbs (t.timing[i], 1e-4));
        REQUIRE_THAT (loaded.velocity[i], WithinAbs (t.velocity[i], 1e-4));
    }

    dir.deleteRecursively();
    bf::UserLibrary::setRootOverride ({});
}
