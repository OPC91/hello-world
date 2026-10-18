#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "analysis/AnalysisEngine.h"
#include "analysis/HintGenerator.h"
#include "util/AudioFifo.h"

#include <juce_dsp/juce_dsp.h>

#include <thread>

using namespace benchmark::analysis;
using benchmark::StereoFifo;
using Catch::Approx;

namespace
{
    constexpr double sr = 48000.0;

    /** Paul Kellet's refined pink noise filter (+/- 0.05 dB above ~10 Hz). */
    struct PinkNoise
    {
        juce::Random rng;
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

        explicit PinkNoise (int seed) : rng (seed) {}

        float next()
        {
            const auto white = rng.nextDouble() * 2.0 - 1.0;
            b0 = 0.99886 * b0 + white * 0.0555179;
            b1 = 0.99332 * b1 + white * 0.0750759;
            b2 = 0.96900 * b2 + white * 0.1538520;
            b3 = 0.86650 * b3 + white * 0.3104856;
            b4 = 0.55000 * b4 + white * 0.5329522;
            b5 = -0.7616 * b5 - white * 0.0168980;
            const auto pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
            b6 = white * 0.115926;
            return (float) (pink * 0.11);
        }
    };

    juce::AudioBuffer<float> pinkStereo (double seconds, int seed = 1, bool independent = true)
    {
        PinkNoise l (seed), r (seed + 1000);
        juce::AudioBuffer<float> b (2, (int) (seconds * sr));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto left = l.next();
            b.setSample (0, i, left);
            b.setSample (1, i, independent ? r.next() : left);
        }
        return b;
    }

    juce::AudioBuffer<float> whiteStereo (double seconds, int seed = 1)
    {
        juce::Random rng (seed);
        juce::AudioBuffer<float> b (2, (int) (seconds * sr));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.3f);
        return b;
    }

    void feed (SpectralAnalyser& a, const juce::AudioBuffer<float>& b, int block = 1000)
    {
        for (int s = 0; s < b.getNumSamples(); s += block)
            a.process (b.getReadPointer (0, s), b.getReadPointer (1, s), std::min (block, b.getNumSamples() - s));
    }

    void feed (DynamicsAnalyser& a, const juce::AudioBuffer<float>& b, int block = 1000)
    {
        for (int s = 0; s < b.getNumSamples(); s += block)
            a.process (b.getReadPointer (0, s), b.getReadPointer (1, s), std::min (block, b.getNumSamples() - s));
    }

    SpectralView averageOf (const juce::AudioBuffer<float>& b)
    {
        SpectralAnalyser a;
        a.prepare (sr);
        feed (a, b);
        SpectralView avg, live;
        a.fillViews (avg, live);
        return avg;
    }

    int bandIndex (float centre)
    {
        for (int i = 0; i < numThirdOctaveBands; ++i)
            if (std::abs (thirdOctaveCentres[(size_t) i] - centre) < 0.01f)
                return i;
        return -1;
    }
}

//==============================================================================
TEST_CASE ("Stereo FIFO keeps order across wrap-around and counts drops", "[fifo]")
{
    StereoFifo fifo (100);
    std::vector<float> l (300), r (300), outL (300), outR (300);
    for (int i = 0; i < 300; ++i) { l[(size_t) i] = (float) i; r[(size_t) i] = (float) -i; }

    int next = 0;
    for (int round = 0; round < 10; ++round)
    {
        REQUIRE (fifo.push (l.data() + 70 * round % 200, r.data() + 70 * round % 200, 70) == 70);
        REQUIRE (fifo.pop (outL.data(), outR.data(), 300) == 70);
        for (int i = 0; i < 70; ++i)
        {
            REQUIRE (outL[(size_t) i] == Approx ((float) (70 * round % 200 + i)));
            REQUIRE (outR[(size_t) i] == Approx ((float) -(70 * round % 200 + i)));
        }
        next += 70;
    }

    // Overflow: the FIFO holds at most capacity - 1 frames; the rest are dropped and counted.
    const auto written = fifo.push (l.data(), r.data(), 300);
    CHECK (written < 100);
    CHECK (fifo.getDroppedFrames() == (uint64_t) (300 - written));
    fifo.discardAll();
    CHECK (fifo.getNumReady() == 0);
}

