#include "Resampler.h"

#include <cmath>
#include <numbers>
#include <numeric>

namespace benchmark
{
    namespace
    {
        double besselI0 (double x)
        {
            double sum = 1.0, term = 1.0;
            const auto halfX = x * 0.5;

            for (int k = 1; k < 64; ++k)
            {
                term *= (halfX / k) * (halfX / k);
                sum += term;
                if (term < sum * 1e-17)
                    break;
            }
            return sum;
        }

        struct Ratio
        {
            bool rational = false;
            int64_t up = 1, down = 1; // output n sits at input position n * down / up
        };

        Ratio findRatio (double sourceRate, double targetRate)
        {
            const auto s = std::llround (sourceRate);
            const auto t = std::llround (targetRate);

            if (std::abs (sourceRate - (double) s) < 1e-6 && std::abs (targetRate - (double) t) < 1e-6 && s > 0 && t > 0)
            {
                const auto g = std::gcd (s, t);
                return { true, t / g, s / g };
            }

            return {};
        }

        /** numPhases rows of numTaps coefficients; row p is for fractional delay p / numPhases. */
        struct FilterBank
        {
            int numPhases = 0, numTaps = 0;
            std::vector<float> coefficients;

            FilterBank (int phases, double cutoff)
            {
                numPhases = phases;
                const auto halfTaps = (int) std::ceil (Resampler::zeroCrossings / cutoff);
                numTaps = halfTaps * 2;
                coefficients.resize ((size_t) numPhases * (size_t) numTaps);

                const auto i0Beta = besselI0 (Resampler::kaiserBeta);

                for (int p = 0; p < numPhases; ++p)
                {
                    const auto frac = (double) p / numPhases;
                    auto* row = coefficients.data() + (size_t) p * (size_t) numTaps;
                    double sum = 0.0;

                    for (int k = 0; k < numTaps; ++k)
                    {
                        // Tap k reads input sample (i - halfTaps + 1 + k); distance from the output instant:
                        const auto d = frac - (double) (k - halfTaps + 1);
                        const auto x = d / halfTaps;
                        const auto window = std::abs (x) < 1.0 ? besselI0 (Resampler::kaiserBeta * std::sqrt (1.0 - x * x)) / i0Beta : 0.0;
                        const auto arg = std::numbers::pi * cutoff * d;
                        const auto sinc = std::abs (arg) < 1e-12 ? 1.0 : std::sin (arg) / arg;
                        const auto h = cutoff * sinc * window;
                        row[k] = (float) h;
                        sum += h;
                    }

                    // Unity DC gain for every phase.
                    for (int k = 0; k < numTaps; ++k)
                        row[k] = (float) (row[k] / sum);
                }
            }

            const float* row (int p) const noexcept { return coefficients.data() + (size_t) p * (size_t) numTaps; }
        };
    }

    int64_t Resampler::getOutputLength (int64_t inputLength, double sourceRate, double targetRate)
    {
        if (inputLength <= 0)
            return 0;

        if (const auto r = findRatio (sourceRate, targetRate); r.rational)
            return (inputLength * r.up + r.down - 1) / r.down;

        return (int64_t) std::ceil ((double) inputLength * targetRate / sourceRate);
    }

    juce::AudioBuffer<float> Resampler::process (const juce::AudioBuffer<float>& input,
                                                 double sourceRate, double targetRate,
                                                 const ProgressCallback& progress)
    {
        jassert (sourceRate > 0.0 && targetRate > 0.0);

        const auto numChannels = input.getNumChannels();
        const auto inputLength = (int64_t) input.getNumSamples();

        if (std::abs (sourceRate - targetRate) < 1e-6 || inputLength == 0)
            return juce::AudioBuffer<float> (input);

        const auto ratio = findRatio (sourceRate, targetRate);
        const auto outputLength = getOutputLength (inputLength, sourceRate, targetRate);
        const auto cutoff = std::min (1.0, targetRate / sourceRate) * passbandFraction;
        const auto phases = (ratio.rational && ratio.up <= 4096) ? (int) ratio.up : 4096;

        const FilterBank bank (phases, cutoff);
        const auto halfTaps = bank.numTaps / 2;

        juce::AudioBuffer<float> output (numChannels, (int) outputLength);
        std::vector<float> padded ((size_t) (inputLength + bank.numTaps * 2), 0.0f);

        const auto totalWork = (double) outputLength * numChannels;
        constexpr int64_t progressInterval = 1 << 15;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // Zero padding either side removes all bounds checks from the inner loop.
            std::copy (input.getReadPointer (ch), input.getReadPointer (ch) + inputLength, padded.begin() + bank.numTaps);
            const auto* base = padded.data() + bank.numTaps - halfTaps + 1;
            auto* out = output.getWritePointer (ch);

            for (int64_t n = 0; n < outputLength; ++n)
            {
                int64_t index;
                int phase;

                if (phases == (int) ratio.up && ratio.rational)
                {
                    const auto position = n * ratio.down;
                    index = position / ratio.up;
                    phase = (int) (position % ratio.up);
                }
                else
                {
                    const auto position = (double) n * sourceRate / targetRate;
                    index = (int64_t) std::floor (position);
                    phase = (int) std::lround ((position - (double) index) * phases);

                    if (phase == phases)
                    {
                        phase = 0;
                        ++index;
                    }
                }

                const auto* taps = bank.row (phase);
                const auto* x = base + index;
                float acc = 0.0f;

                for (int k = 0; k < bank.numTaps; ++k)
                    acc += x[k] * taps[k];

                out[n] = acc;

                if (progress && (n % progressInterval) == 0)
                    if (! progress ((float) (((double) ch * outputLength + n) / totalWork)))
                        return {};
            }
        }

        if (progress)
            progress (1.0f);

        return output;
    }
}
