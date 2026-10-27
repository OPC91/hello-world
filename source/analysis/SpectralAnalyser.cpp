#include "SpectralAnalyser.h"
#include "dsp/KWeighting.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace benchmark::analysis
{
    namespace
    {
        /** |H(e^jw)|^2 of a biquad. */
        double biquadPowerResponse (const dsp::BiquadCoefficients& c, double frequency, double sampleRate)
        {
            const auto w = 2.0 * std::numbers::pi * frequency / sampleRate;
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
            const auto num = c.b0 + c.b1 * z1 + c.b2 * z2;
            const auto den = 1.0 + c.a1 * z1 + c.a2 * z2;
            return std::norm (num / den);
        }

        double toDb (double power) { return power > 0.0 ? 10.0 * std::log10 (power) : -std::numeric_limits<double>::infinity(); }
    }

    int SpectralAnalyser::fftOrderFor (double rate) noexcept
    {
        const auto order = (int) std::ceil (std::log2 (rate / 6.0));
        return juce::jlimit (12, 15, order);
    }

    void SpectralAnalyser::prepare (double rate)
    {
        sampleRate = rate;
        fftOrder = fftOrderFor (rate);
        fftSize = 1 << fftOrder;
        hopSize = fftSize / 2;
        numBins = fftSize / 2 + 1;
        binWidth = (float) (rate / fftSize);
        liveAlpha = 1.0 - std::exp (-(hopSize / rate) / liveTimeConstantSeconds);

        fft = std::make_unique<juce::dsp::FFT> (fftOrder);

        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * std::numbers::pi_v<float> * (float) i / (float) fftSize);

        pendingL.assign ((size_t) fftSize, 0.0f);
        pendingR.assign ((size_t) fftSize, 0.0f);
        fftL.assign ((size_t) fftSize * 2, 0.0f);
        fftR.assign ((size_t) fftSize * 2, 0.0f);

        const auto shelf = dsp::KWeighting::shelf (rate);
        const auto highPass = dsp::KWeighting::highPass (rate);
        kWeight.resize ((size_t) numBins);
        stereoBandOfBin.assign ((size_t) numBins, -1);

        for (int k = 0; k < numBins; ++k)
        {
            const auto f = (double) k * binWidth;
            kWeight[(size_t) k] = (float) (biquadPowerResponse (shelf, f, rate) * biquadPowerResponse (highPass, f, rate));

            for (int b = 0; b < numStereoBands; ++b)
                if (f >= stereoBandEdges[(size_t) b] && f < stereoBandEdges[(size_t) b + 1])
                    stereoBandOfBin[(size_t) k] = b;
        }

        for (int i = 0; i < numCurvePoints; ++i)
        {
            const auto f = curvePointFrequency (i);
            curveRanges[(size_t) i] = makeRange (thirdOctaveLow (f), thirdOctaveHigh (f));
        }

        for (int i = 0; i < numThirdOctaveBands; ++i)
        {
            const auto f = thirdOctaveCentres[(size_t) i];
            bandRanges[(size_t) i] = makeRange (thirdOctaveLow (f), thirdOctaveHigh (f));
        }

        for (int i = 0; i < numTonalRanges; ++i)
            tonalRangeBins[(size_t) i] = makeRange (tonalRanges[(size_t) i].low, tonalRanges[(size_t) i].high);

        average.power.assign ((size_t) numBins, 0.0);
        live.power.assign ((size_t) numBins, 0.0);
        reset();
    }

    SpectralAnalyser::BinRange SpectralAnalyser::makeRange (float low, float high) const
    {
        const auto nyquistBin = numBins - 1;
        high = std::min (high, (float) (sampleRate * 0.5));

        BinRange r;
        r.bandwidthHz = std::max (0.0f, high - low);
        r.first = juce::jlimit (1, nyquistBin, (int) std::ceil (low / binWidth));
        r.last = juce::jlimit (1, nyquistBin, (int) std::ceil (high / binWidth) - 1);

        if (r.last < r.first)
        {
            // Narrower than a bin: use the nearest bin.
            r.first = r.last = juce::jlimit (1, nyquistBin, (int) std::lround (std::sqrt (low * high) / binWidth));
        }

        return r;
    }

    void SpectralAnalyser::reset() noexcept
    {
        std::fill (average.power.begin(), average.power.end(), 0.0);
        std::fill (live.power.begin(), live.power.end(), 0.0);
        for (auto* acc : { &average, &live })
        {
            acc->left.fill (0.0);
            acc->right.fill (0.0);
            acc->cross.fill (0.0);
        }

        averageFrames = 0;
        liveValid = false;
        pendingCount = 0;
    }

    void SpectralAnalyser::process (const float* left, const float* right, int numFrames) noexcept
    {
        int done = 0;

        while (done < numFrames)
        {
            const auto todo = std::min (numFrames - done, fftSize - pendingCount);
            std::copy (left + done, left + done + todo, pendingL.begin() + pendingCount);
            std::copy (right + done, right + done + todo, pendingR.begin() + pendingCount);
            pendingCount += todo;
            done += todo;

            if (pendingCount == fftSize)
            {
                analyseFrame();

                // Keep the second half for the next (50 % overlapping) frame.
                std::copy (pendingL.begin() + hopSize, pendingL.end(), pendingL.begin());
                std::copy (pendingR.begin() + hopSize, pendingR.end(), pendingR.begin());
                pendingCount = fftSize - hopSize;
            }
        }
    }

    void SpectralAnalyser::analyseFrame() noexcept
    {
        double meanPower = 0.0;
        for (int i = 0; i < fftSize; ++i)
            meanPower += (double) pendingL[(size_t) i] * pendingL[(size_t) i] + (double) pendingR[(size_t) i] * pendingR[(size_t) i];
        meanPower /= 2.0 * fftSize;

        if (meanPower < silenceThreshold)
            return;

        for (int i = 0; i < fftSize; ++i)
        {
            fftL[(size_t) i] = pendingL[(size_t) i] * window[(size_t) i];
            fftR[(size_t) i] = pendingR[(size_t) i] * window[(size_t) i];
        }
        std::fill (fftL.begin() + fftSize, fftL.end(), 0.0f);
        std::fill (fftR.begin() + fftSize, fftR.end(), 0.0f);

        fft->performRealOnlyForwardTransform (fftL.data(), true);
        fft->performRealOnlyForwardTransform (fftR.data(), true);

        const auto alpha = liveValid ? liveAlpha : 1.0;
        std::array<double, numStereoBands> frameL {}, frameR {}, frameCross {};

        for (int k = 0; k < numBins; ++k)
        {
            const std::complex<double> l (fftL[(size_t) k * 2], fftL[(size_t) k * 2 + 1]);
            const std::complex<double> r (fftR[(size_t) k * 2], fftR[(size_t) k * 2 + 1]);
            const auto pL = std::norm (l);
            const auto pR = std::norm (r);
            const auto power = 0.5 * (pL + pR);

            average.power[(size_t) k] += power;
            live.power[(size_t) k] += alpha * (power - live.power[(size_t) k]);

            if (const auto band = stereoBandOfBin[(size_t) k]; band >= 0)
            {
                frameL[(size_t) band] += pL;
                frameR[(size_t) band] += pR;
                frameCross[(size_t) band] += (l * std::conj (r)).real();
            }
        }

        for (size_t b = 0; b < (size_t) numStereoBands; ++b)
        {
            average.left[b] += frameL[b];
            average.right[b] += frameR[b];
            average.cross[b] += frameCross[b];
            live.left[b] += alpha * (frameL[b] - live.left[b]);
            live.right[b] += alpha * (frameR[b] - live.right[b]);
            live.cross[b] += alpha * (frameCross[b] - live.cross[b]);
        }

        ++averageFrames;
        liveValid = true;
    }

    float SpectralAnalyser::rangeDb (const std::vector<double>& power, const BinRange& range, double offsetDb) const noexcept
    {
        double sum = 0.0;
        for (int k = range.first; k <= range.last; ++k)
            sum += power[(size_t) k];

        // Power density times bandwidth, so narrow low bands are measured like wide ones.
        const auto mean = sum / (range.last - range.first + 1);
        return (float) (toDb (mean * range.bandwidthHz / binWidth) + offsetDb);
    }

    void SpectralAnalyser::fillView (SpectralView& view, const Accumulator& acc, bool valid) const
    {
        view = SpectralView();
        view.valid = valid;

        if (! valid)
            return;

        double kPower = 0.0;
        for (int k = 0; k < numBins; ++k)
            kPower += acc.power[(size_t) k] * kWeight[(size_t) k];

        if (kPower <= 0.0)
        {
            view.valid = false;
            return;
        }

        const auto offset = -toDb (kPower);

        for (int i = 0; i < numCurvePoints; ++i)
            view.curve[(size_t) i] = curvePointFrequency (i) < sampleRate * 0.5 ? rangeDb (acc.power, curveRanges[(size_t) i], offset) : noValue;

        for (int i = 0; i < numThirdOctaveBands; ++i)
            view.bands[(size_t) i] = thirdOctaveCentres[(size_t) i] < sampleRate * 0.5 ? rangeDb (acc.power, bandRanges[(size_t) i], offset) : noValue;

        for (int i = 0; i < numTonalRanges; ++i)
            view.ranges[(size_t) i] = rangeDb (acc.power, tonalRangeBins[(size_t) i], offset);

        for (size_t b = 0; b < (size_t) numStereoBands; ++b)
        {
            const auto l = acc.left[b], r = acc.right[b], c = acc.cross[b];
            const auto mid = std::max (0.0, (l + r + 2.0 * c) * 0.25);
            const auto side = std::max (0.0, (l + r - 2.0 * c) * 0.25);
            auto& out = view.stereo[b];

            if (l + r <= 0.0)
                continue;

            out.sideToMidDb = mid > 0.0 ? (float) toDb (side / mid) : 99.0f;
            out.width = (float) (side / (mid + side));
            out.correlation = (l > 0.0 && r > 0.0) ? (float) juce::jlimit (-1.0, 1.0, c / std::sqrt (l * r)) : 0.0f;
        }
    }

    void SpectralAnalyser::fillViews (SpectralView& averageView, SpectralView& liveView) const
    {
        fillView (averageView, average, averageFrames > 0);
        fillView (liveView, live, liveValid);
    }
}