TEST_CASE ("Stereo FIFO survives concurrent use", "[fifo][stress]")
{
    StereoFifo fifo (4096);
    constexpr int total = 2'000'000;
    std::atomic<bool> ok { true };

    std::thread consumer ([&]
    {
        std::vector<float> l (1024), r (1024);
        int expected = 0;
        while (expected < total)
        {
            const auto n = fifo.pop (l.data(), r.data(), 1024);
            for (int i = 0; i < n; ++i, ++expected)
                if (! juce::exactlyEqual (l[(size_t) i], (float) (expected % 65536)) || ! juce::exactlyEqual (r[(size_t) i], -l[(size_t) i]))
                    ok = false;
            if (n == 0)
                std::this_thread::yield();
        }
    });

    std::vector<float> l (333), r (333);
    int produced = 0;
    while (produced < total)
    {
        const auto n = std::min (333, total - produced);
        for (int i = 0; i < n; ++i) { l[(size_t) i] = (float) ((produced + i) % 65536); r[(size_t) i] = -l[(size_t) i]; }

        int sent = 0;
        while (sent < n)
        {
            sent += fifo.push (l.data() + sent, r.data() + sent, n - sent);
            if (sent < n)
                std::this_thread::yield();
        }
        produced += n;
    }

    consumer.join();
    CHECK (ok.load());
}

//==============================================================================
TEST_CASE ("FFT size keeps bins about 6 Hz wide", "[spectrum]")
{
    CHECK (SpectralAnalyser::fftOrderFor (44100.0) == 13);
    CHECK (SpectralAnalyser::fftOrderFor (48000.0) == 13);
    CHECK (SpectralAnalyser::fftOrderFor (96000.0) == 14);
    CHECK (SpectralAnalyser::fftOrderFor (192000.0) == 15);
    CHECK (SpectralAnalyser::fftOrderFor (22050.0) == 12);
}

TEST_CASE ("Pink noise reads flat in 1/3-octave bands", "[spectrum]")
{
    const auto v = averageOf (pinkStereo (20.0));
    REQUIRE (v.valid);

    const auto first = bandIndex (40.0f), last = bandIndex (16000.0f);
    double mean = 0.0;
    for (int i = first; i <= last; ++i)
        mean += v.bands[(size_t) i];
    mean /= (last - first + 1);

    for (int i = first; i <= last; ++i)
    {
        CAPTURE (thirdOctaveCentres[(size_t) i]);
        CHECK (v.bands[(size_t) i] == Approx (mean).margin (1.5));
    }

    // The smoothed curve agrees with the bands.
    for (int i = 20; i < numCurvePoints - 20; ++i)
        CHECK (v.curve[(size_t) i] == Approx (mean).margin (1.5));
}

TEST_CASE ("White noise rises 3 dB per octave", "[spectrum]")
{
    const auto v = averageOf (whiteStereo (10.0));
    const auto slope = (v.bands[(size_t) bandIndex (8000.0f)] - v.bands[(size_t) bandIndex (1000.0f)]) / 3.0f;
    CHECK (slope == Approx (3.0f).margin (0.4f));
}

TEST_CASE ("Tonal values don't depend on level", "[spectrum]")
{
    auto loud = pinkStereo (5.0);
    auto quiet = loud;
    quiet.applyGain (0.1f);

    const auto a = averageOf (loud);
    const auto b = averageOf (quiet);

    for (int i = 0; i < numThirdOctaveBands; ++i)
        CHECK (a.bands[(size_t) i] == Approx (b.bands[(size_t) i]).margin (0.01));
}

