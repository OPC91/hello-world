#include "TestHelpers.h"

using namespace seam::test;
using Catch::Approx;

namespace
{
    Settings at (float transition)
    {
        Settings s;
        s.transition = transition;
        return s;
    }
}

TEST_CASE ("At 0% SEAM is a true bypass, bit for bit", "[bypass]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    FakeHost host;
    host.ppq = 50.0;
    const auto input = noise (48000 * 3);

    SECTION ("playing")
    {
        const auto out = runEngine (engine, host, input, [] (size_t) { return at (0.0f); });
        CHECK (firstMismatch (out, input) == input.size());
        CHECK (engine.telemetry().bypassed);
    }
    SECTION ("stopped")
    {
        const auto out = runEngine (engine, host, input, [] (size_t) { return at (0.0f); }, 256, true, false);
        CHECK (firstMismatch (out, input) == input.size());
    }
    SECTION ("every other setting turned on, still bypassed at 0%")
    {
        Settings s = at (0.0f);
        s.vacuum = true; s.tapeStop = true; s.complexity = 1.0f; s.fillBars = 4; s.replace = true;
        s.amtFilter = s.amtReverb = s.amtStutter = s.amtDuck = s.amtFill = 1.5f;
        const auto out = runEngine (engine, host, input, [&] (size_t) { return s; });
        CHECK (firstMismatch (out, input) == input.size());
    }
    SECTION ("mono channel")
    {
        std::vector<float> mono = input.left;
        float* channels[1] { mono.data() };
        for (size_t pos = 0; pos < mono.size(); pos += 300)
        {
            const int n = static_cast<int> (std::min<size_t> (300, mono.size() - pos));
            channels[0] = mono.data() + pos;
            engine.process (channels, 1, n, host.next (n), at (0.0f));
        }
        CHECK (mono == input.left);
    }
}

TEST_CASE ("Turning TRANSITION up changes the sound", "[chain]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    FakeHost host;
    const auto input = noise (48000);
    const auto out = runEngine (engine, host, input, [] (size_t) { return at (0.7f); });
    CHECK (firstMismatch (out, input) < 2000);
    CHECK_FALSE (engine.telemetry().bypassed);
    CHECK (engine.telemetry().effective == Approx (0.7f).margin (0.01f));
}

TEST_CASE ("A big drop waits for the downbeat, then snaps back to a bit-exact bypass", "[snap]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    FakeHost host;
    host.bpm = 174.0;
    const double spq = host.samplesPerQuarter();
    const auto input = noise (static_cast<size_t> (spq * 4 * 4)); // 4 bars

    // 0.8 for the first bar and a half, then 0: the drop lands mid-bar 2 and must wait for bar 3's downbeat.
    const auto dropAt = static_cast<size_t> (spq * 6.0);
    const auto downbeat = static_cast<size_t> (std::ceil (spq * 8.0));
    float effectiveBeforeDownbeat = -1.0f;
    const auto out = runEngine (engine, host, input, [&] (size_t pos)
    {
        if (pos > dropAt && pos + 300 < downbeat)
            effectiveBeforeDownbeat = engine.telemetry().effective;
        return at (pos < dropAt ? 0.8f : 0.0f);
    }, 128, false);

    CHECK (effectiveBeforeDownbeat == Approx (0.8f));
    CHECK (engine.telemetry().effective == 0.0f);

    // Still processed right up to the downbeat...
    CHECK (out.left[downbeat - 200] != input.left[downbeat - 200]);
    // ...and bit-exact again within 30 ms after it (tail cut 10 ms + crossfade 5 ms).
    CHECK (firstMismatch (out, input, downbeat + 1440) == input.size());
}

TEST_CASE ("Small decreases follow at once (fine adjustments and slow fades)", "[snap]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    FakeHost host;
    host.ppq = 1.0;
    const auto input = noise (24000);
    runEngine (engine, host, input, [] (size_t) { return at (0.6f); }, 256, false);
    runEngine (engine, host, noise (512), [] (size_t) { return at (0.57f); }, 256, false);
    CHECK (engine.telemetry().effective == Approx (0.57f));
}

TEST_CASE ("Auto ramp rises over the last N bars and drops to zero exactly on the seam", "[auto]")
{
    PhraseTracker tracker;
    const auto pos = tracker.update (playingAt (0.0), 16);
    Settings s;
    s.autoMode = true;
    s.autoBars = 4;

    CHECK (SeamEngine::autoRampAt (47.9, pos, s) == 0.0f);                     // before the ramp
    CHECK (SeamEngine::autoRampAt (48.0, pos, s) == 0.0f);                     // ramp start
    CHECK (SeamEngine::autoRampAt (56.0, pos, s) == Approx (0.5f));            // halfway (linear)
    CHECK (SeamEngine::autoRampAt (64.0 - 1.0e-6, pos, s) == Approx (1.0f).margin (1.0e-4));
    CHECK (SeamEngine::autoRampAt (64.0, pos, s) == 0.0f);                     // the seam itself
    CHECK (SeamEngine::autoRampAt (64.0 + 48.0 + 8.0, pos, s) == Approx (0.5f)); // next phrase too

    s.autoCurve = 1; // late surge holds back, then catches up
    CHECK (SeamEngine::autoRampAt (56.0, pos, s) < 0.25f);
    CHECK (SeamEngine::autoRampAt (64.0 - 1.0e-6, pos, s) == Approx (1.0f).margin (1.0e-4));

    s.autoBars = 16; // longer than an 8-bar phrase: clamped to the phrase
    const auto shortPhrase = tracker.update (playingAt (0.0), 8);
    CHECK (SeamEngine::autoRampAt (16.0, shortPhrase, s) > 0.0f);
}

