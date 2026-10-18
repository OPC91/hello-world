#include "Crossfader.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace benchmark::dsp
{
    void Crossfader::prepare (double sampleRate, double fadeMs) noexcept
    {
        const auto wasB = targetStep > 0;
        fadeSamples = std::max (1, (int) std::lround (sampleRate * fadeMs / 1000.0));
        setTarget (wasB);
        jumpToTarget();
    }

    void Crossfader::jumpToTarget() noexcept
    {
        step = targetStep;
        updateGains();
    }

    void Crossfader::updateGains() noexcept
    {
        if (step <= 0)                { gA = 1.0f; gB = 0.0f; }
        else if (step >= fadeSamples) { gA = 0.0f; gB = 1.0f; }
        else
        {
            const auto angle = (double) step / fadeSamples * (std::numbers::pi * 0.5);
            gA = (float) std::cos (angle);
            gB = (float) std::sin (angle);
        }
    }

    void Crossfader::next (float& gainA, float& gainB) noexcept
    {
        if (step != targetStep)
        {
            step += step < targetStep ? 1 : -1;
            updateGains();
        }

        gainA = gA;
        gainB = gB;
    }
}