TEST_CASE ("A +6 dB boost at 300 Hz shows in the difference and the hints", "[spectrum][hints]")
{
    const auto reference = pinkStereo (20.0, 7);
    auto mix = reference;

    juce::dsp::IIR::Filter<float> peakL, peakR;
    peakL.coefficients = peakR.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 300.0f, 1.4f, 2.0f);
    for (int i = 0; i < mix.getNumSamples(); ++i)
    {
        mix.setSample (0, i, peakL.processSample (mix.getSample (0, i)));
        mix.setSample (1, i, peakR.processSample (mix.getSample (1, i)));
    }

    AnalysisSnapshot s;
    s.mix.average = averageOf (mix);
    s.reference.average = averageOf (reference);
    s.mix.secondsAnalysed = s.reference.secondsAnalysed = 20.0;

    const auto diff = [&] (float centre) { return s.mix.average.bands[(size_t) bandIndex (centre)] - s.reference.average.bands[(size_t) bandIndex (centre)]; };
    CHECK (diff (315.0f) - diff (3150.0f) == Approx (6.0f).margin (1.0f));
    CHECK (std::abs (diff (3150.0f) - diff (8000.0f)) < 0.5f); // flat away from the boost

    const auto hints = HintGenerator::tonal (s, false);
    INFO (hints.joinIntoString ("\n"));
    REQUIRE (hints.size() >= 1);
    CHECK (hints.joinIntoString ("\n").contains ("more 200-500 Hz (low-mids) than the reference"));
}

TEST_CASE ("Stereo width and correlation per band", "[stereo]")
{
    SECTION ("Mono: no side, correlation +1")
    {
        const auto v = averageOf (pinkStereo (5.0, 3, false));
        for (const auto& b : v.stereo)
        {
            CHECK (b.width == Approx (0.0f).margin (1e-4));
            CHECK (b.correlation == Approx (1.0f).margin (1e-4));
        }
    }

    SECTION ("Independent channels: half side, correlation near 0")
    {
        const auto v = averageOf (pinkStereo (10.0, 3, true));
        for (const auto& b : v.stereo)
        {
            CHECK (b.width == Approx (0.5f).margin (0.05f));
            CHECK (b.correlation == Approx (0.0f).margin (0.08f));
        }
    }

    SECTION ("Each band is measured separately")
    {
        // Low: identical 60 Hz sines (mono). High: 8 kHz in opposite polarity.
        auto low = testing::makeSine (sr, 5.0, 60.0, -12.0);
        const auto high = testing::makeSine (sr, 5.0, 8000.0, -12.0);
        for (int i = 0; i < low.getNumSamples(); ++i)
        {
            low.setSample (0, i, low.getSample (0, i) + high.getSample (0, i));
            low.setSample (1, i, low.getSample (1, i) - high.getSample (1, i));
        }

        const auto v = averageOf (low);
        CHECK (v.stereo[0].correlation == Approx (1.0f).margin (0.01f));
        CHECK (v.stereo[0].width == Approx (0.0f).margin (0.01f));
        CHECK (v.stereo[3].correlation == Approx (-1.0f).margin (0.01f));
        CHECK (v.stereo[3].width == Approx (1.0f).margin (0.01f));
    }
}

TEST_CASE ("Silence is left out of the average; live view follows changes", "[spectrum]")
{
    SpectralAnalyser a;
    a.prepare (sr);
    feed (a, pinkStereo (5.0));

    juce::AudioBuffer<float> silence (2, (int) (10.0 * sr));
    silence.clear();
    feed (a, silence);
    CHECK (a.getSecondsAnalysed() == Approx (5.0).margin (0.3));

    feed (a, testing::makeSine (sr, 6.0, 1000.0, -12.0));

    SpectralView avg, live;
    a.fillViews (avg, live);
    const auto k = (size_t) bandIndex (1000.0f), other = (size_t) bandIndex (125.0f);

    // Live is dominated by the new tone; the average still remembers the noise.
    CHECK (live.bands[k] - live.bands[other] > 25.0f); // about 2 % of the noise remains after 4 time constants
    CHECK (avg.bands[k] - avg.bands[other] < live.bands[k] - live.bands[other] - 10.0f);

    a.reset();
    a.fillViews (avg, live);
    CHECK_FALSE (avg.valid);
    CHECK_FALSE (live.valid);
}

