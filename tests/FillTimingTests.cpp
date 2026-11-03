#include "TestHelpers.h"

#include <catch2/generators/catch_generators.hpp>

using namespace seam::test;
using Catch::Approx;

namespace
{
    /** A kit of single-sample impulses: every hit shows up as exactly one non-zero sample (at full fill level,
        after the fade-in its value is gain/16..gain), so its position can be checked to the sample. */
    std::unique_ptr<BreakKit> impulseKit (double sampleRate, int slices, size_t sliceLength = 4000, std::uint64_t id = 7)
    {
        auto kit = std::make_unique<BreakKit>();
        kit->sampleRate = sampleRate;
        kit->id = id;
        kit->left.assign (static_cast<size_t> (slices) * sliceLength + 4, 0.0f);
        for (int i = 0; i < slices; ++i)
        {
            Slice s;
            s.start = static_cast<size_t> (i) * sliceLength;
            s.length = sliceLength;
            s.type = i == 1 ? SliceType::snare : SliceType::kick;
            kit->left[s.start] = 1.0f;
            kit->slices.push_back (s);
        }
        kit->right = kit->left;
        kit->indexByType();
        return kit;
    }

    std::unique_ptr<FillPattern> handPattern (std::vector<FillEvent> events, int bars = 1, std::uint64_t kitId = 7)
    {
        auto p = std::make_unique<FillPattern>();
        p->bars = bars;
        p->quartersPerBar = 4.0;
        p->length = bars * 4.0;
        p->kitId = kitId;
        p->events = std::move (events);
        return p;
    }

    Settings fillOnly()
    {
        Settings s;
        s.transition = 1.0f;
        s.amtReverb = 0.0f;   // keep the output = the fill alone (input is silent)
        s.phraseBars = 8;
        s.fillBars = 1;
        return s;
    }

    std::vector<size_t> onsets (const std::vector<float>& x)
    {
        std::vector<size_t> found;
        for (size_t i = 0; i < x.size(); ++i)
            if (x[i] != 0.0f && (i == 0 || x[i - 1] == 0.0f))
                found.push_back (i);
        return found;
    }
}

TEST_CASE ("Fill hits land on the host grid, sample-accurately, whatever the block size", "[fill][timing]")
{
    const std::vector<FillEvent> events { { 0.0, 0, 1.0f }, { 0.75, 0, 1.0f }, { 1.0, 1, 1.0f }, { 2.0 + 1.0 / 6.0, 0, 1.0f },
                                          { 3.0, 1, 1.0f }, { 3.5 + 1.0 / 12.0, 0, 1.0f } };
    const double bpm = GENERATE (174.0, 172.5, 140.0);
    const double sampleRate = GENERATE (44100.0, 48000.0);

    std::vector<std::vector<float>> renders;
    for (const int block : { 1, 64, 333, 512, 2048 })
    {
        SeamEngine engine;
        engine.prepare (sampleRate, 512);
        engine.publishKit (impulseKit (sampleRate, 2));
        engine.publishPattern (handPattern (events));
        FakeHost host;
        host.bpm = bpm;
        host.sampleRate = sampleRate;
        host.ppq = 26.0;   // fill window of an 8-bar phrase is ppq 28..32
        const double spq = host.samplesPerQuarter();
        Stereo silence (static_cast<size_t> (spq * 7));
        const auto out = runEngine (engine, host, silence, [] (size_t) { return fillOnly(); }, block, block > 1);
        renders.push_back (out.left);

        const auto found = onsets (out.left);
        INFO ("block " << block << ", " << bpm << " BPM, " << sampleRate << " Hz");
        REQUIRE (found.size() == events.size());
        for (size_t i = 0; i < events.size(); ++i)
        {
            const double exact = (28.0 + events[i].ppq - 26.0) * spq;   // samples from the start of the render
            CHECK (static_cast<double> (found[i]) >= exact - 1.0e-6 * spq - 1.0);
            CHECK (static_cast<double> (found[i]) < exact + 1.0);
        }
    }
    // Identical output for every block size.
    for (size_t i = 1; i < renders.size(); ++i)
    {
        size_t first = 0;
        while (first < renders[0].size() && renders[i][first] == renders[0][first]) ++first;
        INFO ("render " << i << " differs first at sample " << first << ": "
              << (first < renders[0].size() ? renders[i][first] : 0.0f) << " vs " << (first < renders[0].size() ? renders[0][first] : 0.0f));
        CHECK (first == renders[0].size());
    }
}

