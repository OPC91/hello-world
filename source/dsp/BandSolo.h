#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <vector>

namespace benchmark::dsp
{
    /**
        Solos one of four bands (Low / Low-mid / High-mid / High, crossovers at
        150 Hz, 1 kHz and 5 kHz) on the plugin output, so Mix and Reference are
        compared band against band.

        Two modes:
        - Zero latency: Linkwitz-Riley 4th-order crossovers (24 dB/oct). No
          latency; bands overlap at -6 dB around each crossover and phase turns
          near the crossover points.
        - Linear phase: windowed-sinc FIR bands (about 85 ms long) applied by
          partitioned convolution. Steep, phase-coherent bands that sum back to
          the input exactly, at the cost of getLinearPhaseLatency() samples of
          latency. In this mode the latency is constant, solo or not, so the host's
          delay compensation never has to change while playing.

        Band and mode changes are de-clicked. process() is real-time safe; the
        linear-phase impulse response is swapped from the message thread with
        updateLinearPhaseBand() (the convolution crossfades between them).
    */
    class BandSolo
    {
    public:
        enum class Mode { zeroLatency = 0, linearPhase = 1 };
        static constexpr int numBands = 4;
        static constexpr int off = -1;
        static constexpr double firLengthSeconds = 0.0853;
        static constexpr double kaiserBeta = 6.0;

        void prepare (double sampleRate, int maxBlockSize, int numChannels);
        void reset() noexcept;

        // Audio thread
        void setBand (int band) noexcept { targetBand = juce::jlimit (off, numBands - 1, band); }
        void setMode (Mode mode) noexcept { requestedMode = mode; }
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        /** True when process() would leave the signal untouched (zero-latency mode, solo off, settled). */
        bool isTransparent() const noexcept;

        Mode getActiveMode() const noexcept { return activeMode; }
        int getLinearPhaseLatency() const noexcept { return firLength / 2; }
        int getLatencyFor (Mode mode) const noexcept { return mode == Mode::linearPhase ? getLinearPhaseLatency() : 0; }

        // Message thread
        /** Loads the FIR for a band (or off = pure delay) into the convolution. */
        void updateLinearPhaseBand (int band);
        int getLoadedLinearPhaseBand() const noexcept { return loadedLinearBand.load(); }

        /** Linear-phase FIR for a band (off = centred unit impulse). Odd length, symmetric. */
        static std::vector<float> designBandFir (int band, double sampleRate, int length);
        static int firLengthFor (double sampleRate) noexcept;

    private:
        void processZeroLatency (float* const* channels, int numChannels, int numSamples) noexcept;

        double sampleRate = 48000.0;
        int firLength = 4097;

        // Zero-latency crossover tree: split at 1 kHz, then 150 Hz below and 5 kHz above.
        juce::dsp::LinkwitzRileyFilter<float> splitMid, splitLow, splitHigh;
        std::array<juce::SmoothedValue<float>, numBands + 1> bandGains; // [0] = full band, [1..4] = bands

        juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 256 } };
        std::atomic<int> loadedLinearBand { -2 };

        int targetBand = off;
        Mode requestedMode = Mode::zeroLatency, activeMode = Mode::zeroLatency;

        // Mode switch: fade out, switch, fade in.
        int switchRamp = 0, switchLength = 256;
        bool switchingOut = false;
    };
}
