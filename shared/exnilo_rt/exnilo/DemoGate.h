#pragma once

#include <algorithm>
#include <cstdint>

namespace exnilo
{
    /** Demo mode for every Exnilo plugin: the audio drops out for 2 seconds every 40 seconds (with 10 ms fades so it
        never clicks). Counts processed samples, so it is the same in every host. Real-time safe. */
    class DemoGate
    {
    public:
        static constexpr double periodSeconds = 40.0;
        static constexpr double muteSeconds = 2.0;
        static constexpr double fadeSeconds = 0.01;

        void prepare (double sampleRate) noexcept
        {
            period = static_cast<std::int64_t> (periodSeconds * sampleRate);
            mute = static_cast<std::int64_t> (muteSeconds * sampleRate);
            fade = std::max<std::int64_t> (1, static_cast<std::int64_t> (fadeSeconds * sampleRate));
        }

        void reset() noexcept { position = 0; }

        /** Gain for the sample `offset` samples into the current position (1 = audible). */
        float gainAt (std::int64_t offset) const noexcept
        {
            if (period <= 0)
                return 1.0f;
            const auto t = (position + offset) % period;
            const auto muteStart = period - mute;        // the last 2 s of every 40 s
            if (t < muteStart - fade) return 1.0f;
            if (t < muteStart) return static_cast<float> (muteStart - t) / static_cast<float> (fade);
            if (t >= period - fade) return 1.0f - static_cast<float> (period - t) / static_cast<float> (fade);
            return 0.0f;
        }

        /** Applies the gate to `numChannels` channels in place, unless licensed. */
        void process (float* const* channels, int numChannels, int numSamples, bool licensed) noexcept
        {
            if (! licensed)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    const float g = gainAt (i);
                    if (g < 1.0f)
                        for (int ch = 0; ch < numChannels; ++ch)
                            channels[ch][i] *= g;
                }
            }
            position = period > 0 ? (position + numSamples) % period : 0;
        }

    private:
        std::int64_t period = 0, mute = 0, fade = 1, position = 0;
    };
}
