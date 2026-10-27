#include "KWeighting.h"

#include <cmath>
#include <numbers>

namespace benchmark::dsp
{
    // Analogue prototype parameters that reproduce the BS.1770 48 kHz coefficients.
    BiquadCoefficients KWeighting::shelf (double sampleRate) noexcept
    {
        constexpr double f0 = 1681.974450955533;
        constexpr double gainDb = 3.999843853973347;
        constexpr double q = 0.7071752369554196;

        const auto k = std::tan (std::numbers::pi * f0 / sampleRate);
        const auto vh = std::pow (10.0, gainDb / 20.0);
        const auto vb = std::pow (vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;

        return { (vh + vb * k / q + k * k) / a0,
                 2.0 * (k * k - vh) / a0,
                 (vh - vb * k / q + k * k) / a0,
                 2.0 * (k * k - 1.0) / a0,
                 (1.0 - k / q + k * k) / a0 };
    }

    BiquadCoefficients KWeighting::highPass (double sampleRate) noexcept
    {
        constexpr double f0 = 38.13547087602444;
        constexpr double q = 0.5003270373238773;

        const auto k = std::tan (std::numbers::pi * f0 / sampleRate);
        const auto a0 = 1.0 + k / q + k * k;

        return { 1.0, -2.0, 1.0,
                 2.0 * (k * k - 1.0) / a0,
                 (1.0 - k / q + k * k) / a0 };
    }
}