//==============================================================================
TEST_CASE ("Dynamics: crest factor, PLR and history", "[dynamics]")
{
    SECTION ("Sine: crest 3 dB, PLR 0")
    {
        DynamicsAnalyser d;
        d.prepare (sr);
        feed (d, testing::makeSine (sr, 10.0, 1000.0, -20.0));
        DynamicsView v;
        d.fill (v);
        CHECK (v.peakDb == Approx (-20.0f).margin (0.01f));
        CHECK (v.crestDb == Approx (3.01f).margin (0.05f));
        CHECK (v.integratedLufs == Approx (-20.0f).margin (0.1f));
        CHECK (v.plr == Approx (0.0f).margin (0.15f));
        CHECK (v.historyCount == 100);
        CHECK (v.history[99] == Approx (-20.0f).margin (0.1f));
    }

    SECTION ("Square wave: crest 0 dB")
    {
        juce::AudioBuffer<float> sq (2, (int) sr * 4);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < sq.getNumSamples(); ++i)
                sq.setSample (ch, i, ((i / 24) % 2) ? 0.5f : -0.5f);

        DynamicsAnalyser d;
        d.prepare (sr);
        feed (d, sq);
        DynamicsView v;
        d.fill (v);
        CHECK (v.crestDb == Approx (0.0f).margin (0.01f));
    }

    SECTION ("Silence doesn't lower the RMS; loudness range is reported")
    {
        DynamicsAnalyser d;
        d.prepare (sr);
        feed (d, testing::makeSineSequence (sr, { { 20, -20 }, { 20, -30 } }));
        juce::AudioBuffer<float> silence (2, (int) sr * 5);
        silence.clear();
        feed (d, silence);

        DynamicsView v;
        d.fill (v);
        CHECK (v.peakDb == Approx (-20.0f).margin (0.01f));
        CHECK (v.loudnessRange == Approx (10.0f).margin (1.0f));
        CHECK (v.historyCount == 450); // one value per 100 ms (the cap is 60 s)
    }
}

//==============================================================================
TEST_CASE ("Analysis engine drains FIFOs into snapshots", "[engine]")
{
    AnalysisEngine engine; // thread not started: driven directly
    AnalysisSnapshot snap;

    CHECK_FALSE (engine.processPending()); // no sample rate yet

    engine.setSampleRate (sr);
    const auto pink = pinkStereo (3.0);
    const auto tone = testing::makeSine (sr, 3.0, 1000.0, -18.0);

    for (int s = 0; s < pink.getNumSamples(); s += 512)
    {
        const auto n = std::min (512, pink.getNumSamples() - s);
        engine.pushMix (pink.getReadPointer (0, s), pink.getReadPointer (1, s), n);
        engine.pushReference (tone.getReadPointer (0, s), tone.getReadPointer (1, s), n);

        if ((s / 512) % 16 == 0)
            engine.processPending();
    }
    engine.processPending();

    engine.getSnapshot (snap);
    CHECK (snap.sampleRate == Approx (sr));
    CHECK (snap.mix.average.valid);
    CHECK (snap.reference.average.valid);
    CHECK (snap.mix.secondsAnalysed > 2.5);
    CHECK (snap.reference.dynamics.crestDb == Approx (3.01f).margin (0.1f));
    const auto versionBefore = engine.getVersion();
    CHECK (versionBefore > 0);

    engine.requestReset (false, true);
    CHECK (engine.processPending());
    engine.getSnapshot (snap);
    CHECK (snap.mix.average.valid);
    CHECK_FALSE (snap.reference.average.valid);
    CHECK (engine.getVersion() > versionBefore);

    // A new sample rate reconfigures and clears.
    engine.setSampleRate (96000.0);
    CHECK (engine.processPending());
    engine.getSnapshot (snap);
    CHECK (snap.sampleRate == Approx (96000.0));
    CHECK_FALSE (snap.mix.average.valid);
}

//==============================================================================
namespace
{
    AnalysisSnapshot snapshotWithRanges (std::array<float, numTonalRanges> mixRanges, std::array<float, numTonalRanges> refRanges)
    {
        AnalysisSnapshot s;
        s.mix.secondsAnalysed = s.reference.secondsAnalysed = 30.0;
        s.mix.average.valid = s.reference.average.valid = true;
        s.mix.average.ranges = mixRanges;
        s.reference.average.ranges = refRanges;
        return s;
    }
}

