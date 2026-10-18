#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "reference/WaveformOverview.h"

using benchmark::BenchmarkProcessor;
using benchmark::LoopPoints;
using benchmark::LoopRegion;
using benchmark::ReferenceLibrary;
using benchmark::TransportState;
using Catch::Approx;
namespace ParamIDs = benchmark::ParamIDs;

TEST_CASE ("Loop points pack into one word", "[transport]")
{
    const LoopPoints p { 123456, 7654321 };
    const auto q = LoopPoints::unpack (p.pack());
    CHECK (q.start == p.start);
    CHECK (q.end == p.end);

    CHECK (LoopPoints { 10, 10 }.pack() == 0u);
    CHECK (LoopPoints { 20, 10 }.pack() == 0u);
    CHECK_FALSE (LoopPoints::unpack (0).isValid());
}

TEST_CASE ("Waveform overview holds per-bucket peak and RMS", "[overview]")
{
    constexpr int bucket = benchmark::WaveformOverview::samplesPerBucket;
    juce::AudioBuffer<float> b (2, bucket * 3 + 10);
    b.clear();

    // Bucket 0 silent, bucket 1 constant 0.5 on the left, bucket 2 a single -0.9 spike on the right.
    for (int i = bucket; i < bucket * 2; ++i)
        b.setSample (0, i, 0.5f);
    b.setSample (1, bucket * 2 + 7, -0.9f);

    const auto o = benchmark::WaveformOverview::compute (b, 48000.0);
    REQUIRE (o.getNumBuckets() == 4);
    CHECK (o.numSamples == b.getNumSamples());
    CHECK (o.peak[0] == Approx (0.0f));
    CHECK (o.peak[1] == Approx (0.5f));
    CHECK (o.rms[1] == Approx (0.5f / std::sqrt (2.0f))); // one of two channels active
    CHECK (o.peak[2] == Approx (0.9f));
    CHECK (o.getLengthSeconds() == Approx (b.getNumSamples() / 48000.0));
}

//==============================================================================
namespace
{
    struct LibraryWithTransport
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        testing::TempDir temp;
        benchmark::RealtimeHandoff<benchmark::ReferenceAudio> handoff;
        TransportState transport;
        ReferenceLibrary library { handoff, &transport };

        LibraryWithTransport()
        {
            library.setTargetSampleRate (48000.0);
            for (const auto* name : { "a.wav", "b.wav" })
            {
                const auto f = temp / name;
                testing::writeWav (f, testing::makeSine (48000.0, 10.0, 440.0, -20.0), 48000.0);
                library.addFiles ({ f });
            }
            REQUIRE (library.waitUntilLoaded (20000));
        }
    };
}

TEST_CASE ("Loops reach the audio thread for the active reference only", "[library][loop]")
{
    LibraryWithTransport f;
    REQUIRE (f.library.getActiveIndex() == 0);
    CHECK (f.transport.loop.load() == 0u);

    f.library.setLoop (0, LoopRegion { 1.0, 2.5 });
    auto loop = LoopPoints::unpack (f.transport.loop.load());
    CHECK (loop.start == 48000);
    CHECK (loop.end == 120000);

    f.library.setLoopEnabled (0, false);
    CHECK (f.transport.loop.load() == 0u);
    CHECK (f.library[0].loop.has_value()); // region kept

    f.library.setLoopEnabled (0, true);
    CHECK (f.transport.loop.load() != 0u);

    // Another reference's loop doesn't affect playback until it is active.
    f.library.setLoop (1, LoopRegion { 3.0, 4.0 });
    CHECK (LoopPoints::unpack (f.transport.loop.load()).start == 48000);
    f.library.setActive (1);
    CHECK (LoopPoints::unpack (f.transport.loop.load()).start == 144000);

    // Too short, reversed and negative inputs.
    f.library.setLoop (1, LoopRegion { 5.0, 5.01 });
    CHECK (f.library[1].loop == LoopRegion { 3.0, 4.0 });
    f.library.setLoop (1, LoopRegion { 6.0, 5.0 });
    CHECK (f.library[1].loop == LoopRegion { 5.0, 6.0 });
    f.library.setLoop (1, LoopRegion { -1.0, 0.5 });
    CHECK (f.library[1].loop == LoopRegion { 0.0, 0.5 });

    f.library.setLoop (1, std::nullopt);
    CHECK (f.transport.loop.load() == 0u);
}

TEST_CASE ("Markers recall loops and seek; everything is saved", "[library][loop][state]")
{
    LibraryWithTransport f;

    f.library.setLoop (0, LoopRegion { 1.0, 2.0 });
    f.library.setMarker (0, 0, f.library[0].loop);
    f.library.setMarker (0, 2, LoopRegion { 4.0, 6.0 });
    f.library.setLoop (0, LoopRegion { 7.0, 8.0 });
    f.library.setLoopEnabled (0, false);

    f.library.recallMarker (0, 2);
    CHECK (f.library[0].loop == LoopRegion { 4.0, 6.0 });
    CHECK (f.library[0].loopEnabled);
    CHECK (f.transport.seekRequest.load() == 4 * 48000);

    f.library.recallMarker (0, 1); // empty: no change
    CHECK (f.library[0].loop == LoopRegion { 4.0, 6.0 });

    // Round trip through saved state.
    const auto state = f.library.getStateSnapshot();
    benchmark::RealtimeHandoff<benchmark::ReferenceAudio> h2;
    TransportState t2;
    ReferenceLibrary restored (h2, &t2);
    restored.setTargetSampleRate (44100.0);
    restored.restoreState (state);

    REQUIRE (restored.size() == 2);
    CHECK (restored[0].loop == LoopRegion { 4.0, 6.0 });
    CHECK (restored[0].loopEnabled);
    CHECK (restored[0].markers[0] == LoopRegion { 1.0, 2.0 });
    CHECK_FALSE (restored[0].markers[1].has_value());
    CHECK (restored[0].markers[2] == LoopRegion { 4.0, 6.0 });

    // Loops are stored in seconds, so they follow the new host rate.
    CHECK (LoopPoints::unpack (t2.loop.load()).start == 4 * 44100);
    REQUIRE (restored.waitUntilLoaded (20000));
}

