#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dsp/ReferencePlayer.h"
#include "reference/ReferenceAudio.h"

using benchmark::ReferenceAudio;
using benchmark::dsp::ReferencePlayer;
using Catch::Approx;

namespace
{
    ReferenceAudio makeRamp (int channels, int length)
    {
        ReferenceAudio ref;
        ref.sampleRate = 48000.0;
        ref.samples.setSize (channels, length);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                ref.samples.setSample (ch, i, (float) i + (float) ch * 1000.0f);

        return ref;
    }
}

TEST_CASE ("Reference player loops at the end of the file", "[player]")
{
    const auto ref = makeRamp (2, 10);
    ReferencePlayer player;

    float l[25], r[25];
    float* out[] = { l, r };
    player.render (ref, out, 2, 25);

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
        float m[8];
        float* out[] = { m };
        player.render (ref, out, 1, 8);

        for (int i = 0; i < 8; ++i)
            REQUIRE (m[i] == Approx ((float) i + 500.0f));
    }

    SECTION ("Mono file on a stereo bus plays on both channels")
    {
        const auto ref = makeRamp (1, 8);
        ReferencePlayer player;
        float l[8], r[8];
        float* out[] = { l, r };
        player.render (ref, out, 2, 8);

        for (int i = 0; i < 8; ++i)
        {
            REQUIRE (l[i] == Approx ((float) i));
            REQUIRE (r[i] == Approx ((float) i));
        }
    }
}

TEST_CASE ("Reference player restarts when the reference changes", "[player]")
{
    const auto a = makeRamp (1, 100);
    const auto b = makeRamp (1, 100);
    ReferencePlayer player;

    player.advance (a, 40);
    CHECK (player.getPosition() == 40);

    float m[4];
    float* out[] = { m };
    player.render (b, out, 1, 4);
    CHECK (m[0] == Approx (0.0f));
    CHECK (player.getPosition() == 4);
}
