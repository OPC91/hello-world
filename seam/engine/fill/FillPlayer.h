#pragma once

#include "Break.h"
#include "FillGenerator.h"
#include "engine/PhraseTracker.h"

#include <array>

namespace seam::engine
{
    /** Plays the fill in the last bars before each seam, sample-accurate to the host grid.

        Stateless with respect to time: each block it works out which events fall inside the block's event range
        (see PhrasePosition::eventFromPpq), so loops and seeks just work. Fixed voice pool, no allocation.
        The main lane is monophonic (each hit chokes the last, like a chopped break); reversed hits have their own
        lane. Every voice is silent by the seam's first sample. */
    class FillPlayer
    {
    public:
        static constexpr int maxVoices = 8;
        static constexpr int maxTriggersPerChunk = 64;

        void prepare (double sampleRate);
        void reset() noexcept;

        struct Output
        {
            float* left;          ///< fill audio is ADDED here
            float* right;
            float* dryGain;       ///< multiplied in: how much of the incoming drums to keep (Replace mode)
            bool snareHit = false;   ///< a snare-type hit started in this chunk (for the reverb throw)
        };

        /** Renders samples [offset, offset + n) of the block. `effective` holds the effective TRANSITION per sample
            (the fill only starts hits while it is above 0 and fades in over the first 25%). `endOffset` (quarters)
            makes the fill finish that long before the seam (the vacuum's gap). */
        void render (const BreakKit*, const FillPattern*, const PhrasePosition&, int offset, int n, const float* effective,
                     float level, bool replace, Output&, double endOffset = 0.0) noexcept;

        /** Fast fade of every voice (loop/seek). */
        void killAll() noexcept;
        bool isActive() const noexcept;

        /** Where the fill window starts for the phrase containing `ppq` (exposed for tests and the UI). */
        static double windowStart (double ppq, const PhrasePosition&, const FillPattern&) noexcept;

    private:
        struct Voice
        {
            bool active = false;
            bool reverse = false;
            bool fxLane = false;
            const BreakKit* kit = nullptr;
            size_t sliceStart = 0, sliceLength = 0;
            double position = 0.0;     ///< in kit samples from the slice start (counts down when reversed)
            double rate = 1.0;
            float gain = 1.0f;
            int age = 0;
            int release = -1;          ///< samples left in a choke fade (-1 = not fading)
            double seamPpq = 0.0;      ///< hard stop
        };

        struct Trigger { int sample; int event; double seamPpq; long long reverseSkip; };

        void start (const BreakKit&, const FillEvent&, double seamPpq, double hostRate, long long reverseSkip) noexcept;

        std::array<Voice, maxVoices> voices {};
        std::array<Trigger, maxTriggersPerChunk> triggers {};
        double sampleRate = 44100.0;
        int fadeIn = 16, chokeFade = 96, seamFade = 64;
    };
}
