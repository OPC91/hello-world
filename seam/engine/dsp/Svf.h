#pragma once

#include <cmath>

namespace seam::engine::dsp
{
    /** Topology-preserving state-variable filter (Zavalishin). Stable under fast cutoff modulation, so the
        high-pass can sweep per sample without zipper noise or blow-ups. */
    class Svf
    {
    public:
        struct Out { float low, band, high; };

        void setCoefficients (double cutoffHz, double q, double sampleRate) noexcept
        {
            const double fc = std::fmin (cutoffHz, sampleRate * 0.45);
            g = static_cast<float> (std::tan (3.14159265358979323846 * fc / sampleRate));
            k = static_cast<float> (1.0 / (q > 0.05 ? q : 0.05));
            a1 = 1.0f / (1.0f + g * (g + k));
        }

        Out tick (float x) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * (ic1 + g * v3);
            const float v2 = ic2 + g * v1;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return { v2, v1, x - k * v1 - v2 };
        }

        void reset() noexcept { ic1 = ic2 = 0.0f; }

    private:
        float g = 0.0f, k = 1.41421356f, a1 = 1.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
    };

    /** One-pole smoother towards a target. */
    struct Smoother
    {
        float value = 0.0f;
        float coeff = 0.0f;

        void setTime (double seconds, double sampleRate) noexcept
        {
            coeff = seconds > 0.0 ? static_cast<float> (std::exp (-1.0 / (seconds * sampleRate))) : 0.0f;
        }
        float next (float target) noexcept
        {
            const float stepped = target + (value - target) * coeff;
            // Snap when close, or when float rounding stops the step making progress (it would stall short of the
            // target forever, which matters for exact results at 0 and 1).
            value = (stepped == value || std::fabs (stepped - target) < 1.0e-6f) ? target : stepped;
            return value;
        }
    };

    /** Envelope follower with separate attack and release. */
    struct Follower
    {
        float env = 0.0f, attack = 0.0f, release = 0.0f;

        void setTimes (double attackSeconds, double releaseSeconds, double sampleRate) noexcept
        {
            attack = static_cast<float> (std::exp (-1.0 / (attackSeconds * sampleRate)));
            release = static_cast<float> (std::exp (-1.0 / (releaseSeconds * sampleRate)));
        }
        float next (float x) noexcept
        {
            const float a = std::fabs (x);
            env = a + (env - a) * (a > env ? attack : release);
            return env;
        }
    };
}
