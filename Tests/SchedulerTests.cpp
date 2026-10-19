#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Core/Scheduler.h"

#include <random>

using Catch::Matchers::WithinAbs;

namespace
{
    bf::Pattern allSixteenths (int track = 0)
    {
        bf::Pattern p;
        for (auto& s : p.tracks[(size_t) track].steps)
            s.on = true;
        return p;
    }

    /** Runs the block scheduler over `totalSamples` with the given block sizes and returns the
        absolute sample position of every hit. */
    std::vector<juce::int64> runBlocks (const bf::Pattern& pattern, const bf::GrooveContext& ctx, double sr, double bpm,
                                        juce::int64 totalSamples, std::function<int()> nextBlockSize,
                                        double jitterSamples = 0.0, double startPpq = 0.0)
    {
        bf::Scheduler scheduler;
        std::array<bf::Scheduler::BlockHit, bf::Scheduler::maxHitsPerBlock> hits {};
        std::vector<juce::int64> positions;
        std::mt19937 rng (42);
        std::uniform_real_distribution<double> jitter (-jitterSamples, jitterSamples);
        const double spp = sr * 60.0 / bpm;

        for (juce::int64 pos = 0; pos < totalSamples;)
        {
            const int n = (int) std::min<juce::int64> (nextBlockSize(), totalSamples - pos);
            // Simulate a host whose reported position carries rounding noise.
            const double hostPpq = startPpq + ((double) pos + (jitterSamples > 0.0 ? jitter (rng) : 0.0)) / spp;
            const int num = scheduler.collect (pattern, ctx, hostPpq, bpm, sr, 0, n, hits, 0);

            for (int i = 0; i < num; ++i)
                positions.push_back (pos + hits[(size_t) i].sampleOffset);

            pos += n;
        }

        return positions;
    }
}

TEST_CASE ("Every step fires exactly once regardless of block size", "[scheduler]")
{
    const double sr = 44100.0, bpm = 174.0;
    const auto pattern = allSixteenths();
    const bf::GrooveContext ctx;
    const double spp = sr * 60.0 / bpm;
    const juce::int64 total = (juce::int64) (spp * 16.0); // 4 bars

    for (int blockSize : { 1, 17, 64, 127, 512, 1024, 4096 })
    {
        const auto positions = runBlocks (pattern, ctx, sr, bpm, total, [blockSize] { return blockSize; });
        REQUIRE (positions.size() == 64);

        for (size_t i = 0; i < positions.size(); ++i)
            REQUIRE (positions[i] == (juce::int64) std::lround ((double) i * 0.25 * spp));
    }
}

TEST_CASE ("Random block sizes and host position jitter neither drop nor double hits", "[scheduler]")
{
    const double sr = 48000.0, bpm = 172.0;
    auto pattern = allSixteenths();
    pattern.tracks[3].steps[5].on = true;
    pattern.tracks[3].steps[5].ratchet = 4;
    const bf::GrooveContext ctx;
    const double spp = sr * 60.0 / bpm;
    const juce::int64 total = (juce::int64) (spp * 64.0); // 16 bars

    std::mt19937 rng (7);
    std::uniform_int_distribution<int> sizes (1, 2048);
    const auto positions = runBlocks (pattern, ctx, sr, bpm, total, [&] { return sizes (rng); }, 0.4);

    // 16 bars * 16 hits on track 0, plus 16 bars * 4 ratchet hits on track 3.
    REQUIRE (positions.size() == 16 * 16 + 16 * 4);
}

TEST_CASE ("Hits shifted earlier than the block start are still delivered once", "[scheduler]")
{
    const double sr = 44100.0, bpm = 170.0;
    bf::Pattern pattern;
    pattern.tracks[0].steps[0].on = true;
    pattern.tracks[0].steps[0].nudge = -0.5f; // lands half a 16th before the bar line
    const bf::GrooveContext ctx;
    const double spp = sr * 60.0 / bpm;

    // Start a little before bar 2 so the nudged hit of bar 2 (at ppq 3.875) is inside the range.
    const auto positions = runBlocks (pattern, ctx, sr, bpm, (juce::int64) (spp * 8.0), [] { return 333; }, 0.0, 0.0);

    // Bar 1's nudged hit sits before ppq 0 and is never played; bars 2 and 3 fire early.
    REQUIRE (positions.size() == 2);
    REQUIRE (positions[0] == (juce::int64) std::lround (3.875 * spp));
    REQUIRE (positions[1] == (juce::int64) std::lround (7.875 * spp));
}

TEST_CASE ("Pattern loops relative to ppq 0 and supports 1/32 resolution", "[scheduler]")
{
    bf::Pattern p;
    p.resolution = bf::Resolution::ThirtySecond;
    p.length = 32;
    p.tracks[2].steps[1].on = true; // second 32nd

    std::vector<double> times;
    bf::scheduling::forEachHit (p, bf::GrooveContext {}, 0.0, 12.0, 174.0, [&] (const bf::HitEvent& e) { times.push_back (e.ppq); });

    REQUIRE (times.size() == 3);
    REQUIRE_THAT (times[0], WithinAbs (0.125, 1e-9));
    REQUIRE_THAT (times[1], WithinAbs (4.125, 1e-9));
    REQUIRE_THAT (times[2], WithinAbs (8.125, 1e-9));
}

TEST_CASE ("Probability is deterministic for a given seed", "[scheduler]")
{
    auto p = allSixteenths();
    for (auto& s : p.tracks[0].steps)
        s.probability = 50;

    bf::GrooveContext ctx;
    ctx.seed = 1234;

    const auto count = [&] {
        std::vector<double> t;
        bf::scheduling::forEachHit (p, ctx, 0.0, 64.0, 174.0, [&] (const bf::HitEvent& e) { t.push_back (e.ppq); });
        return t;
    };

    const auto a = count();
    const auto b = count();
    REQUIRE (a == b);
    REQUIRE (a.size() > 80);  // 256 candidates at 50 %
    REQUIRE (a.size() < 176);

    ctx.seed = 99;
    REQUIRE (count() != a);
}
