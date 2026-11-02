#include "TestHelpers.h"

#include <catch2/generators/catch_generators.hpp>

using namespace seam::test;
using Catch::Approx;

namespace
{
    Settings vacuumOn (double gap, bool tape = false)
    {
        Settings s;
        s.transition = 0.3f;     // below the stutter
        s.vacuum = true;
        s.vacuumQuarters = gap;
        s.tapeStop = tape;
        s.amtReverb = 0.0f;
        s.amtDuck = 0.0f;
        s.phraseBars = 8;
        return s;
    }
}

TEST_CASE ("Vacuum: true silence that ends exactly on the seam's downbeat", "[vacuum]")
{
    const double gap = GENERATE (0.25, 0.5, 1.0);
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    FakeHost host;
    host.ppq = 24.0;   // seam at 32
    const double spq = host.samplesPerQuarter();
    const auto input = noise (static_cast<size_t> (spq * 12));
    const auto out = runEngine (engine, host, input, [&] (size_t) { return vacuumOn (gap); }, 300);

    const auto seam = static_cast<size_t> (std::ceil (8.0 * spq - 1.0e-6));
    const auto gapStart = static_cast<size_t> (std::ceil ((8.0 - gap) * spq - 1.0e-6));
    INFO ("gap " << gap << " quarters");
    CHECK (rms (out.left, gapStart - 3000, gapStart - 100) > 0.05);   // audio before the gap
    for (size_t i = gapStart + 80; i < seam; ++i)                       // silent after a 1.5 ms fade
        REQUIRE (out.left[i] == 0.0f);
    CHECK (out.left[seam] != 0.0f);                                      // back on the downbeat's first sample
    CHECK (rms (out.left, seam + 20, seam + 3000) > 0.05);
}

TEST_CASE ("Vacuum does nothing at 0% (true bypass), and nothing between gaps", "[vacuum]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    FakeHost host;
    host.ppq = 24.0;
    const auto input = noise (static_cast<size_t> (host.samplesPerQuarter() * 12));
    auto s = vacuumOn (1.0, true);
    s.transition = 0.0f;
    const auto out = runEngine (engine, host, input, [&] (size_t) { return s; }, 300);
    CHECK (firstMismatch (out, input) == input.size());
}

TEST_CASE ("Tape stop slows the final beat to a halt before the gap", "[vacuum]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    FakeHost host;
    host.ppq = 24.0;
    host.bpm = 120.0;   // 24000 samples per beat
    const double spq = host.samplesPerQuarter();
    Stereo input (static_cast<size_t> (spq * 10));
    for (size_t i = 0; i < input.size(); ++i)
        input.left[i] = input.right[i] = 0.5f * std::sin (6.2831853f * 440.0f * static_cast<float> (i) / 48000.0f);

    auto s = vacuumOn (0.25, true);
    s.amtFilter = 0.0f;
    const auto out = runEngine (engine, host, input, [&] (size_t) { return s; }, 256);

    // Final beat before the gap: ppq 30.75..31.75 = samples 162000..186000.
    auto crossings = [&] (size_t from, size_t to)
    {
        int count = 0;
        for (size_t i = from + 1; i < to; ++i)
            count += (out.left[i - 1] < 0.0f) != (out.left[i] < 0.0f) ? 1 : 0;
        return count;
    };
    const int early = crossings (162000, 168000);
    const int late = crossings (180000, 186000);
    CHECK (early > 3 * late);      // pitch (and speed) falling
    CHECK (late < 30);
    // Before the tape stop the sine is untouched in pitch.
    CHECK (crossings (150000, 156000) == Approx (crossings (162000 - 12000, 162000 - 6000)).margin (2));
}

TEST_CASE ("With the vacuum on, the fill finishes where the gap starts", "[vacuum][fill]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    auto kit = std::make_unique<BreakKit>();
    kit->sampleRate = 48000.0;
    kit->id = 5;
    kit->left.assign (8000, 0.0f);
    kit->left[0] = 1.0f;
    kit->right = kit->left;
    kit->slices.push_back ({ 0, 6000, SliceType::snare, 1.0f, 2000.0f, 0.0f });
    kit->indexByType();
    engine.publishKit (std::move (kit));
    auto pattern = std::make_unique<FillPattern>();
    pattern->length = 4.0;
    pattern->kitId = 5;
    pattern->events = { { 3.75, 0, 1.0f }, { 4.0, 0, 1.0f, true, FillRole::reverse } };   // a hit inside the gap, and a reverse
    engine.publishPattern (std::move (pattern));

    FakeHost host;
    host.ppq = 26.0;
    const double spq = host.samplesPerQuarter();
    Stereo silence (static_cast<size_t> (spq * 8));
    auto s = vacuumOn (0.5);
    s.amtStutter = 0.0f;
    s.transition = 1.0f;
    const auto out = runEngine (engine, host, silence, [&] (size_t) { return s; }, 256);

    // The reversed impulse lands just before the gap (ppq 31.5 = 5.5 quarters in); nothing after it.
    const auto gapStart = 5.5 * spq;
    std::vector<size_t> hits;
    for (size_t i = 0; i < out.size(); ++i)
        if (out.left[i] != 0.0f && (i == 0 || out.left[i - 1] == 0.0f)) hits.push_back (i);
    REQUIRE (hits.size() == 1);
    CHECK (static_cast<double> (hits[0]) < gapStart);
    CHECK (static_cast<double> (hits[0]) > gapStart - 0.003 * 48000.0);
}
