#pragma once

#include <juce_dsp/juce_dsp.h>

namespace bf
{

/** Fixed-length stereo delay used to keep every output path at the same latency. */
class LatencyDelay
{
public:
    void prepare (int delaySamples, int maxBlock);
    void process (float* left, float* right, int numSamples) noexcept;
    void reset() noexcept;

private:
    juce::AudioBuffer<float> ring;
    int delay = 0, writePos = 0;
};

/** Look-ahead peak limiter. Guaranteed not to exceed the ceiling: the gain curve is a sliding
    minimum over the look-ahead window, smoothed by a moving average of the same length (so it
    reaches the required gain exactly at the peak), with a smooth release. */
class LookaheadLimiter
{
public:
    void prepare (double sampleRate, int lookaheadSamples);
    void reset() noexcept;
    void process (float* left, float* right, int numSamples, bool enabled) noexcept;

    int getLatency() const noexcept { return lookahead; }
    void setCeilingDb (float db) noexcept { ceiling = juce::Decibels::decibelsToGain (db); }

private:
    int lookahead = 64;
    float ceiling = 0.966f; // -0.3 dBFS
    float releaseCoef = 0.999f;
    float gain = 1.0f;

    // Sliding-window minimum: a monotonic queue in a fixed ring (no allocation).
    std::vector<float> minValues;
    std::vector<juce::int64> minIndices;
    int minHead = 0, minTail = 0;
    juce::int64 counter = 0;

    std::vector<float> avgRing;
    int avgPos = 0;
    double avgSum = 0.0;

    juce::AudioBuffer<float> delayLine;
    int delayPos = 0;
};

/** Master section: one-knob Drive (2x oversampled saturation) then the limiter. Latency is
    constant whatever is bypassed, so toggling never shifts the timing in the host. */
class MasterBus
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;
    void process (juce::AudioBuffer<float>& stereo, float drive, bool driveOn, bool limiterOn) noexcept;

    int getLatency() const noexcept { return latency; }

private:
    void processChunk (juce::dsp::AudioBlock<float>&, bool limiterOn) noexcept;

    int maxBlock = 512;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    LookaheadLimiter limiter;
    juce::SmoothedValue<float> driveSmoothed;
    int latency = 0;
};

} // namespace bf