TEST_CASE ("Fills recur at every seam and survive loops and jumps", "[fill][timing]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    engine.publishKit (impulseKit (48000.0, 2));
    engine.publishPattern (handPattern ({ { 0.5, 0, 1.0f }, { 2.0, 1, 1.0f }, { 3.75, 0, 1.0f } }));
    FakeHost host;
    host.bpm = 170.0;
    const double spq = host.samplesPerQuarter();

    // Play 2 phrases straight: fill in bars 8 and 16.
    Stereo silence (static_cast<size_t> (spq * 64));
    const auto straight = runEngine (engine, host, silence, [] (size_t) { return fillOnly(); }, 400);
    const auto found = onsets (straight.left);
    REQUIRE (found.size() == 6);
    CHECK (std::abs (static_cast<double> (found[3]) - static_cast<double> (found[0]) - 32.0 * spq) <= 1.0);

    // Loop: 2 bars ending mid-fill (ppq 26..30.5 looped twice). The hit at 28.5 and 30.0 must play on both passes.
    std::vector<float> looped;
    for (int pass = 0; pass < 2; ++pass)
    {
        host.ppq = 26.0;   // the host jumps back
        Stereo segment (static_cast<size_t> (spq * 4.5));
        const auto out = runEngine (engine, host, segment, [] (size_t) { return fillOnly(); }, 256);
        const auto hits = onsets (out.left);
        INFO ("pass " << pass);
        REQUIRE (hits.size() == 2);
        CHECK (std::abs (static_cast<double> (hits[0]) - 2.5 * spq) <= 1.0);
        CHECK (std::abs (static_cast<double> (hits[1]) - 4.0 * spq) <= 1.0);
    }

    // Seek into the middle of a fill: only later hits play, on time.
    host.ppq = 29.0;
    Stereo rest (static_cast<size_t> (spq * 3));
    const auto afterSeek = runEngine (engine, host, rest, [] (size_t) { return fillOnly(); }, 256);
    const auto seekHits = onsets (afterSeek.left);
    REQUIRE (seekHits.size() == 2);
    CHECK (std::abs (static_cast<double> (seekHits[0]) - 1.0 * spq) <= 1.0);
}

TEST_CASE ("Every fill voice is silent from the seam's first sample", "[fill][timing]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    // One long, loud slice: a sustained tone, triggered just before the seam.
    auto kit = std::make_unique<BreakKit>();
    kit->sampleRate = 48000.0;
    kit->id = 3;
    kit->left.assign (96000, 0.0f);
    for (size_t i = 0; i < kit->left.size(); ++i)
        kit->left[i] = 0.5f * std::sin (static_cast<float> (i) * 0.05f);
    kit->right = kit->left;
    kit->slices.push_back ({ 0, 96000, SliceType::kick, 0.5f, 100.0f, 0.9f });
    kit->indexByType();
    engine.publishKit (std::move (kit));
    engine.publishPattern (handPattern ({ { 3.0, 0, 1.0f } }, 1, 3));

    FakeHost host;
    host.ppq = 30.0;
    const double spq = host.samplesPerQuarter();
    Stereo silence (static_cast<size_t> (spq * 6));
    auto s = fillOnly();
    s.amtStutter = 0.0f;
    const auto out = runEngine (engine, host, silence, [&] (size_t) { return s; }, 300);

    const auto seam = static_cast<size_t> (std::ceil (2.0 * spq));
    CHECK (rms (out.left, seam - 2000, seam - 200) > 0.1);        // playing up to the seam
    for (size_t i = seam; i < out.size(); ++i)
        REQUIRE (out.left[i] == 0.0f);                              // and silent from it on
}

