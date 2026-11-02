#pragma once

#include "PhraseTracker.h"

namespace seam::engine
{
    /** Everything the user controls, read once per block. Plain values, no JUCE. */
    struct Settings
    {
        float transition = 0.0f;     ///< 0..1, the hero knob
        int style = 0;               ///< 0 Liquid, 1 Neuro, 2 Jungle, 3 Jump-Up
        float complexity = 0.5f;     ///< 0..1
        int fillBars = 1;            ///< 1, 2 or 4
        bool replace = false;        ///< fill replaces the incoming drums (else mixes over them)
        bool vacuum = false;
        double vacuumQuarters = 0.5; ///< gap length in quarter notes
        bool tapeStop = false;
        bool autoMode = false;
        int autoBars = 4;
        int autoCurve = 0;           ///< 0 linear, 1 late surge
        int phraseBars = 16;

        // Advanced panel, 0..1.5 (1 = the design amount).
        float amtFilter = 1.0f, amtReverb = 1.0f, amtStutter = 1.0f, amtDuck = 1.0f, amtFill = 1.0f;
    };

    /** What the UI shows, written by the audio thread each block. */
    struct Telemetry
    {
        bool playing = false;
        double barsToSeam = 0.0;
        int barInPhrase = 0;
        int phraseBars = 16;
        float beatPhase = -1.0f;     ///< 0..1 within the current beat, -1 when stopped
        float effective = 0.0f;      ///< transition after auto mode and the downbeat snap
        bool fillActive = false;
        bool bypassed = true;        ///< true while the block passed through untouched
        double barsPerBeat = 0.25;
    };

    /** The whole SEAM process: phrase tracking, the TRANSITION chain, fills and vacuum.

        Real-time safe: prepare() allocates everything; process() never allocates, locks or frees. */
    class SeamEngine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** Processes `numChannels` (1 or 2) channels in place. */
        void process (float* const* channels, int numChannels, int numSamples, const TransportSnapshot&, const Settings&) noexcept;

        const Telemetry& telemetry() const noexcept { return lastTelemetry; }
        const PhrasePosition& lastPosition() const noexcept { return position; }

    private:
        PhraseTracker tracker;
        PhrasePosition position;
        Telemetry lastTelemetry;
        double sampleRate = 44100.0;
        int maxBlock = 0;
    };
}
