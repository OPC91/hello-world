#pragma once

#include "dsp/Svf.h"
#include "dsp/ThrowReverb.h"

#include <array>
#include <vector>

namespace seam::engine
{
    /** Advanced-panel scaling of each effect (1 = design amount, 0..1.5). */
    struct ChainAmounts
    {
        float filter = 1.0f, reverb = 1.0f, stutter = 1.0f, duck = 1.0f;
    };

    /** The effects TRANSITION drives, all moving together from one value (like Nil2Max's INTENSITY chain):
          1. drum-bus high-pass, sweeping up with rising resonance;
          2. reverb throw: snares (detected in the input, or triggered by the fill) open a send into a big reverb;
          3. beat-locked stutter: the start of each beat is captured and repeated in 1/8s, 1/16s, then 1/32s;
          4. ducking dip: a beat-synced dip that lets each transient through, then breathes.
        Real-time safe: prepare() allocates, everything else is allocation-free. */
    class TransitionChain
    {
    public:
        void prepare (double sampleRate);
        void reset() noexcept;

        /** Processes `n` stereo samples in place. `effective` and `ppq` hold one value per sample;
            when `tempoValid` is false the tempo-locked parts (stutter, duck) are off. */
        void process (float* left, float* right, int n, const float* effective, const double* ppq,
                      double samplesPerQuarter, bool tempoValid, const ChainAmounts&) noexcept;

        /** Opens the reverb send now (the fill engine calls this for its own snare hits). */
        void throwSnare (double samplesPerQuarter) noexcept;
        /** The drop: fade the reverb tail and stutter out fast (~10 ms), then clear them. */
        void cutTails() noexcept;
        /** Nothing ringing: the chain's output equals its filtered input. */
        bool isQuiet() const noexcept;

        // Curves, exposed for tests and the UI.
        static double cutoffFor (float effective, float amount) noexcept;
        static double stutterDivisionFor (float effective) noexcept;   ///< in quarter notes (0 = off)
        static float duckShape (double beatPhase) noexcept;

    private:
        double sampleRate = 44100.0;

        std::array<dsp::Svf, 2> highPass;
        dsp::Smoother cutoffLog2, resonance;
        int coefficientCountdown = 0;

        // Snare detection + throw.
        dsp::Svf detectBand, detectLow;
        dsp::Follower bandFast, bandSlow, lowFast;
        int refractory = 0, throwHold = 0;
        dsp::Smoother throwGate, sendLevel;
        dsp::ThrowReverb reverb;
        float tailFade = 1.0f, tailFadeStep = 0.0f;
        bool cuttingTails = false;

        // Stutter.
        std::vector<float> captureL, captureR;
        long long currentBeat = -1, capturedBeat = -2;
        double division = 0.0;
        dsp::Smoother stutterMix;
        int rampSamples = 64;

        dsp::Smoother duckDepth;
    };
}
