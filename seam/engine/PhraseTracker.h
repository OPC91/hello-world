#pragma once

namespace seam::engine
{
    /** What the host told us at the start of a block. Pure data, filled from the JUCE playhead by the plugin. */
    struct TransportSnapshot
    {
        bool playing = false;
        bool hasPosition = false;     ///< false if the host gave no PPQ position (then nothing is tempo-synced)
        double ppq = 0.0;             ///< quarter notes since the project start, at the first sample of the block
        double bpm = 120.0;
        int timeSigNumerator = 4;
        int timeSigDenominator = 4;
        double sampleRate = 44100.0;
        int numSamples = 0;
    };

    /** Where the block sits in the phrase grid. All positions are in quarter notes (PPQ). */
    struct PhrasePosition
    {
        bool valid = false;           ///< playing, with a usable tempo and position
        bool jumped = false;          ///< the position does not follow on from the previous block (loop, seek, start)

        double blockStartPpq = 0.0;   ///< host position at sample 0
        double blockEndPpq = 0.0;     ///< position just after the last sample
        /** Start of the half-open range [eventFromPpq, blockEndPpq) in which events belong to this block. It equals the
            previous block's end when playback is continuous, so an event can never fire twice or fall between two blocks,
            even if the host's positions are rounded. */
        double eventFromPpq = 0.0;
        double samplesPerQuarter = 0.0;
        double quartersPerSample = 0.0;

        double quartersPerBar = 4.0;
        double quartersPerBeat = 1.0; ///< length of one time-signature beat (1/4 in 4/4, 1/8 in 7/8)
        int phraseBars = 16;

        double barsSinceStart = 0.0;  ///< fractional bar count at the block start (bar 0 starts at PPQ 0)
        int barInPhrase = 0;          ///< 0 .. phraseBars-1
        double phraseStartPpq = 0.0;  ///< start of the current phrase
        double seamPpq = 0.0;         ///< the next seam: the downbeat that starts the next phrase
        double barsToSeam = 0.0;      ///< fractional bars from the block start to the seam (> 0)

        /** Sample index in this block of a position (may be outside [0, numSamples)). Rounded down, so the event
            plays on the first sample at or after its exact position. */
        long long sampleAt (double eventPpq) const noexcept;
        /** Position of a sample in this block. */
        double ppqAt (double sample) const noexcept { return blockStartPpq + sample * quartersPerSample; }
        /** Progress through the phrase, 0 at the phrase start and 1 at the seam, at the block start. */
        double phraseProgress() const noexcept;
    };

    /** Turns the host transport into phrase positions, block by block.

        The position is recalculated from the host's PPQ every block, never accumulated, so loops, seeks and tempo
        changes are followed immediately. Bars are counted from PPQ 0 using the current time signature (the bar grid
        assumes the time signature does not change during the song). */
    class PhraseTracker
    {
    public:
        static constexpr int phraseChoices[] = { 8, 16, 32 };

        void reset() noexcept { havePrevious = false; }

        PhrasePosition update (const TransportSnapshot& transport, int phraseBars) noexcept;

        /** Phrase-grid maths, exposed for the fill engine and tests. */
        static double quartersPerBar (int numerator, int denominator) noexcept;
        /** Floor that treats values within a hair below an integer as that integer (PPQ rounding from hosts). */
        static double gridFloor (double value) noexcept;

    private:
        bool havePrevious = false;
        double previousEndPpq = 0.0;
        double previousQuartersPerSample = 0.0;
    };
}
