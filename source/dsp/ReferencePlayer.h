#pragma once

#include <cstdint>

namespace benchmark
{
    struct ReferenceAudio;
}

namespace benchmark::dsp
{
    /**
        Plays the active reference on the audio thread.

        - Seeks and loop wraps are de-clicked with a short equal-power crossfade
          between the old and new playback positions.
        - Pause / resume ramp the level instead of cutting it.
        - In wrap mode (Free) playback loops over the loop region, or the whole
          file; otherwise (Sync) it plays silence outside the file.
        - Channel counts are converted: mono file -> both outputs, stereo file
          -> mono output as (L + R) / 2.

        Real-time safe.
    */
    class ReferencePlayer
    {
    public:
        static constexpr double defaultFadeMs = 5.0;

        void prepare (double sampleRate, double fadeMs = defaultFadeMs) noexcept;
        void reset() noexcept;

        void setPlaying (bool shouldPlay) noexcept { playTarget = shouldPlay ? fadeLength : 0; }
        bool isPlaying() const noexcept { return playTarget > 0; }
        bool isSilent() const noexcept { return playRamp == 0 && playTarget == 0; }

        /** Jumps to a position; crossfades if audible. */
        void seek (int64_t newPosition) noexcept;

        void setLoop (int64_t start, int64_t end) noexcept;
        void clearLoop() noexcept { loopStart = loopEnd = 0; }
        bool hasLoop() const noexcept { return loopEnd > loopStart; }

        void setWrapAtEnd (bool shouldWrap) noexcept { wrap = shouldWrap; }

        /**
            Sets the reference to play. A different reference restarts from 0 with a
            fade-in. Call before seek() so a seek for the new reference is kept.
        */
        void setReference (const ReferenceAudio* reference) noexcept;

        /** Renders numSamples of the current reference (silence if none) into out. */
        void render (float* const* out, int numOutChannels, int numSamples) noexcept;

        int64_t getPosition() const noexcept { return position; }
        int getFadeLength() const noexcept { return fadeLength; }

    private:
        void startJump (int64_t target) noexcept;

        const ReferenceAudio* current = nullptr;
        int fadeLength = 240;

        int64_t position = 0;
        int64_t oldPosition = 0;    // voice being faded out after a jump
        int fadeRemaining = 0;

        int playRamp = 0, playTarget = 240; // 0 .. fadeLength

        int64_t loopStart = 0, loopEnd = 0;
        bool wrap = true;
    };
}
