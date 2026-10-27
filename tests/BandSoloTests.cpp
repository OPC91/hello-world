#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "dsp/BandSolo.h"
#include "PluginProcessor.h"
#include "Parameters.h"

#include <complex>
#include <thread>

using benchmark::dsp::BandSolo;
using Catch::Approx;

namespace
{
    constexpr double sr = 48000.0;

    double responseDb (const std::vector<float>& fir, double frequency)
    {
        std::complex<double> sum;
        for (size_t n = 0; n < fir.size(); ++n)
            sum += (double) fir[n] * std::polar (1.0, -2.0 * std::numbers::pi * frequency * (double) n / sr);
        return 20.0 * std::log10 (std::abs (sum) + 1e-30);
    }

    /** Runs a stereo signal through the solo in blocks; returns the output. */
    juce::AudioBuffer<float> run (BandSolo& solo, juce::AudioBuffer<float> signal, int block = 256)
    {
        for (int s = 0; s < signal.getNumSamples(); s += block)
        {
            float* ptrs[2] = { signal.getWritePointer (0, s), signal.getWritePointer (1, s) };
            solo.process (ptrs, 2, std::min (block, signal.getNumSamples() - s));
        }
        return signal;
    }

    double rmsDbTail (const juce::AudioBuffer<float>& b, double fromSeconds)
    {
        const auto start = (int) (fromSeconds * sr);
        return juce::Decibels::gainToDecibels (b.getRMSLevel (0, start, b.getNumSamples() - start), -200.0f);
    }

    /** Lets the convolution's background loader install a new impulse response. */
    void settleConvolution (BandSolo& solo)
    {
        juce::AudioBuffer<float> silence (2, 512);
        for (int i = 0; i < 60; ++i)
        {
            silence.clear();
            run (solo, silence, 512);
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
    }
}

TEST_CASE ("Linear-phase band filters", "[bandsolo][fir]")
{
    const auto length = BandSolo::firLengthFor (sr);
    REQUIRE (length % 2 == 1);
    CHECK ((double) (length / 2) / sr == Approx (0.0427).margin (0.001)); // ~43 ms latency

    const auto offFir = BandSolo::designBandFir (BandSolo::off, sr, length);
    for (int i = 0; i < length; ++i)
        REQUIRE (offFir[(size_t) i] == Approx (i == length / 2 ? 1.0f : 0.0f));

    std::vector<double> sum ((size_t) length, 0.0);
    std::array<std::vector<float>, 4> bands;

    for (int b = 0; b < BandSolo::numBands; ++b)
    {
        bands[(size_t) b] = BandSolo::designBandFir (b, sr, length);
        for (int i = 0; i < length; ++i)
        {
            REQUIRE (bands[(size_t) b][(size_t) i] == Approx (bands[(size_t) b][(size_t) (length - 1 - i)]).margin (1e-9)); // symmetric: linear phase
            sum[(size_t) i] += bands[(size_t) b][(size_t) i];
        }
    }

    // The four bands add back up to a pure delay.
    for (int i = 0; i < length; ++i)
        REQUIRE (sum[(size_t) i] == Approx (i == length / 2 ? 1.0 : 0.0).margin (1e-6));

    // Pass inside each band, reject well outside it.
    struct Check { int band; double pass; std::vector<double> stop; };
    for (const auto& c : { Check { 0, 60.0, { 300.0, 2000.0 } },
                           Check { 1, 400.0, { 60.0, 2500.0 } },
                           Check { 2, 2500.0, { 400.0, 12000.0 } },
                           Check { 3, 10000.0, { 2500.0, 400.0 } } })
    {
        CAPTURE (c.band);
        CHECK (responseDb (bands[(size_t) c.band], c.pass) == Approx (0.0).margin (0.1));
        for (const auto f : c.stop)
            CHECK (responseDb (bands[(size_t) c.band], f) < -50.0);
    }
}

TEST_CASE ("Zero-latency band solo passes its band and rejects the others", "[bandsolo]")
{
    BandSolo solo;
    solo.prepare (sr, 256, 2);
    CHECK (solo.isTransparent());

    struct Case { double frequency; int band; bool passes; };
    for (const auto& c : { Case { 60.0, 0, true }, Case { 60.0, 3, false }, Case { 400.0, 1, true }, Case { 400.0, 3, false },
                           Case { 2500.0, 2, true }, Case { 2500.0, 0, false }, Case { 10000.0, 3, true }, Case { 10000.0, 0, false } })
    {
        CAPTURE (c.frequency, c.band);
        solo.prepare (sr, 256, 2);
        solo.setBand (c.band);
        const auto out = run (solo, testing::makeSine (sr, 1.0, c.frequency, -6.0));
        const auto level = rmsDbTail (out, 0.5) - (-6.0 - 3.01);

        // LR4 skirts take up to ~0.7 dB inside the narrow middle bands.
        if (c.passes)
            CHECK (level == Approx (0.0).margin (1.0));
        else
            CHECK (level < -40.0);
    }

    CHECK_FALSE (solo.isTransparent());
    CHECK (solo.getLatencyFor (BandSolo::Mode::zeroLatency) == 0);
}

TEST_CASE ("Switching bands doesn't click", "[bandsolo]")
{
    BandSolo solo;
    solo.prepare (sr, 256, 2);
    solo.setBand (1);

    // 1 kHz sits on the Low-mid / High-mid crossover, so both bands carry it.
    auto signal = testing::makeSine (sr, 1.0, 1000.0, -6.0);
    float* ptrs[2] = { signal.getWritePointer (0), signal.getWritePointer (1) };

    for (int s = 0; s < signal.getNumSamples(); s += 256)
    {
        if (s == 256 * 80)  solo.setBand (2);
        if (s == 256 * 120) solo.setBand (BandSolo::off);
        if (s == 256 * 160) solo.setBand (0);

        float* block[2] = { ptrs[0] + s, ptrs[1] + s };
        solo.process (block, 2, std::min (256, signal.getNumSamples() - s));
    }

    const auto maxSlope = 0.5f * 2.0f * std::numbers::pi_v<float> * 1000.0f / (float) sr;
    float maxStep = 0.0f;
    for (int i = 2000; i < signal.getNumSamples(); ++i)
        maxStep = std::max (maxStep, std::abs (signal.getSample (0, i) - signal.getSample (0, i - 1)));

    CHECK (maxStep < maxSlope * 1.5f);
}

TEST_CASE ("Linear-phase mode delays by the reported latency and filters bands", "[bandsolo][linear]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BandSolo solo;
    solo.setMode (BandSolo::Mode::linearPhase);
    solo.prepare (sr, 512, 2);
    REQUIRE (solo.getActiveMode() == BandSolo::Mode::linearPhase);
    settleConvolution (solo);

    const auto latency = solo.getLinearPhaseLatency();
    CHECK (latency == BandSolo::firLengthFor (sr) / 2);

    // Solo off: an impulse comes out exactly `latency` samples later, unchanged.
    juce::AudioBuffer<float> impulse (2, latency + 4096);
    impulse.clear();
    impulse.setSample (0, 0, 1.0f);
    impulse.setSample (1, 0, 1.0f);
    const auto out = run (solo, impulse, 512);

    int peakIndex = 0;
    for (int i = 0; i < out.getNumSamples(); ++i)
        if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peakIndex)))
            peakIndex = i;

    CHECK (peakIndex == latency);
    CHECK (out.getSample (0, peakIndex) == Approx (1.0f).margin (1e-3));

    // Solo the low band: 60 Hz passes, 2 kHz is removed.
    solo.setBand (0);
    solo.updateLinearPhaseBand (0);
    settleConvolution (solo);

    CHECK (rmsDbTail (run (solo, testing::makeSine (sr, 1.0, 60.0, -6.0)), 0.3) == Approx (-9.03).margin (0.3));
    CHECK (rmsDbTail (run (solo, testing::makeSine (sr, 1.0, 2000.0, -6.0)), 0.3) < -55.0);
}

