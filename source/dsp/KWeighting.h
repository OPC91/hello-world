#pragma once

#include <array>

namespace benchmark::dsp
{
    /** Normalised biquad coefficients (a0 == 1). */
    struct BiquadCoefficients
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    /**
        ITU-R BS.1770-4 K-weighting (pre-filter shelf + RLB high-pass), derived
        for any sample rate from the analogue prototype so that the 48 kHz
        coefficients match the table in the recommendation.
    */
    struct KWeighting
    {
        static BiquadCoefficients shelf (double sampleRate) noexcept;
        static BiquadCoefficients highPass (double sampleRate) noexcept;
    };

    /** Transposed direct form II biquad with double-precision state. */
    class Biquad
    {
    public:
        void setCoefficients (const BiquadCoefficients& c) noexcept { coeffs = c; }
        void reset() noexcept { s1 = s2 = 0.0; }

        double process (double x) noexcept
        {
            const auto y = coeffs.b0 * x + s1;
            s1 = coeffs.b1 * x - coeffs.a1 * y + s2;
            s2 = coeffs.b2 * x - coeffs.a2 * y;
            return y;
        }

    private:
        BiquadCoefficients coeffs;
        double s1 = 0.0, s2 = 0.0;
    };

    /** Both K-weighting stages for one channel. */
    class KWeightingFilter
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            stage1.setCoefficients (KWeighting::shelf (sampleRate));
            stage2.setCoefficients (KWeighting::highPass (sampleRate));
            reset();
        }

        void reset() noexcept
        {
            stage1.reset();
            stage2.reset();
        }

        double process (double x) noexcept { return stage2.process (stage1.process (x)); }

    private:
        Biquad stage1, stage2;
    };
}
