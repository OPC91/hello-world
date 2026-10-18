#pragma once

namespace benchmark::dsp
{
    /**
        Equal-power crossfade between two uncorrelated sources (Mix and Reference).
        Gains follow cos/sin of the fade position, so gainA^2 + gainB^2 == 1 at
        every sample. Retargeting mid-fade reverses smoothly from the current point.
        Real-time safe.
    */
    class Crossfader
    {
    public:
        static constexpr double defaultFadeMs = 15.0;

        void prepare (double sampleRate, double fadeMs = defaultFadeMs) noexcept;

        /** false = source A (Mix), true = source B (Reference). */
        void setTarget (bool toB) noexcept { targetStep = toB ? fadeSamples : 0; }
        void jumpToTarget() noexcept;

        bool isSettled() const noexcept { return step == targetStep; }
        bool isFullyA() const noexcept { return step == 0 && targetStep == 0; }
        float getPosition() const noexcept { return (float) step / (float) fadeSamples; }
        int getFadeLengthSamples() const noexcept { return fadeSamples; }

        /** Advances one sample and returns the gains for A and B. */
        void next (float& gainA, float& gainB) noexcept;

    private:
        void updateGains() noexcept;

        int fadeSamples = 720;
        int step = 0, targetStep = 0; // position in samples, 0 = A, fadeSamples = B
        float gA = 1.0f, gB = 0.0f;
    };
}
