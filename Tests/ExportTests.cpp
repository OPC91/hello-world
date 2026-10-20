#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Core/Exporter.h"
#include "Core/MasterBus.h"
#include "TestHelpers.h"

using Catch::Matchers::WithinAbs;

namespace
{
    juce::MidiFile roundTrip (const juce::MidiFile& f)
    {
        juce::MemoryOutputStream out;
        REQUIRE (f.writeTo (out, 1));
        juce::MemoryInputStream in (out.getData(), out.getDataSize(), false);
        juce::MidiFile back;
        REQUIRE (back.readFrom (in));
        return back;
    }
}

TEST_CASE ("MIDI export bakes in swing and loops seamlessly", "[export][midi]")
{
    bf::Pattern p;
    p.tracks[0].steps[0].on = true;           // kick on 1
    p.tracks[3].steps[1].on = true;           // hat on the swung "e"
    p.tracks[3].steps[1].velocity = 64;
    p.tracks[1].steps[0].on = true;           // a snare nudged before the downbeat
    p.tracks[1].steps[0].nudge = -0.25f;

    bf::GrooveContext ctx;
    ctx.swing.fill (0.6f);
    bf::SequencerSnapshot snap;

    const auto file = roundTrip (bf::Exporter::makeMidiFile (p, ctx, snap.midiNotes, 174.0, 4));
    REQUIRE (file.getTimeFormat() == bf::Exporter::midiTicksPerQuarter);
    REQUIRE (file.getNumTracks() == 1);

    const auto* seq = file.getTrack (0);
    std::vector<std::pair<double, juce::MidiMessage>> ons;
    int offs = 0;
    double tempo = 0.0;

    for (int i = 0; i < seq->getNumEvents(); ++i)
    {
        const auto& m = seq->getEventPointer (i)->message;
        if (m.isNoteOn()) ons.emplace_back (m.getTimeStamp(), m);
        if (m.isNoteOff()) ++offs;
        if (m.isTempoMetaEvent()) tempo = 60.0 / m.getTempoSecondsPerQuarterNote();
    }

    REQUIRE_THAT (tempo, WithinAbs (174.0, 0.01));
    // 4 bars: 4 kicks, 4 hats, and 4 snares (the first one wraps to the end of the file).
    REQUIRE (ons.size() == 12);
    REQUIRE (offs == 12);

    std::vector<double> hatTicks, snareTicks;
    for (auto& [t, m] : ons)
    {
        REQUIRE (m.getChannel() == 10);
        if (m.getNoteNumber() == 42) { hatTicks.push_back (t); REQUIRE (m.getVelocity() == 64); }
        if (m.getNoteNumber() == 38) snareTicks.push_back (t);
    }

    REQUIRE (hatTicks.size() == 4);
    // Swing 60 %: the "e" moves from 0.25 to 0.30 of a quarter = 288 ticks.
    REQUIRE (hatTicks[0] == 288.0);
    REQUIRE (hatTicks[1] == 288.0 + 4 * 960.0);

    REQUIRE (snareTicks.size() == 4);
    const double nudge = 0.25 * 0.25 * 960.0; // a quarter of a 16th
    REQUIRE (snareTicks.back() == 16 * 960.0 - nudge);
}

TEST_CASE ("Offline render is sample-accurate, latency-compensated and loop-length exact", "[export][render]")
{
    bf::Exporter::RenderRequest req;
    req.sampleRate = 48000.0;
    req.bpm = 172.0;
    req.bars = 2;
    req.params = bftest::neutralParams();
    req.params.driveOn = false;
    req.params.limiterOn = true;
    req.applyMaster = true;

    for (auto& s : req.samples)
        s = bftest::makeClick (req.sampleRate);

    auto& p = req.snapshot.bank.patterns[0];
    for (int s : { 0, 6, 10 })
        p.tracks[0].steps[(size_t) s].on = true;

    const auto result = bf::Exporter::render (req);
    const double spp = req.sampleRate * 60.0 / req.bpm;
    REQUIRE (result.mix.getNumSamples() == (int) std::lround (8.0 * spp));

    // The oversampler's linear-phase filter pre-rings, so locate each click by its peak.
    const auto* left = result.mix.getReadPointer (0);
    const int len = result.mix.getNumSamples();

    for (int bar = 0; bar < 2; ++bar)
    {
        for (int s : { 0, 6, 10 })
        {
            const auto expected = (int) std::lround ((bar * 4.0 + s * 0.25) * spp);
            int peakAt = expected;

            for (int i = juce::jmax (0, expected - 200); i < juce::jmin (len, expected + 200); ++i)
                if (std::abs (left[i]) > std::abs (left[peakAt]))
                    peakAt = i;

            REQUIRE (peakAt == expected);
            REQUIRE (std::abs (left[expected]) > 0.5f);
        }
    }
    REQUIRE (result.mix.getMagnitude (0, result.mix.getNumSamples()) <= juce::Decibels::decibelsToGain (-0.3f) + 1e-4f);
}

