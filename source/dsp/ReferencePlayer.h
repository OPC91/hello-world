#pragma once

#include <cstdint>

namespace benchmark
{
    struct ReferenceAudio;
}

namespace benchmark::dsp
{
    /**
        Plays the active reference on the audio thread, looping at the end.
        Converts channel counts (mono file -> both outputs, stereo file -> mono
        output as (L + R) / 2). Real-time safe.
    */
    class ReferencePlayer
    {
    public:
        /** Renders numSamples into out. Resets the playhead when the reference changes. */
        void render (const ReferenceAudio& reference, float* const* out, int numOutChannels, int numSamples) noexcept;

        /** Advances the playhead without producing audio. */
        void advance (const ReferenceAudio& reference, int numSamples) noexcept;

        int64_t getPosition() const noexcept { return position; }
        void reset() noexcept { position = 0; current = nullptr; }

    private:
        void syncTo (const ReferenceAudio& reference) noexcept;

        const ReferenceAudio* current = nullptr;
        int64_t position = 0;
    };
}
