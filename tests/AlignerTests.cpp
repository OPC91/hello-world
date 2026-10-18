#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "reference/Aligner.h"
#include "PluginProcessor.h"
#include "Parameters.h"

#include <juce_dsp/juce_dsp.h>

using benchmark::Aligner;
using Catch::Approx;

namespace
{
    constexpr double sr = 48000.0;

    /** Noise shaped by a rhythmic envelope: a stand-in for a song. */
    juce::AudioBuffer<float> songLike (double seconds, int seed)
    {
        juce::Random rng (seed);
        juce::AudioBuffer<float> b (2, (int) (seconds * sr));
        float lp = 0.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto t = i / sr;
            const auto env = 0.3 + 0.7 * std::pow (std::abs (std::sin (t * 4.1 + seed)), 4.0);
            lp += 0.3f * ((rng.nextFloat() * 2.0f - 1.0f) - lp);
            b.setSample (0, i, (float) env * lp);
            b.setSample (1, i, (float) env * lp * 0.8f + 0.05f * (rng.nextFloat() - 0.5f));
        }
        return b;
    }
}

TEST_CASE ("Aligner finds an EQ'd, noisy excerpt to the sample", "[aligner]")
{
    const auto reference = songLike (60.0, 1);
    const int truePosition = (int) (23.4567 * sr);
    const int length = (int) (15.0 * sr);

    // A "different version": low-passed (forwards then backwards, so no delay), quieter, with added noise.
    std::vector<float> excerpt ((size_t) length);
    for (int i = 0; i < length; ++i)
        excerpt[(size_t) i] = 0.5f * (reference.getSample (0, truePosition + i) + reference.getSample (1, truePosition + i));

    juce::dsp::IIR::Filter<float> lowPass;
    lowPass.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 3000.0f);
    for (auto& x : excerpt)
        x = lowPass.processSample (x);
    lowPass.reset();
    for (auto it = excerpt.rbegin(); it != excerpt.rend(); ++it)
        *it = lowPass.processSample (*it);

    juce::Random noise (99);
    for (auto& x : excerpt)
        x = 0.5f * x + 0.01f * (noise.nextFloat() - 0.5f);

    const auto result = Aligner::findExcerpt (reference, excerpt.data(), length, sr);
    INFO (result.message);
    CHECK (result.found);
    CHECK (std::abs (result.referencePosition - truePosition) <= 1);
    CHECK (result.confidence > 0.6f);
}

TEST_CASE ("Aligner reports when there is no match", "[aligner]")
{
    const auto reference = songLike (40.0, 1);
    const auto unrelated = songLike (15.0, 7);
    std::vector<float> excerpt ((size_t) unrelated.getNumSamples());
    for (int i = 0; i < unrelated.getNumSamples(); ++i)
        excerpt[(size_t) i] = unrelated.getSample (0, i);

    const auto result = Aligner::findExcerpt (reference, excerpt.data(), (int) excerpt.size(), sr);
    CHECK_FALSE (result.found);
    CHECK (result.confidence < Aligner::minimumConfidence);

    const auto tooShort = Aligner::findExcerpt (reference, excerpt.data(), (int) (sr * 1.0), sr);
    CHECK_FALSE (tooShort.found);
}

TEST_CASE ("Align in Sync mode sets the reference offset", "[aligner][processor][sync]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    testing::TempDir temp;

    struct PlayHead final : juce::AudioPlayHead
    {
        int64_t time = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (true);
            info.setTimeInSamples (time);
            return info;
        }
    } playHead;

    const auto song = songLike (40.0, 3);
    const auto file = temp / "song.wav";
    juce::WavAudioFormat wav;
    testing::writeAudioFile (file, wav, song, sr, 32);

    benchmark::BenchmarkProcessor processor;
    processor.setPlayHead (&playHead);
    processor.prepareToPlay (sr, 512);
    processor.getReferenceLibrary().addFiles ({ file });
    REQUIRE (processor.getReferenceLibrary().waitUntilLoaded (20000));
    processor.getValueTreeState().getParameter (benchmark::ParamIDs::syncMode)->setValueNotifyingHost (1.0f);

    // In the session, the song starts 5 s into the reference: session time t plays reference t + 5 s.
    const int64_t trueOffset = (int64_t) (5.0 * sr);
    playHead.time = 100000;

    REQUIRE (processor.startAlignment());
    CHECK (processor.getAlignmentStatus().state == benchmark::AlignmentStatus::State::listening);

    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    const auto blocksNeeded = (int) (benchmark::BenchmarkProcessor::alignmentCaptureSeconds * sr / 512) + 4;

    for (int b = 0; b < blocksNeeded; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                block.setSample (ch, i, song.getSample (ch, (int) (playHead.time + trueOffset + i)));

        processor.processBlock (block, midi);
        playHead.time += 512;
    }

    const auto deadline = juce::Time::getMillisecondCounter() + 60000;
    while (processor.getAlignmentStatus().state != benchmark::AlignmentStatus::State::done
           && processor.getAlignmentStatus().state != benchmark::AlignmentStatus::State::failed
           && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    INFO (processor.getAlignmentStatus().message);
    REQUIRE (processor.getAlignmentStatus().state == benchmark::AlignmentStatus::State::done);

    const auto& entry = processor.getReferenceLibrary()[0];
    CHECK (std::abs (entry.syncOffsetSeconds * sr - (double) trueOffset) <= 1.0);
    CHECK (entry.alignmentConfidence > 0.9f);
    CHECK (std::abs (processor.getTransport().syncOffset.load() - trueOffset) <= 1);

    // Saved with the session.
    const auto state = processor.getReferenceLibrary().getStateSnapshot();
    CHECK ((double) state.getChild (0).getProperty ("syncOffset") == Approx (5.0).margin (1.0 / sr));
}
