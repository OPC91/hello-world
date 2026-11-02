#pragma once

#include "PhraseTracker.h"
#include "TransitionChain.h"
#include "fill/Break.h"
#include "fill/FillGenerator.h"
#include "fill/FillPlayer.h"

#include <exnilo/RealtimeExchange.h>

#include <array>

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

        At an effective TRANSITION of 0, with nothing ringing, the audio is not touched at all (true bypass, bit for
        bit). Real-time safe: prepare() allocates everything; process() never allocates, locks or frees. */
    class SeamEngine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** Processes `numChannels` (1 or 2) channels in place. */
        void process (float* const* channels, int numChannels, int numSamples, const TransportSnapshot&, const Settings&) noexcept;

        /** Hand a new sliced break / fill pattern to the audio thread (any non-realtime thread; lock-free for the
            audio thread, which picks them up at the start of its next block). */
        void publishKit (std::unique_ptr<BreakKit> newKit) { kits.publish (std::move (newKit)); }
        void publishPattern (std::unique_ptr<FillPattern> newPattern) { patterns.publish (std::move (newPattern)); }
        /** Frees retired kits/patterns. Call regularly from a non-realtime thread. */
        void collectGarbage() { kits.collectGarbage(); patterns.collectGarbage(); }

        const Telemetry& telemetry() const noexcept { return lastTelemetry; }
        const PhrasePosition& lastPosition() const noexcept { return position; }

        /** The auto-mode ramp at a position (0 outside the ramp; drops to 0 exactly on the seam). */
        static float autoRampAt (double ppq, const PhrasePosition&, const Settings&) noexcept;

        static constexpr int chunkSize = 256;

    private:
        void processStopped (float* left, float* right, int numSamples, const Settings&) noexcept;
        void processChunk (float* left, float* right, int offset, int n, const Settings&) noexcept;
        void updateManual (float target, double ppq, bool atDownbeat) noexcept;

        PhraseTracker tracker;
        PhrasePosition position;
        Telemetry lastTelemetry;
        TransitionChain chain;
        FillPlayer fill;
        exnilo::RealtimeExchange<BreakKit> kits;
        exnilo::RealtimeExchange<FillPattern> patterns;
        const BreakKit* kit = nullptr;          ///< audio thread's current kit (owned by `kits`)
        const FillPattern* pattern = nullptr;   ///< audio thread's current pattern (owned by `patterns`)
        double sampleRate = 44100.0;

        // Effective transition state.
        double manual = 0.0;          ///< the knob after the downbeat snap (double: a float smoother stalls short of its target)
        bool dropPending = false;     ///< a big decrease is waiting for the next downbeat
        float previousEffective = 0.0f;
        long long previousBar = 0;
        bool haveBar = false;
        double riseCoeff = 0.0;

        // Dry/wet crossfade (so entering and leaving the chain never clicks).
        float mix = 0.0f, mixStep = 0.0f;
        bool chunkWasBypassed = true;

        std::array<float, chunkSize> dryL {}, dryR {}, effective {}, monoScratch {}, fillL {}, fillR {}, dryGain {};
        std::array<double, chunkSize> ppqs {};
    };
}
