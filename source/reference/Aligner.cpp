#include "Aligner.h"
#include "Resampler.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <complex>

namespace benchmark
{
    namespace
    {
        juce::AudioBuffer<float> monoOf (const juce::AudioBuffer<float>& b)
        {
            juce::AudioBuffer<float> mono (1, b.getNumSamples());
            mono.copyFrom (0, 0, b, 0, 0, b.getNumSamples());
            if (b.getNumChannels() > 1)
            {
                mono.addFrom (0, 0, b, 1, 0, b.getNumSamples());
                mono.applyGain (0.5f);
            }
            return mono;
        }

        /** Real FFT of x (zero padded to size), interleaved complex output of size + 2 floats. */
        std::vector<float> forward (juce::dsp::FFT& fft, const float* x, int n, int size)
        {
            std::vector<float> data ((size_t) size * 2, 0.0f);
            std::copy (x, x + n, data.begin());
            fft.performRealOnlyForwardTransform (data.data(), true);
            return data;
        }

        /** Normalised correlation of excerpt against reference[pos ...]. */
        double correlationAt (const float* ref, int64_t refLength, const float* ex, int exLength, int64_t pos)
        {
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (int i = 0; i < exLength; ++i)
            {
                const auto r = pos + i;
                const auto y = (r >= 0 && r < refLength) ? (double) ref[r] : 0.0;
                const auto x = (double) ex[i];
                xy += x * y;
                xx += x * x;
                yy += y * y;
            }
            return (xx > 0.0 && yy > 0.0) ? xy / std::sqrt (xx * yy) : 0.0;
        }
    }

    Aligner::Result Aligner::findExcerpt (const juce::AudioBuffer<float>& reference, const float* excerpt, int excerptLength,
                                          double sampleRate, const std::atomic<bool>* cancel)
    {
        Result result;
        const auto cancelled = [cancel] { return cancel != nullptr && cancel->load(); };

        if (reference.getNumSamples() < excerptLength || excerptLength < (int) (sampleRate * 2.0))
        {
            result.message = "Not enough audio to align";
            return result;
        }

        // Stage 1: coarse search at ~4 kHz with GCC-PHAT.
        const auto refMono = monoOf (reference);
        juce::AudioBuffer<float> exBuffer (1, excerptLength);
        exBuffer.copyFrom (0, 0, excerpt, excerptLength);

        const auto ratio = coarseRate / sampleRate;
        const auto refCoarse = Resampler::process (refMono, sampleRate, coarseRate, [&] (float) { return ! cancelled(); });
        const auto exCoarse = Resampler::process (exBuffer, sampleRate, coarseRate, [&] (float) { return ! cancelled(); });

        if (cancelled() || refCoarse.getNumSamples() == 0 || exCoarse.getNumSamples() == 0)
        {
            result.message = "Cancelled";
            return result;
        }

        const auto refN = refCoarse.getNumSamples();
        const auto exN = exCoarse.getNumSamples();
        const auto order = (int) std::ceil (std::log2 ((double) (refN + exN)));
        const auto size = 1 << order;

        juce::dsp::FFT fft (order);
        auto R = forward (fft, refCoarse.getReadPointer (0), refN, size);
        const auto E = forward (fft, exCoarse.getReadPointer (0), exN, size);

        // Cross-spectrum R * conj(E), phase only.
        for (int k = 0; k <= size / 2; ++k)
        {
            const std::complex<float> r (R[(size_t) k * 2], R[(size_t) k * 2 + 1]);
            const std::complex<float> e (E[(size_t) k * 2], E[(size_t) k * 2 + 1]);
            auto c = r * std::conj (e);
            const auto magnitude = std::abs (c);
            c = magnitude > 1e-20f ? c / magnitude : std::complex<float> {};
            R[(size_t) k * 2] = c.real();
            R[(size_t) k * 2 + 1] = c.imag();
        }

        fft.performRealOnlyInverseTransform (R.data());

        // Lags 0 .. refN - exN: the excerpt lies inside the reference.
        int bestLag = 0;
        float bestValue = -1.0e30f;
        for (int lag = 0; lag <= refN - exN; ++lag)
        {
            if (R[(size_t) lag] > bestValue)
            {
                bestValue = R[(size_t) lag];
                bestLag = lag;
            }
        }

        if (cancelled())
        {
            result.message = "Cancelled";
            return result;
        }

        // Stage 2: refine at the full rate around the coarse estimate.
        const auto* ref = refMono.getReadPointer (0);
        const auto refLength = (int64_t) refMono.getNumSamples();
        const auto centre = (int64_t) std::llround (bestLag / ratio);
        const auto searchRadius = (int64_t) std::ceil (2.0 / ratio) + 2;

        // Use a 4 s slice of the excerpt for speed; it is plenty for sample accuracy.
        const auto sliceLength = std::min (excerptLength, (int) (sampleRate * 4.0));
        const auto sliceStart = (excerptLength - sliceLength) / 2;

        int64_t bestPosition = centre;
        double bestCorrelation = -2.0;

        for (auto pos = centre - searchRadius; pos <= centre + searchRadius; ++pos)
        {
            const auto c = correlationAt (ref, refLength, excerpt + sliceStart, sliceLength, pos + sliceStart);
            if (c > bestCorrelation)
            {
                bestCorrelation = c;
                bestPosition = pos;
            }
        }

        // Confidence over the whole excerpt.
        const auto confidence = (float) std::max (0.0, correlationAt (ref, refLength, excerpt, excerptLength, bestPosition));

        result.referencePosition = bestPosition;
        result.confidence = confidence;
        result.found = confidence >= minimumConfidence;
        result.message = result.found ? juce::String ("Aligned")
                                      : "No confident match (" + juce::String (confidence, 2) + "): is this reference a version of the same song?";
        return result;
    }
}
