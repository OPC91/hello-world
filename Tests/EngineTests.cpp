#include <catch2/catch_test_macros.hpp>

#include "Core/StateIO.h"
#include "TestHelpers.h"

#include <random>

namespace
{
    struct Rig
    {
        explicit Rig (double sampleRate, int maxBlock = 512) : sr (sampleRate)
        {
            for (int t = 0; t < bf::kNumTracks; ++t)
                slots.set (t, bftest::makeClick (sr));

            engine.prepare (sr, maxBlock);
        }

        /** Renders `total` samples using the given block sizes, returning the left channel and MIDI. */
        std::vector<float> run (juce::int64 total, std::function<int()> blockSize, double bpm,
                                std::vector<std::pair<juce::int64, juce::MidiMessage>>* midiOut = nullptr,
                                std::vector<std::pair<juce::int64, juce::MidiMessage>> midiIn = {})
        {
            std::vector<float> out;
            const double spp = sr * 60.0 / bpm;

            for (juce::int64 pos = 0; pos < total;)
            {
                const int n = (int) std::min<juce::int64> (blockSize(), total - pos);
                juce::AudioBuffer<float> buffer (2, n);
                buffer.clear();
                juce::MidiBuffer midi;

                for (auto& [when, msg] : midiIn)
                    if (when >= pos && when < pos + n)
                        midi.addEvent (msg, (int) (when - pos));

                bf::TransportInfo t;
                t.isPlaying = true;
                t.hasPosition = true;
                t.bpm = bpm;
                t.ppqStart = (double) pos / spp;
                engine.process (buffer, nullptr, midi, t, params, snap);

                for (int i = 0; i < n; ++i)
                    out.push_back (buffer.getSample (0, i));

                if (midiOut != nullptr)
                    for (const auto m : midi)
                        midiOut->emplace_back (pos + m.samplePosition, m.getMessage());

                pos += n;
            }

            return out;
        }

        double sr;
        bf::SampleSlots slots;
        bf::Engine engine { slots };
        bf::EngineParams params = bftest::neutralParams();
        bf::SequencerSnapshot snap;
    };
}

TEST_CASE ("Engine renders hits on the exact sample across arbitrary block sizes", "[engine]")
{
    Rig rig (48000.0, 256);
    auto& p = rig.snap.bank.patterns[0];
    for (int s : { 0, 3, 6, 10, 14 })
        p.tracks[0].steps[(size_t) s].on = true;

    const double bpm = 174.0, spp = rig.sr * 60.0 / bpm;
    std::mt19937 rng (1);
    std::uniform_int_distribution<int> sizes (1, 3000); // includes blocks larger than prepared

    const auto audio = rig.run ((juce::int64) (spp * 8.0), [&] { return sizes (rng); }, bpm);
    const auto onsets = bftest::findOnsets (audio);

    std::vector<juce::int64> expected;
    for (int bar = 0; bar < 2; ++bar)
        for (int s : { 0, 3, 6, 10, 14 })
            expected.push_back ((juce::int64) std::lround ((bar * 4.0 + s * 0.25) * spp));

    REQUIRE (onsets == expected);
}

TEST_CASE ("MIDI out mirrors the sequence with note-offs", "[engine][midi]")
{
    Rig rig (44100.0);
    rig.params.midiOut = true;
    auto& p = rig.snap.bank.patterns[0];
    p.tracks[0].steps[0].on = true;
    p.tracks[1].steps[4].on = true;
    p.tracks[1].steps[4].velocity = 90;

    const double bpm = 170.0, spp = rig.sr * 60.0 / bpm;
    std::vector<std::pair<juce::int64, juce::MidiMessage>> midi;
    rig.run ((juce::int64) (spp * 4.0), [] { return 300; }, bpm, &midi);

    std::vector<std::pair<juce::int64, juce::MidiMessage>> ons;
    int offs = 0;
    for (auto& [t, m] : midi)
    {
        if (m.isNoteOn()) ons.emplace_back (t, m);
        if (m.isNoteOff()) ++offs;
    }

    REQUIRE (ons.size() == 2);
    REQUIRE (offs == 2);
    REQUIRE (ons[0].first == 0);
    REQUIRE (ons[0].second.getNoteNumber() == 36);
    REQUIRE (ons[1].first == (juce::int64) std::lround (1.0 * spp));
    REQUIRE (ons[1].second.getNoteNumber() == 38);
    REQUIRE (ons[1].second.getVelocity() == 90);
    REQUIRE (ons[1].second.getChannel() == 10);
}