TEST_CASE ("Processor reports latency for the linear-phase mode", "[bandsolo][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    benchmark::BenchmarkProcessor processor;
    processor.prepareToPlay (sr, 512);
    CHECK (processor.getLatencySamples() == 0);

    processor.getValueTreeState().getParameter (benchmark::ParamIDs::crossoverMode)->setValueNotifyingHost (1.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200); // timer updates the latency
    const auto latency = BandSolo::firLengthFor (sr) / 2;
    CHECK (processor.getLatencySamples() == latency);

    // Host bypass keeps the same latency so nothing jumps in time.
    juce::AudioBuffer<float> silence (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 40; ++i)
    {
        silence.clear();
        processor.processBlockBypassed (silence, midi);
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }

    juce::AudioBuffer<float> impulse (2, latency + 2048);
    impulse.clear();
    impulse.setSample (0, 0, 1.0f);
    for (int s = 0; s < impulse.getNumSamples(); s += 512)
    {
        juce::AudioBuffer<float> view (impulse.getArrayOfWritePointers(), 2, s, std::min (512, impulse.getNumSamples() - s));
        processor.processBlockBypassed (view, midi);
    }

    CHECK (impulse.getSample (0, latency) == Approx (1.0f).margin (1e-3));
    CHECK (std::abs (impulse.getSample (0, 0)) < 1e-4f);

    processor.getValueTreeState().getParameter (benchmark::ParamIDs::crossoverMode)->setValueNotifyingHost (0.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    CHECK (processor.getLatencySamples() == 0);
}
