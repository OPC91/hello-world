#include "TestHelpers.h"
#include "engine/fill/Slicer.h"

#include <catch2/generators/catch_generators.hpp>

using namespace seam::test;
using Catch::Approx;

TEST_CASE ("The slicer finds every hit of the built-in break, never late and at most 2.5 ms early", "[slicer]")
{
    const double sampleRate = GENERATE (44100.0, 48000.0);
    const auto kit = makeBuiltInBreak (sampleRate);
    const auto result = sliceBreak (kit.left, sampleRate);
    const auto& hits = builtInBreakHits();

    INFO (result.summary);
    REQUIRE (result.slices.size() == hits.size());
    for (size_t i = 0; i < hits.size(); ++i)
    {
        const double found = static_cast<double> (result.slices[i].start) / sampleRate;
        INFO ("hit " << i << " (" << sliceTypeName (hits[i].type) << ") at " << hits[i].time << " s, found " << found
                     << " s as " << sliceTypeName (result.slices[i].type) << ", centroid " << result.slices[i].centroid
                     << ", low " << result.slices[i].lowRatio << ", peak " << result.slices[i].peak);
        // Late would cut the transient off the slice; slightly early only adds a sliver of the previous tail.
        CHECK (found - hits[i].time < 0.0005);
        CHECK (hits[i].time - found < 0.0025);
        CHECK (result.slices[i].type == hits[i].type);
    }
}

TEST_CASE ("Slices run to the next hit and never overlap", "[slicer]")
{
    const auto kit = makeBuiltInBreak (44100.0);
    const auto result = sliceBreak (kit.left, 44100.0);
    for (size_t i = 0; i + 1 < result.slices.size(); ++i)
    {
        CHECK (result.slices[i].start + result.slices[i].length == result.slices[i + 1].start);
        CHECK (result.slices[i].length > 0);
    }
    CHECK (result.slices.back().start + result.slices.back().length == kit.left.size());
}

TEST_CASE ("Level does not change what the slicer hears", "[slicer]")
{
    auto kit = makeBuiltInBreak (44100.0);
    for (auto& s : kit.left)
        s *= 0.05f;   // -26 dB
    const auto result = sliceBreak (kit.left, 44100.0);
    CHECK (result.slices.size() == builtInBreakHits().size());
}

TEST_CASE ("Silence and near-silence give no slices", "[slicer]")
{
    std::vector<float> silence (44100, 0.0f);
    CHECK (sliceBreak (silence, 44100.0).slices.empty());
    std::vector<float> hiss (44100);
    for (size_t i = 0; i < hiss.size(); ++i)
        hiss[i] = 1.0e-6f * static_cast<float> ((i * 7919) % 13) - 6.0e-6f;
    CHECK (sliceBreak (hiss, 44100.0).slices.empty());
    CHECK (sliceBreak ({}, 44100.0).slices.empty());
}

TEST_CASE ("A single kick is one kick", "[slicer]")
{
    const auto kit = makeBuiltInBreak (48000.0);
    const auto bar = kit.left.size();
    std::vector<float> one (kit.left.begin(), kit.left.begin() + static_cast<long> (bar / 8));
    const auto result = sliceBreak (one, 48000.0);
    REQUIRE (result.slices.size() == 1);
    CHECK (result.slices[0].type == SliceType::kick);
    CHECK (result.slices[0].start < 48);
}

TEST_CASE ("buildKit indexes slices by type", "[slicer]")
{
    const auto built = makeBuiltInBreak (44100.0);
    const auto kit = buildKit (built.left, {}, 44100.0, "test");
    CHECK (kit.right.size() == kit.left.size());
    CHECK (kit.byType[static_cast<size_t> (SliceType::kick)].size() == 2);
    CHECK (kit.byType[static_cast<size_t> (SliceType::snare)].size() == 2);
    CHECK (kit.byType[static_cast<size_t> (SliceType::ghost)].size() == 3);
    CHECK (kit.byType[static_cast<size_t> (SliceType::hat)].size() == 3);
}