TEST_CASE ("MIDI notes switch patterns sample-accurately", "[engine][midi]")
{
    Rig rig (44100.0);
    // Pattern 1: hit on every 16th of track 0. Pattern 2: empty.
    for (auto& s : rig.snap.bank.patterns[0].tracks[0].steps)
        s.on = true;

    const double bpm = 174.0, spp = rig.sr * 60.0 / bpm;
    // Switch to pattern 2 (D#1... note 37) exactly at beat 2, back to pattern 1 at beat 3.
    std::vector<std::pair<juce::int64, juce::MidiMessage>> in {
        { (juce::int64) std::lround (1.0 * spp), juce::MidiMessage::noteOn (1, 37, (juce::uint8) 100) },
        { (juce::int64) std::lround (2.0 * spp), juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100) },
    };

    const auto audio = rig.run ((juce::int64) (spp * 4.0), [] { return 512; }, bpm, nullptr, in);
    const auto onsets = bftest::findOnsets (audio);

    // Beat 1: 4 hits, beat 2: none, beats 3-4: 8 hits.
    REQUIRE (onsets.size() == 12);
    REQUIRE (onsets[4] == (juce::int64) std::lround (2.0 * spp));
}

TEST_CASE ("Hold mode silences the sequencer when no pattern note is held", "[engine][midi]")
{
    Rig rig (44100.0);
    rig.params.midiPatternMode = bf::MidiPatternMode::Hold;
    for (auto& s : rig.snap.bank.patterns[0].tracks[0].steps)
        s.on = true;

    const double bpm = 174.0, spp = rig.sr * 60.0 / bpm;
    std::vector<std::pair<juce::int64, juce::MidiMessage>> in {
        { 0, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100) },
        { (juce::int64) std::lround (1.0 * spp), juce::MidiMessage::noteOff (1, 36) },
    };

    const auto onsets = bftest::findOnsets (rig.run ((juce::int64) (spp * 4.0), [] { return 128; }, bpm, nullptr, in));
    REQUIRE (onsets.size() == 4);
}

TEST_CASE ("Choke group cuts the open hat when the closed hat plays", "[engine]")
{
    Rig rig (44100.0);
    // Long constant sample on the open hat so we can see it being cut.
    juce::AudioBuffer<float> longSample (1, 44100);
    longSample.clear();
    for (int i = 0; i < longSample.getNumSamples(); ++i)
        longSample.setSample (0, i, 0.5f);
    rig.slots.set (4, new bf::SampleData (std::move (longSample), 44100.0, "drone"));
    rig.slots.set (3, new bf::SampleData (juce::AudioBuffer<float> (1, 1), 44100.0, "silent"));
    rig.params.tracks[3].chokeGroup = 1;
    rig.params.tracks[4].chokeGroup = 1;

    auto& p = rig.snap.bank.patterns[0];
    p.tracks[4].steps[0].on = true; // open hat
    p.tracks[3].steps[2].on = true; // closed hat chokes it

    const double bpm = 174.0, spp = rig.sr * 60.0 / bpm;
    const auto audio = rig.run ((juce::int64) (spp * 1.0), [] { return 64; }, bpm);
    const auto chokeAt = (size_t) std::lround (0.5 * spp);

    REQUIRE (std::abs (audio[chokeAt - 10]) > 0.3f);
    REQUIRE (std::abs (audio[chokeAt + 2000]) < 0.001f);
}

TEST_CASE ("Pattern bank survives serialisation", "[state]")
{
    bf::PatternBank bank;
    auto& p = bank.patterns[5];
    p.length = 32;
    p.resolution = bf::Resolution::ThirtySecond;
    p.accents[3] = true;
    auto& s = p.tracks[6].steps[17];
    s.on = true;
    s.velocity = 33;
    s.probability = 60;
    s.ratchet = 3;
    s.nudge = -0.25f;
    s.pitch = -7;
    s.length = 2.5f;
    bank.patterns[0].tracks[0].steps[0].on = true;

    const auto restored = bf::StateIO::bankFromTree (bf::StateIO::bankToTree (bank));
    REQUIRE (restored == bank);

    // And through XML text, as the host stores it.
    const auto xml = bf::StateIO::bankToTree (bank).toXmlString();
    REQUIRE (bf::StateIO::bankFromTree (juce::ValueTree::fromXml (xml)) == bank);
}
