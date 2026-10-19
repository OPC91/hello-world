#include <catch2/catch_test_macros.hpp>

#include "Core/DrumSynth.h"
#include "Core/Engine.h"

TEST_CASE ("Kick fires exactly on the bar line", "[engine]")
{
    constexpr double sr = 48000.0;
    bf::Engine engine;
    engine.prepare (sr, 512);
    engine.setKick (bf::DrumSynth::renderKick (sr, {}));

    bf::TransportInfo t;
    t.isPlaying = true;
    t.bpm = 174.0;

    const double samplesPerPpq = sr * 60.0 / t.bpm;
    // Block that straddles the bar line at ppq 4: the kick must start 100 samples in.
    t.ppqStart = 4.0 - 100.0 / samplesPerPpq;

    juce::AudioBuffer<float> out (2, 512);
    out.clear();
    engine.process (out, t);

    for (int i = 0; i < 100; ++i)
        REQUIRE (out.getSample (0, i) == 0.0f);

    REQUIRE (out.getMagnitude (0, 100, 412) > 0.0f);
}
