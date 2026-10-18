#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dsp/ReferencePlayer.h"
#include "reference/ReferenceAudio.h"

#include <cmath>
#include <numbers>

using benchmark::ReferenceAudio;
using benchmark::dsp::ReferencePlayer;
using Catch::Approx;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double instantFadeMs = 1.0e-6; // one-sample fades: output equals the source exactly

    ReferenceAudio makeRamp (int channels, int length)
    {
        ReferenceAudio ref;
        ref.sampleRate = sampleRate;
        ref.samples.setSize (channels, length);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                ref.samples.setSample (ch, i, (float) i + (float) ch * 1000.0f);

        return ref;
    }

    ReferenceAudio makeSine (int length, double frequency)
    {
        ReferenceAudio ref;
        ref.sampleRate = sampleRate;
        ref.samples.setSize (1, length);

        for (int i = 0; i < length; ++i)
            ref.samples.setSample (0, i, (float) (0.5 * std::sin (2.0 * std::numbers::pi * frequency * i / sampleRate)));

        return ref;
    }

    std::vector<float> renderMono (ReferencePlayer& player, const ReferenceAudio& ref, int numSamples, int blockSize = 64)
    {
        std::vector<float> out ((size_t) numSamples);

        for (int start = 0; start < numSamples; start += blockSize)
        {
            float* ptr[] = { out.data() + start };
            player.setReference (&ref);
            player.render (ptr, 1, std::min (blockSize, numSamples - start));
        }

        return out;
    }

    float maxStep (const std::vector<float>& x, size_t from = 1)
    {
        float m = 0.0f;
        for (auto i = std::max<size_t> (from, 1); i < x.size(); ++i)
            m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    }
}

TEST_CASE ("Reference player loops at the end of the file", "[player]")
{
    const auto ref = makeRamp (2, 10);
    ReferencePlayer player;
    player.prepare (sampleRate, instantFadeMs);

    float l[25], r[25];
    float* out[] = { l, r };
    player.setReference (&ref);
    player.render (out, 2, 25);

    for (int i = 0; i < 25; ++i)
    {
        REQUIRE (l[i] == Approx ((float) (i % 10)));
        REQUIRE (r[i] == Approx ((float) (i % 10) + 1000.0f));
    }

    CHECK (player.getPosition() == 5);
}

TEST_CASE ("Reference player converts channel counts", "[player]")
{
    SECTION ("Stereo file on a mono bus plays (L + R) / 2")
    {
        const auto ref = makeRamp (2, 8);
        ReferencePlayer player;
        player.prepare (sampleRate, instantFadeMs);
        const auto m = renderMono (player, ref, 8);

        for (int i = 0; i < 8; ++i)
            REQUIRE (m[(size_t) i] == Approx ((float) i + 500.0f));
    }

    SECTION ("Mono file on a stereo bus plays on both channels")
    {
        const auto ref = makeRamp (1, 8);
        ReferencePlayer player;
        player.prepare (sampleRate, instantFadeMs);
        float l[8], r[8];
        float* out[] = { l, r };
        player.setReference (&ref);
        player.render (out, 2, 8);

        for (int i = 0; i < 8; ++i)
        {
            REQUIRE (l[i] == Approx ((float) i));
            REQUIRE (r[i] == Approx ((float) i));
        }
    }
}

TEST_CASE ("Loop region repeats exactly", "[player][loop]")
{
    const auto ref = makeRamp (1, 1000);
    ReferencePlayer player;
    player.prepare (sampleRate, instantFadeMs);
    player.setLoop (100, 150);

    // Plays 0..149, then 100..149 repeatedly.
    const auto out = renderMono (player, ref, 400, 37);

    for (int i = 0; i < 150; ++i)
        REQUIRE (out[(size_t) i] == Approx ((float) i));

    for (int i = 150; i < 400; ++i)
        REQUIRE (out[(size_t) i] == Approx ((float) (100 + (i - 150) % 50)));

    CHECK (player.getPosition() >= 100);
    CHECK (player.getPosition() <= 150); // wraps lazily at the start of the next block

    // A playhead beyond a newly set loop jumps back to the loop start.
    player.setLoop (10, 20);
    const auto after = renderMono (player, ref, 5);
    CHECK (after[0] == Approx (10.0f));
}