//==============================================================================
namespace
{
    struct FakePlayHead final : juce::AudioPlayHead
    {
        bool playing = false;
        int64_t time = 0;
        bool provideTime = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            if (provideTime)
                info.setTimeInSamples (time);
            return info;
        }
    };

    /** Processor with a reference whose sample value encodes its position: x[i] = i / length. */
    struct TransportFixture
    {
        static constexpr double sr = 48000.0;
        static constexpr int length = 48000;
        static constexpr int block = 256;

        juce::ScopedJuceInitialiser_GUI juceInit;
        testing::TempDir temp;
        BenchmarkProcessor processor;
        FakePlayHead playHead;

        TransportFixture()
        {
            juce::AudioBuffer<float> ramp (2, length);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < length; ++i)
                    ramp.setSample (ch, i, (float) i / (float) length);

            const auto file = temp / "ramp.wav";
            juce::WavAudioFormat wav;
            testing::writeAudioFile (file, wav, ramp, sr, 32);

            processor.setPlayHead (&playHead);
            processor.prepareToPlay (sr, block);
            processor.getReferenceLibrary().addFiles ({ file });
            REQUIRE (processor.getReferenceLibrary().waitUntilLoaded (20000));

            auto& apvts = processor.getValueTreeState();
            apvts.getParameter (ParamIDs::loudnessMatch)->setValueNotifyingHost (0.0f);
            apvts.getParameter (ParamIDs::listenRef)->setValueNotifyingHost (1.0f);
        }

        void setSync (bool sync)
        {
            processor.getValueTreeState().getParameter (ParamIDs::syncMode)->setValueNotifyingHost (sync ? 1.0f : 0.0f);
        }

        /** Runs one block of silent mix and returns the first output sample, as a reference position. */
        double runBlock()
        {
            juce::AudioBuffer<float> buffer (2, block);
            buffer.clear();
            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);

            if (playHead.playing)
                playHead.time += block;

            lastBlock = buffer;
            return (double) buffer.getSample (0, 0) * length;
        }

        juce::AudioBuffer<float> lastBlock;
    };
}

TEST_CASE ("Free mode seeks on request and ignores the host", "[transport][processor]")
{
    TransportFixture f;
    f.setSync (false);

    for (int i = 0; i < 20; ++i)
        f.runBlock();

    f.processor.getTransport().requestSeek (20000);
    f.runBlock();                              // crossfade happens here
    CHECK (f.runBlock() == Approx (20000 + TransportFixture::block).margin (0.5));
    CHECK (f.processor.getTransport().playhead.load() == 20000 + 2 * TransportFixture::block);

    // Pausing holds the position.
    f.processor.getTransport().freePlaying = false;
    f.runBlock();
    const auto held = f.processor.getTransport().playhead.load();
    f.runBlock();
    CHECK (f.processor.getTransport().playhead.load() == held);
    CHECK (f.lastBlock.getMagnitude (0, TransportFixture::block) == Approx (0.0f).margin (1e-6));
}

TEST_CASE ("Sync mode follows host play, stop and position", "[transport][processor][sync]")
{
    TransportFixture f;
    f.setSync (true);

    // Host stopped: the reference is silent.
    for (int i = 0; i < 4; ++i)
        f.runBlock();
    CHECK (f.lastBlock.getMagnitude (0, TransportFixture::block) == Approx (0.0f).margin (1e-6));
    CHECK_FALSE (f.processor.getTransport().referencePlaying.load());

    // Host plays from 10000: the reference follows sample-accurately.
    f.playHead.playing = true;
    f.playHead.time = 10000;
    for (int i = 0; i < 8; ++i)
        f.runBlock();

    auto hostTime = f.playHead.time;
    CHECK (f.runBlock() == Approx ((double) hostTime).margin (0.5));
    CHECK (f.processor.getTransport().referencePlaying.load());

    // Host jumps (e.g. cycle or locate): the reference jumps with it.
    f.playHead.time = 30000;
    f.runBlock();
    hostTime = f.playHead.time;
    CHECK (f.runBlock() == Approx ((double) hostTime).margin (0.5));

    // Host stops: fades out and holds.
    f.playHead.playing = false;
    f.runBlock();
    const auto held = f.processor.getTransport().playhead.load();
    f.runBlock();
    CHECK (f.lastBlock.getMagnitude (0, TransportFixture::block) == Approx (0.0f).margin (1e-6));
    CHECK (f.processor.getTransport().playhead.load() == held);

    // Before the reference starts there is silence, not a wrap-around.
    f.playHead.playing = true;
    f.playHead.time = 48000 * 3;
    f.runBlock();
    f.runBlock();
    CHECK (f.lastBlock.getMagnitude (0, TransportFixture::block) == Approx (0.0f).margin (1e-6));
}

TEST_CASE ("Sync falls back to Free when the host gives no position", "[transport][processor][sync]")
{
    TransportFixture f;
    f.setSync (true);
    f.playHead.provideTime = false;

    for (int i = 0; i < 10; ++i)
        f.runBlock();

    CHECK_FALSE (f.processor.getTransport().hostPositionAvailable.load());
    CHECK (f.processor.getTransport().playhead.load() == 10 * TransportFixture::block);
}