TEST_CASE ("Auto mode in the engine: effect rises before the seam, bypass after it", "[auto]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    FakeHost host;
    host.ppq = 56.0; // 2 bars before the seam at 64
    const double spq = host.samplesPerQuarter();
    const auto input = noise (static_cast<size_t> (spq * 16));
    Settings s;
    s.autoMode = true;
    s.autoBars = 2;
    const auto out = runEngine (engine, host, input, [&] (size_t) { return s; }, 200);
    const auto seam = static_cast<size_t> (std::ceil (spq * 8.0));

    CHECK (out.left[seam - 1000] != input.left[seam - 1000]);
    CHECK (firstMismatch (out, input, seam + 1440) == input.size());
}

TEST_CASE ("Stopping the transport leaves no tails and no noise", "[stop]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    FakeHost host;
    const double spq = host.samplesPerQuarter();

    // Loud snare-like bursts with the effect up, so the reverb is ringing.
    auto input = noise (static_cast<size_t> (spq * 8), 0.8f);
    runEngine (engine, host, input, [] (size_t) { return at (1.0f); });

    // Stop, and feed silence: after a few ms the output must be exactly zero.
    Stereo silence (48000);
    const auto out = runEngine (engine, host, silence, [] (size_t) { return at (1.0f); }, 256, true, false);
    CHECK (firstMismatch (out, silence, 480) == silence.size());
    CHECK (engine.telemetry().bypassed);
}

TEST_CASE ("Snares throw into the reverb, and the tail rings after the hit", "[chain]")
{
    auto render = [] (float transition)
    {
        SeamEngine engine;
        engine.prepare (48000.0, 512);
        FakeHost host;
        host.ppq = 1.0;
        Stereo input (48000);
        // A snare at 0.1 s: 2 kHz-ish noise burst with a fast decay.
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        for (size_t i = 0; i < 6000; ++i)
        {
            const float env = std::exp (-static_cast<float> (i) / 1200.0f);
            const float v = 0.7f * env * (dist (rng) + std::sin (static_cast<float> (i) * 0.27f));
            input.left[4800 + i] = input.right[4800 + i] = v;
        }
        Settings s;
        s.transition = transition;
        s.amtStutter = 0.0f;
        s.amtDuck = 0.0f;
        return runEngine (engine, host, input, [&] (size_t) { return s; });
    };

    const auto dryish = render (0.05f);
    const auto thrown = render (0.9f);
    // 300-500 ms later the input is silent; only the throw's tail is left.
    CHECK (rms (dryish.left, 24000, 33600) < 1.0e-3);
    CHECK (rms (thrown.left, 24000, 33600) > 5.0e-3);
}

TEST_CASE ("Stutter repeats the start of the beat, locked to the grid", "[chain]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    FakeHost host;
    host.bpm = 120.0;           // 24000 samples per quarter, 1/32 = 3000 samples
    host.ppq = 4.0;
    const auto input = noise (24000 * 4);
    Settings s;
    s.transition = 0.9f;        // 1/32 cells
    s.amtReverb = 0.0f;
    s.amtDuck = 0.0f;
    const auto out = runEngine (engine, host, input, [&] (size_t) { return s; }, 256, false);

    // Beat 3 (samples 48000..72000): cells 2, 3 ... repeat cell 1, sample for sample (away from the cell edges).
    for (size_t cell = 1; cell < 8; ++cell)
        for (size_t i = 200; i < 2800; i += 97)
            REQUIRE (out.left[48000 + cell * 3000 + i] == out.left[48000 + i]);
    // Cell 1 itself is the live (filtered) input, not a repeat of the previous beat.
    CHECK (out.left[48000 + 500] != out.left[24000 + 500]);
}

TEST_CASE ("No clicks: rise, stutter cells, the downbeat snap and the crossfade are all smooth", "[chain]")
{
    SeamEngine engine;
    engine.prepare (48000.0, 512);
    FakeHost host;
    const double spq = host.samplesPerQuarter();
    Stereo input (static_cast<size_t> (spq * 4 * 6));
    for (size_t i = 0; i < input.size(); ++i)
        input.left[i] = input.right[i] = 0.5f * std::sin (6.283185307f * 100.0f * static_cast<float> (i) / 48000.0f);

    // Up to 95% (1/32 stutter), then yanked to 0 mid-bar 4, snapping on bar 5's downbeat.
    const auto out = runEngine (engine, host, input, [&] (size_t pos)
    {
        Settings s;
        s.transition = pos < static_cast<size_t> (spq * 14.5) ? 0.95f : 0.0f;
        return s;
    });

    float maxStep = 0.0f;
    for (size_t i = 1; i < out.size(); ++i)
        maxStep = std::max (maxStep, std::abs (out.left[i] - out.left[i - 1]));
    // A 100 Hz sine at 0.5 moves at most 0.0065 per sample; allow for the short (1.5 ms) stutter ramps.
    CHECK (maxStep < 0.05f);
}