TEST_CASE ("Seeks and loop wraps are crossfaded, not cut", "[player][declick]")
{
    // 200 Hz sine; a jump to an arbitrary point would normally make a step.
    const auto ref = makeSine (48000, 200.0);
    ReferencePlayer player;
    player.prepare (sampleRate); // default 5 ms fades

    const auto smoothStep = (float) (0.5 * 2.0 * std::numbers::pi * 200.0 / sampleRate); // max slope of the sine itself

    SECTION ("Seek")
    {
        auto out = renderMono (player, ref, 2000);
        player.seek (12345 + 61); // lands near a peak while we are elsewhere
        const auto more = renderMono (player, ref, 2000);
        out.insert (out.end(), more.begin(), more.end());

        CHECK (maxStep (out, (size_t) player.getFadeLength()) < smoothStep * 3.0f);
    }

    SECTION ("Loop wrap at a non-zero crossing")
    {
        player.setLoop (1000, 1000 + 4321);
        const auto out = renderMono (player, ref, 20000);
        CHECK (maxStep (out, (size_t) player.getFadeLength()) < smoothStep * 3.0f);
    }
}

TEST_CASE ("Pause and resume ramp the level and hold the position", "[player]")
{
    ReferenceAudio ref;
    ref.sampleRate = sampleRate;
    ref.samples.setSize (1, 48000);
    ref.samples.clear();
    for (int i = 0; i < 48000; ++i)
        ref.samples.setSample (0, i, 0.5f);

    ReferencePlayer player;
    player.prepare (sampleRate);
    const auto fade = player.getFadeLength();

    auto out = renderMono (player, ref, 1000);
    CHECK (out[0] < 0.5f);                 // fades in on a new reference
    CHECK (out[999] == Approx (0.5f));

    player.setPlaying (false);
    out = renderMono (player, ref, 1000);
    CHECK (out[0] > 0.0f);
    CHECK (out[(size_t) fade + 1] == Approx (0.0f));
    CHECK (maxStep (out) <= 0.5f / (float) fade + 1e-6f);
    CHECK (player.isSilent());

    const auto heldAt = player.getPosition();
    renderMono (player, ref, 1000);
    CHECK (player.getPosition() == heldAt);

    player.setPlaying (true);
    out = renderMono (player, ref, 1000);
    CHECK (out[0] < 0.5f);
    CHECK (out[999] == Approx (0.5f));
    CHECK (player.getPosition() == heldAt + 1000);
}

TEST_CASE ("Without wrapping (Sync) the player is silent outside the file", "[player][sync]")
{
    const auto ref = makeRamp (1, 100);
    ReferencePlayer player;
    player.prepare (sampleRate, instantFadeMs);
    player.setWrapAtEnd (false);

    player.setReference (&ref);
    player.seek (-10);
    auto out = renderMono (player, ref, 20);
    for (int i = 0; i < 10; ++i)
        REQUIRE (out[(size_t) i] == Approx (0.0f).margin (1e-6));
    for (int i = 10; i < 20; ++i)
        REQUIRE (out[(size_t) i] == Approx ((float) (i - 10)));

    player.seek (95);
    out = renderMono (player, ref, 20);
    for (int i = 0; i < 5; ++i)
        REQUIRE (out[(size_t) i] == Approx ((float) (95 + i)));
    for (int i = 5; i < 20; ++i)
        REQUIRE (out[(size_t) i] == Approx (0.0f).margin (1e-6));

    CHECK (player.getPosition() == 115); // keeps following time past the end
}

TEST_CASE ("Reference player restarts when the reference changes", "[player]")
{
    const auto a = makeRamp (1, 100);
    const auto b = makeRamp (1, 100);
    ReferencePlayer player;
    player.prepare (sampleRate, instantFadeMs);

    renderMono (player, a, 40);
    CHECK (player.getPosition() == 40);

    const auto out = renderMono (player, b, 4);
    CHECK (out[0] == Approx (0.0f));
    CHECK (player.getPosition() == 4);
}

TEST_CASE ("A seek right after changing reference is kept", "[player]")
{
    const auto a = makeRamp (1, 100);
    const auto b = makeRamp (1, 100);
    ReferencePlayer player;
    player.prepare (sampleRate, instantFadeMs);

    renderMono (player, a, 10);
    player.setReference (&b);
    player.seek (60);

    float m[4];
    float* out[] = { m };
    player.render (out, 1, 4);
    CHECK (m[0] == Approx (60.0f));
    CHECK (player.getPosition() == 64);
}