TEST_CASE ("Tonal hints describe differences in plain language", "[hints]")
{
    SECTION ("Waiting for audio")
    {
        AnalysisSnapshot s;
        CHECK (HintGenerator::tonal (s, false)[0] == "Play your mix with a reference loaded to compare them.");
        s.mix.average.valid = true;
        s.mix.secondsAnalysed = 5.0;
        CHECK (HintGenerator::tonal (s, false)[0].startsWith ("Play the reference"));
    }

    SECTION ("Close match")
    {
        const auto s = snapshotWithRanges ({ -10, -8, -9, -12, -15, -20, -28 }, { -10.5f, -8, -9.5f, -12, -15, -20.4f, -28 });
        const auto hints = HintGenerator::tonal (s, false);
        REQUIRE (hints.size() == 1);
        CHECK (hints[0].startsWith ("Tonal balance is close to the reference"));
    }

    SECTION ("Brighter, with more low-mids, largest first")
    {
        const auto s = snapshotWithRanges ({ -10, -8, -6.5f, -12, -15, -17, -24 }, { -10, -8, -9, -12, -15, -20, -28 });
        const auto hints = HintGenerator::tonal (s, false);
        REQUIRE (hints.size() == 4);
        CHECK (hints[0] == "Overall the mix sounds brighter than the reference.");
        CHECK (hints[1] == "Mix has +4.0 dB more 10-20 kHz (air) than the reference");
        CHECK (hints[2] == "Mix has +3.0 dB more 5-10 kHz (presence) than the reference");
        CHECK (hints[3] == "Mix has +2.5 dB more 200-500 Hz (low-mids) than the reference");
    }

    SECTION ("Darker and lighter in the low end")
    {
        const auto s = snapshotWithRanges ({ -14, -11, -9, -12, -15, -23, -31 }, { -10, -8, -9, -12, -15, -20, -28 });
        const auto hints = HintGenerator::tonal (s, false);
        CHECK (hints[0] == "Overall the mix sounds darker and lighter in the low end than the reference.");
        CHECK (hints[1] == "Mix has 4.0 dB less 20-60 Hz (sub bass) than the reference");
    }
}

TEST_CASE ("Stereo and dynamics hints", "[hints]")
{
    auto s = snapshotWithRanges ({}, {});

    SECTION ("Stereo")
    {
        s.mix.average.stereo[0] = { -6.0f, 0.25f, 0.4f };
        s.reference.average.stereo[0] = { -15.0f, 0.05f, 0.9f };
        s.mix.average.stereo[3] = { -8.0f, 0.10f, -0.2f };
        s.reference.average.stereo[3] = { -4.0f, 0.30f, 0.3f };

        const auto hints = HintGenerator::stereo (s, false);
        INFO (hints.joinIntoString ("\n"));
        CHECK (hints.contains ("Mix has more side content in the low end than the reference (25% vs 5%): check mono compatibility"));
        CHECK (hints.contains ("Mix has negative correlation in the high band (-0.20): parts of it may cancel in mono"));
        CHECK (hints.contains ("Mix is narrower in the high band (10% side vs 30%)"));
    }

    SECTION ("Dynamics")
    {
        s.mix.dynamics.plr = 6.2f;
        s.reference.dynamics.plr = 9.0f;
        s.mix.dynamics.crestDb = 9.0f;
        s.reference.dynamics.crestDb = 12.5f;
        s.mix.dynamics.loudnessRange = 3.0f;
        s.reference.dynamics.loudnessRange = 6.5f;

        const auto hints = HintGenerator::dynamics (s);
        REQUIRE (hints.size() == 3);
        CHECK (hints[0] == "Mix is more compressed or limited: PLR 6.2 dB vs 9.0 dB in the reference");
        CHECK (hints[1] == "Mix has softer transients (lower crest factor): 9.0 dB vs 12.5 dB");
        CHECK (hints[2] == "Mix is more even in loudness: LRA 3.0 LU vs 6.5 LU");
    }
}