TEST_CASE ("Reversed snares end just before the seam, at full level", "[fill][timing]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    engine.publishKit (impulseKit (48000.0, 2, 6000));
    FillEvent reversed { 4.0, 1, 1.0f, true, FillRole::reverse };
    engine.publishPattern (handPattern ({ reversed }));

    FakeHost host;
    host.ppq = 26.0;
    const double spq = host.samplesPerQuarter();
    Stereo silence (static_cast<size_t> (spq * 8));
    const auto out = runEngine (engine, host, silence, [] (size_t) { return fillOnly(); }, 256);

    // The impulse at the slice start plays last when reversed: it must land within ~2 ms before the seam.
    const auto seam = 6.0 * spq;
    const auto hits = onsets (out.left);
    REQUIRE (hits.size() == 1);
    CHECK (static_cast<double> (hits[0]) < seam);
    CHECK (static_cast<double> (hits[0]) > seam - 0.0025 * 48000.0);
    CHECK (out.left[hits[0]] == Approx (1.0f).margin (0.02f));
}

TEST_CASE ("Replace mutes the incoming drums in the fill window and brings them back on the seam", "[fill]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    engine.publishKit (impulseKit (48000.0, 2));
    engine.publishPattern (handPattern ({ { 0.0, 0, 0.5f } }));
    FakeHost host;
    host.ppq = 24.0;   // window 28..32
    const double spq = host.samplesPerQuarter();
    const auto input = noise (static_cast<size_t> (spq * 12), 0.3f);
    auto s = fillOnly();
    s.replace = true;
    s.transition = 0.3f;   // no stutter, little filtering
    s.amtDuck = 0.0f;
    const auto out = runEngine (engine, host, input, [&] (size_t) { return s; }, 256);

    const auto windowStart = static_cast<size_t> (4.0 * spq), seam = static_cast<size_t> (8.0 * spq);
    CHECK (rms (out.left, windowStart - 4000, windowStart - 500) > 0.1);            // before: drums
    CHECK (rms (out.left, windowStart + 500, windowStart + 4000) < 1.0e-6);         // window: muted (impulse at its start)
    CHECK (rms (out.left, seam - 4000, seam - 500) < 1.0e-6);
    CHECK (rms (out.left, seam + 10, seam + 4000) > 0.1);                           // back on the seam
    CHECK (std::abs (out.left[seam]) > 0.0f);
}

TEST_CASE ("A pattern made for another kit never plays", "[fill]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    engine.publishKit (impulseKit (48000.0, 2, 4000, 11));
    engine.publishPattern (handPattern ({ { 0.0, 0, 1.0f } }, 1, 99));
    FakeHost host;
    host.ppq = 27.0;
    Stereo silence (48000);
    const auto out = runEngine (engine, host, silence, [] (size_t) { return fillOnly(); }, 256);
    CHECK (onsets (out.left).empty());
}

TEST_CASE ("Fills follow TRANSITION: none at 0%, quiet at 10%, full from 25%", "[fill]")
{
    auto peakAt = [] (float transition)
    {
        SeamEngine engine;
        engine.prepare (48000.0, 512);
        engine.publishKit (impulseKit (48000.0, 2));
        engine.publishPattern (handPattern ({ { 1.0, 0, 1.0f } }));
        FakeHost host;
        host.ppq = 27.0;
        Stereo silence (60000);
        auto s = fillOnly();
        s.transition = transition;
        s.amtStutter = 0.0f;
        const auto out = runEngine (engine, host, silence, [&] (size_t) { return s; }, 256);
        float peak = 0.0f;
        for (const float v : out.left) peak = std::max (peak, std::abs (v));
        return peak;
    };
    CHECK (peakAt (0.0f) == 0.0f);
    CHECK (peakAt (0.1f) == Approx (0.4f / 16.0f).margin (0.01f));   // fade-in sample of a 40%-level hit
    CHECK (peakAt (0.5f) == Approx (peakAt (1.0f)));
}
