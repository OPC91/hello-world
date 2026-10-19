#include "MasterBus.h"

#include <cmath>

namespace bf
{

void LatencyDelay::prepare (int delaySamples, int)
{
    delay = juce::jmax (0, delaySamples);
    ring.setSize (2, juce::jmax (1, delay), false, true, false);
    reset();
}

void LatencyDelay::reset() noexcept
{
    ring.clear();
    writePos = 0;
}

void LatencyDelay::process (float* left, float* right, int numSamples) noexcept
{
    if (delay == 0)
        return;

    float* rl = ring.getWritePointer (0);
    float* rr = ring.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        std::swap (left[i], rl[writePos]);
        std::swap (right[i], rr[writePos]);
        writePos = writePos + 1 == delay ? 0 : writePos + 1;
    }
}

// -----------------------------------------------------------------------------

void LookaheadLimiter::prepare (double sampleRate, int lookaheadSamples)
{
    lookahead = juce::jmax (1, lookaheadSamples);
    releaseCoef = (float) std::exp (-1.0 / (0.08 * sampleRate)); // 80 ms release

    minValues.assign ((size_t) lookahead + 2, 1.0f);
    minIndices.assign ((size_t) lookahead + 2, 0);
    avgRing.assign ((size_t) lookahead, 1.0f);
    delayLine.setSize (2, lookahead, false, true, false);
    reset();
}

void LookaheadLimiter::reset() noexcept
{
    gain = 1.0f;
    minHead = minTail = 0;
    counter = 0;
    std::fill (avgRing.begin(), avgRing.end(), 1.0f);
    avgPos = 0;
    avgSum = (double) lookahead;
    delayLine.clear();
    delayPos = 0;
}

void LookaheadLimiter::process (float* left, float* right, int numSamples, bool enabled) noexcept
{
    const int cap = (int) minValues.size();
    float* dl = delayLine.getWritePointer (0);
    float* dr = delayLine.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        const float peak = juce::jmax (std::abs (left[i]), std::abs (right[i]));
        const float required = peak > ceiling ? ceiling / peak : 1.0f;

        // Sliding minimum over the last (lookahead + 1) required gains.
        while (minTail != minHead && minValues[(size_t) ((minTail - 1 + cap) % cap)] >= required)
            minTail = (minTail - 1 + cap) % cap;

        minValues[(size_t) minTail] = required;
        minIndices[(size_t) minTail] = counter;
        minTail = (minTail + 1) % cap;

        while (minIndices[(size_t) minHead] < counter - lookahead)
            minHead = (minHead + 1) % cap;

        const float windowMin = minValues[(size_t) minHead];
        ++counter;

        // Moving average of the minimum: a smooth attack that lands exactly on the peak.
        avgSum += (double) windowMin - (double) avgRing[(size_t) avgPos];
        avgRing[(size_t) avgPos] = windowMin;
        avgPos = avgPos + 1 == lookahead ? 0 : avgPos + 1;
        const float target = (float) juce::jmin (1.0, avgSum / lookahead);

        gain = target < gain ? target : target + (gain - target) * releaseCoef;

        // Delay the audio by the look-ahead so the gain arrives in time.
        const float l = dl[delayPos], r = dr[delayPos];
        dl[delayPos] = left[i];
        dr[delayPos] = right[i];
        delayPos = delayPos + 1 == lookahead ? 0 : delayPos + 1;

        const float g = enabled ? gain : 1.0f;
        left[i] = l * g;
        right[i] = r * g;
    }

    // Guard against accumulated rounding in the running sum.
    if ((counter & 0xffff) == 0)
    {
        avgSum = 0.0;
        for (auto v : avgRing)
            avgSum += v;
    }
}

// -----------------------------------------------------------------------------

void MasterBus::prepare (double sampleRate, int maxBlockSize)
{
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    maxBlock = juce::jmax (1, maxBlockSize);
    oversampler->initProcessing ((size_t) maxBlock);

    limiter.prepare (sampleRate, (int) std::ceil (0.0015 * sampleRate)); // 1.5 ms look-ahead
    driveSmoothed.reset (sampleRate, 0.05);

    latency = (int) std::lround (oversampler->getLatencyInSamples()) + limiter.getLatency();
    reset();
}

void MasterBus::reset() noexcept
{
    if (oversampler != nullptr)
        oversampler->reset();

    limiter.reset();
}

void MasterBus::process (juce::AudioBuffer<float>& stereo, float drive, bool driveOn, bool limiterOn) noexcept
{
    if (oversampler == nullptr || stereo.getNumChannels() < 2)
        return;

    driveSmoothed.setTargetValue (driveOn ? juce::jlimit (0.0f, 1.0f, drive) : 0.0f);
    juce::dsp::AudioBlock<float> whole (stereo);
    const int total = stereo.getNumSamples();

    // The oversampler's buffers are sized for maxBlock, so larger host blocks are chunked.
    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - start);
        auto block = whole.getSubBlock ((size_t) start, (size_t) n).getSubsetChannelBlock (0, 2);
        processChunk (block, limiterOn);
    }
}

void MasterBus::processChunk (juce::dsp::AudioBlock<float>& block, bool limiterOn) noexcept
{
    const int n = (int) block.getNumSamples();

    // Always run the oversampler so the latency doesn't change when Drive is toggled.
    auto up = oversampler->processSamplesUp (block);
    const int upN = (int) up.getNumSamples();

    // Drive is constant within a block at the oversampled rate (smoothed between blocks).
    const float d = driveSmoothed.skip (n);

    if (d > 0.0005f)
    {
        // Asymmetric tanh: the bias adds even harmonics ("thickness"). The makeup gain keeps a
        // -12 dBFS peak at the same level, so the knob changes density rather than loudness.
        constexpr float reference = 0.25f;
        const float pre = 1.0f + d * 7.0f;
        const float bias = 0.18f * d;
        const float tb = std::tanh (bias);
        const float makeup = reference / (std::tanh (reference * pre + bias) - tb);
        const float wet = juce::jmin (1.0f, d * 3.0f);

        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            float* x = up.getChannelPointer (ch);

            for (int i = 0; i < upN; ++i)
            {
                const float shaped = (std::tanh (x[i] * pre + bias) - tb) * makeup;
                x[i] += wet * (shaped - x[i]);
            }
        }
    }

    oversampler->processSamplesDown (block);
    limiter.process (block.getChannelPointer (0), block.getChannelPointer (1), n, limiterOn);
}

} // namespace bf
