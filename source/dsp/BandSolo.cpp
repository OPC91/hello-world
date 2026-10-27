#include "BandSolo.h"
#include "analysis/AnalysisBands.h"

#include <cmath>
#include <numbers>

namespace benchmark::dsp
{
    namespace
    {
        double besselI0 (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 64; ++k)
            {
                term *= (x * 0.5 / k) * (x * 0.5 / k);
                sum += term;
                if (term < sum * 1e-17)
                    break;
            }
            return sum;
        }

        /** Ideal low-pass impulse response value at offset n from the centre. */
        double lowPass (double cutoffHz, double sampleRate, int n)
        {
            const auto fc = cutoffHz / sampleRate;
            if (n == 0)
                return 2.0 * fc;
            return std::sin (2.0 * std::numbers::pi * fc * n) / (std::numbers::pi * n);
        }
    }

    int BandSolo::firLengthFor (double rate) noexcept
    {
        return 2 * (int) std::lround (rate * firLengthSeconds * 0.5) + 1;
    }

    std::vector<float> BandSolo::designBandFir (int band, double rate, int length)
    {
        jassert (length % 2 == 1);
        const auto half = length / 2;
        std::vector<float> fir ((size_t) length, 0.0f);

        if (band == off)
        {
            fir[(size_t) half] = 1.0f;
            return fir;
        }

        const auto& edges = analysis::stereoBandEdges;
        const auto low = band == 0 ? 0.0 : (double) edges[(size_t) band];
        const auto high = band == numBands - 1 ? -1.0 : (double) edges[(size_t) band + 1];
        const auto i0Beta = besselI0 (kaiserBeta);

        for (int i = 0; i < length; ++i)
        {
            const auto n = i - half;
            const auto x = (double) n / half;
            const auto window = besselI0 (kaiserBeta * std::sqrt (std::max (0.0, 1.0 - x * x))) / i0Beta;

            // Band = low-pass(high edge) - low-pass(low edge); the top band uses delta - low-pass.
            const auto upper = high < 0.0 ? (n == 0 ? 1.0 : 0.0) : lowPass (high, rate, n) * window;
            const auto lower = low > 0.0 ? lowPass (low, rate, n) * window : 0.0;
            fir[(size_t) i] = (float) (upper - lower);
        }

        return fir;
    }

    void BandSolo::prepare (double rate, int maxBlockSize, int numChannels)
    {
        sampleRate = rate;
        firLength = firLengthFor (rate);

        const juce::dsp::ProcessSpec spec { rate, (juce::uint32) juce::jmax (1, maxBlockSize), (juce::uint32) juce::jmax (1, numChannels) };

        const auto& edges = analysis::stereoBandEdges;
        for (auto* f : { &splitMid, &splitLow, &splitHigh })
        {
            f->setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
            f->prepare (spec);
        }
        splitLow.setCutoffFrequency (edges[1]);
        splitMid.setCutoffFrequency (edges[2]);
        splitHigh.setCutoffFrequency (edges[3]);

        for (size_t i = 0; i < bandGains.size(); ++i)
        {
            bandGains[i].reset (rate, 0.01);
            bandGains[i].setCurrentAndTargetValue (i == (size_t) (targetBand + 1) ? 1.0f : 0.0f);
        }

        convolution.prepare (spec);
        loadedLinearBand = -2;
        updateLinearPhaseBand (targetBand);

        switchLength = juce::jmax (1, (int) std::lround (rate * 0.005));
        activeMode = requestedMode;
        switchRamp = switchLength;
        switchingOut = false;
    }

    void BandSolo::reset() noexcept
    {
        splitMid.reset();
        splitLow.reset();
        splitHigh.reset();
        convolution.reset();
    }

    void BandSolo::updateLinearPhaseBand (int band)
    {
        if (band == loadedLinearBand.load())
            return;

        auto fir = designBandFir (band, sampleRate, firLength);
        juce::AudioBuffer<float> ir (1, (int) fir.size());
        std::copy (fir.begin(), fir.end(), ir.getWritePointer (0));

        convolution.loadImpulseResponse (std::move (ir), sampleRate, juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        loadedLinearBand = band;
    }

    bool BandSolo::isTransparent() const noexcept
    {
        if (activeMode != Mode::zeroLatency || requestedMode != Mode::zeroLatency || targetBand != off)
            return false;

        for (size_t i = 1; i < bandGains.size(); ++i)
            if (bandGains[i].isSmoothing() || bandGains[i].getTargetValue() > 0.0f)
                return false;

        return ! bandGains[0].isSmoothing();
    }

    void BandSolo::processZeroLatency (float* const* channels, int numChannels, int numSamples) noexcept
    {
        for (size_t i = 0; i < bandGains.size(); ++i)
            bandGains[i].setTargetValue (i == (size_t) (targetBand + 1) ? 1.0f : 0.0f);

        for (int n = 0; n < numSamples; ++n)
        {
            std::array<float, numBands + 1> g;
            for (size_t i = 0; i < g.size(); ++i)
                g[i] = bandGains[i].getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto x = channels[ch][n];
                float lowHalf, highHalf, b0, b1, b2, b3;
                splitMid.processSample (ch, x, lowHalf, highHalf);
                splitLow.processSample (ch, lowHalf, b0, b1);
                splitHigh.processSample (ch, highHalf, b2, b3);

                channels[ch][n] = g[0] * x + g[1] * b0 + g[2] * b1 + g[3] * b2 + g[4] * b3;
            }
        }
    }

    void BandSolo::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (requestedMode != activeMode && ! switchingOut)
            switchingOut = true;

        if (activeMode == Mode::linearPhase)
        {
            juce::dsp::AudioBlock<float> block (channels, (size_t) numChannels, (size_t) numSamples);
            convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
        }
        else
        {
            processZeroLatency (channels, numChannels, numSamples);
        }

        // Mode switch envelope.
        if (switchingOut || switchRamp < switchLength)
        {
            for (int n = 0; n < numSamples; ++n)
            {
                if (switchingOut)
                {
                    if (switchRamp > 0)
                        --switchRamp;
                    else
                    {
                        // Silent: swap processing paths, clearing their history.
                        activeMode = requestedMode;
                        reset();
                        switchingOut = false;
                    }
                }
                else if (switchRamp < switchLength)
                {
                    ++switchRamp;
                }

                const auto g = (float) switchRamp / (float) switchLength;
                for (int ch = 0; ch < numChannels; ++ch)
                    channels[ch][n] *= g;
            }
        }
    }
}