TEST_CASE ("Rendered loops carry the previous loop's tail and stems sum to the mix", "[export][render]")
{
    bf::Exporter::RenderRequest req;
    req.sampleRate = 44100.0;
    req.bpm = 174.0;
    req.bars = 1;
    req.params = bftest::neutralParams();
    req.applyMaster = false;
    req.renderStems = true;

    // A one-second ring on the open hat, hit on the last 16th of the bar.
    juce::AudioBuffer<float> ring (1, 44100);
    for (int i = 0; i < ring.getNumSamples(); ++i)
        ring.setSample (0, i, 0.3f * (float) std::sin (i * 0.05));
    req.samples[4 * bf::kSampleVariants] = new bf::SampleData (std::move (ring), 44100.0, "ring");
    req.samples[0] = bftest::makeClick (req.sampleRate);

    auto& p = req.snapshot.bank.patterns[0];
    p.tracks[4].steps[15].on = true;
    p.tracks[0].steps[0].on = true;

    const auto result = bf::Exporter::render (req);

    // The very start of the loop already contains the open hat ringing over from the last loop.
    REQUIRE (result.stems[4].getMagnitude (0, 1, 2000) > 0.1f);
    REQUIRE (result.trackHasAudio[0]);
    REQUIRE (result.trackHasAudio[4]);
    REQUIRE_FALSE (result.trackHasAudio[1]);

    juce::AudioBuffer<float> sum (2, result.mix.getNumSamples());
    sum.clear();
    for (const auto& stem : result.stems)
        if (stem.getNumSamples() > 0)
            for (int ch = 0; ch < 2; ++ch)
                sum.addFrom (ch, 0, stem, ch, 0, stem.getNumSamples());

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < sum.getNumSamples(); i += 7)
            REQUIRE_THAT (sum.getSample (ch, i), WithinAbs (result.mix.getSample (ch, i), 1e-5));
}

TEST_CASE ("Offline renders use per-hit samples", "[export][render][samples]")
{
    bf::Exporter::RenderRequest req;
    req.sampleRate = 48000.0;
    req.bpm = 174.0;
    req.bars = 1;
    req.params = bftest::neutralParams();
    req.applyMaster = false;

    juce::AudioBuffer<float> quarter (1, 64);
    quarter.clear();
    quarter.setSample (0, 0, 0.25f);
    req.samples[0] = bftest::makeClick (req.sampleRate);
    req.samples[3] = new bf::SampleData (std::move (quarter), 48000.0, "quarter"); // track 0, variant 3

    auto& p = req.snapshot.bank.patterns[0];
    p.tracks[0].steps[4].on = p.tracks[0].steps[8].on = true;
    p.tracks[0].steps[8].sample = 3;

    const auto result = bf::Exporter::render (req);
    const double spp = req.sampleRate * 60.0 / req.bpm;
    REQUIRE (std::abs (result.mix.getSample (0, (int) std::lround (1.0 * spp)) - 1.0f) < 1.0e-3f);
    REQUIRE (std::abs (result.mix.getSample (0, (int) std::lround (2.0 * spp)) - 0.25f) < 1.0e-3f);
}

TEST_CASE ("Limiter never exceeds its ceiling and is transparent below it", "[master][limiter]")
{
    const double sr = 48000.0;
    bf::LookaheadLimiter lim;
    lim.prepare (sr, 72);
    const float ceiling = juce::Decibels::decibelsToGain (-0.3f);

    // +12 dB drum-like bursts: sharp transients are the hard case for a limiter.
    std::vector<float> l (48000), r (48000);
    for (size_t i = 0; i < l.size(); ++i)
    {
        const double env = std::exp (-(double) (i % 6000) / 600.0);
        l[i] = (float) (4.0 * env * std::sin (i * 0.07));
        r[i] = (float) (3.0 * env * std::sin (i * 0.11));
    }

    for (size_t start = 0; start < l.size(); start += 333)
    {
        const int n = (int) std::min<size_t> (333, l.size() - start);
        lim.process (l.data() + start, r.data() + start, n, true);
    }

    for (size_t i = 0; i < l.size(); ++i)
    {
        REQUIRE (std::abs (l[i]) <= ceiling + 1e-6f);
        REQUIRE (std::abs (r[i]) <= ceiling + 1e-6f);
    }

    // Quiet material only gets delayed.
    bf::LookaheadLimiter quiet;
    quiet.prepare (sr, 72);
    std::vector<float> a (4000), b (4000);
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = b[i] = 0.5f * (float) std::sin (i * 0.01);
    const auto original = a;
    quiet.process (a.data(), b.data(), (int) a.size(), true);

    for (size_t i = 72; i < a.size(); ++i)
        REQUIRE_THAT (a[i], WithinAbs (original[i - 72], 1e-6));
}

TEST_CASE ("Drive thickens without changing the latency", "[master][drive]")
{
    const double sr = 48000.0;
    const int n = 4800;

    const auto run = [&] (bool driveOn, float drive) {
        bf::MasterBus bus;
        bus.prepare (sr, 512);
        juce::AudioBuffer<float> b (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sr));
        for (int start = 0; start < n; start += 480)
        {
            juce::AudioBuffer<float> chunk (b.getArrayOfWritePointers(), 2, start, 480);
            bus.process (chunk, drive, driveOn, false);
        }
        return std::make_pair (bus.getLatency(), b);
    };

    const auto [latOff, off] = run (false, 1.0f);
    const auto [latOn, on] = run (true, 1.0f);
    REQUIRE (latOff == latOn);

    // Drive changes the waveform (harmonics) but keeps the level in the same ballpark.
    double diff = 0.0, rmsOff = 0.0, rmsOn = 0.0;
    for (int i = 2400; i < n; ++i)
    {
        diff += std::abs (on.getSample (0, i) - off.getSample (0, i));
        rmsOff += off.getSample (0, i) * off.getSample (0, i);
        rmsOn += on.getSample (0, i) * on.getSample (0, i);
    }
    REQUIRE (diff / 2400.0 > 0.01);
    REQUIRE (std::sqrt (rmsOn / rmsOff) > 0.5);
    REQUIRE (std::sqrt (rmsOn / rmsOff) < 2.0);
}
