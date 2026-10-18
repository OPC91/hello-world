#include "ReferencePlayer.h"
#include "reference/ReferenceAudio.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace benchmark::dsp
{
    namespace
    {
        /** Copies [pos, pos + n) into dst, zero outside the file, converting channel counts. */
        void readInto (const juce::AudioBuffer<float>& source, int64_t pos, float* const* dst, int numOut, int n) noexcept
        {
            const auto length = (int64_t) source.getNumSamples();
            const auto sourceChannels = source.getNumChannels();

            // Leading region before the file start, trailing region past its end.
            const auto lead = (int) std::clamp (-pos, (int64_t) 0, (int64_t) n);
            const auto available = (int) std::clamp (length - std::max (pos, (int64_t) 0), (int64_t) 0, (int64_t) (n - lead));
            const auto trail = n - lead - available;

            for (int ch = 0; ch < numOut; ++ch)
            {
                std::fill (dst[ch], dst[ch] + lead, 0.0f);
                std::fill (dst[ch] + lead + available, dst[ch] + lead + available + trail, 0.0f);
            }

            if (available == 0)
                return;

            const auto start = (int) std::max (pos, (int64_t) 0);

            if (numOut == 1 && sourceChannels >= 2)
            {
                const auto* l = source.getReadPointer (0, start);
                const auto* r = source.getReadPointer (1, start);
                auto* o = dst[0] + lead;

                for (int i = 0; i < available; ++i)
                    o[i] = 0.5f * (l[i] + r[i]);
            }
            else
            {
                for (int ch = 0; ch < numOut; ++ch)
                {
                    const auto* src = source.getReadPointer (std::min (ch, sourceChannels - 1), start);
                    std::copy (src, src + available, dst[ch] + lead);
                }
            }
        }

        float sampleAt (const juce::AudioBuffer<float>& source, int64_t pos, int ch, int numOut) noexcept
        {
            if (pos < 0 || pos >= source.getNumSamples())
                return 0.0f;

            const auto i = (int) pos;

            if (numOut == 1 && source.getNumChannels() >= 2)
                return 0.5f * (source.getSample (0, i) + source.getSample (1, i));

            return source.getSample (std::min (ch, source.getNumChannels() - 1), i);
        }
    }

    void ReferencePlayer::prepare (double sampleRate, double fadeMs) noexcept
    {
        const auto wasPlaying = isPlaying();
        fadeLength = std::max (1, (int) std::lround (sampleRate * fadeMs / 1000.0));
        playTarget = wasPlaying ? fadeLength : 0;
        reset();
    }

    void ReferencePlayer::reset() noexcept
    {
        current = nullptr;
        position = oldPosition = 0;
        fadeRemaining = 0;
        playRamp = 0;
    }

    void ReferencePlayer::setLoop (int64_t start, int64_t end) noexcept
    {
        if (end > start && start >= 0)
        {
            loopStart = start;
            loopEnd = end;
        }
        else
        {
            clearLoop();
        }
    }

    void ReferencePlayer::seek (int64_t newPosition) noexcept
    {
        if (newPosition == position)
            return;

        if (playRamp == 0)
        {
            // Silent: jump straight there.
            position = newPosition;
            fadeRemaining = 0;
            return;
        }

        startJump (newPosition);
    }

    void ReferencePlayer::startJump (int64_t target) noexcept
    {
        oldPosition = position;
        position = target;
        fadeRemaining = fadeLength;
    }

    void ReferencePlayer::setReference (const ReferenceAudio* reference) noexcept
    {
        if (current == reference)
            return;

        current = reference;
        position = oldPosition = 0;
        fadeRemaining = 0;
        playRamp = 0; // fade in the new track
    }

    void ReferencePlayer::render (float* const* out, int numOutChannels, int numSamples) noexcept
    {
        if (current == nullptr)
        {
            for (int ch = 0; ch < numOutChannels; ++ch)
                std::fill (out[ch], out[ch] + numSamples, 0.0f);
            return;
        }

        const auto& source = current->samples;
        const auto length = (int64_t) source.getNumSamples();
        int written = 0;

        while (written < numSamples)
        {
            const auto remaining = numSamples - written;
            float* dst[2] = { out[0] + written, numOutChannels > 1 ? out[1] + written : nullptr };

            if (playRamp == 0 && playTarget == 0)
            {
                for (int ch = 0; ch < numOutChannels; ++ch)
                    std::fill (dst[ch], dst[ch] + remaining, 0.0f);
                break;
            }

            if (length == 0)
            {
                for (int ch = 0; ch < numOutChannels; ++ch)
                    std::fill (dst[ch], dst[ch] + remaining, 0.0f);
                position += remaining;
                break;
            }

            int segment = remaining;

            if (wrap)
            {
                const auto looping = hasLoop() && loopStart < length;
                const auto boundary = looping ? std::min (loopEnd, length) : length;

                if (position >= boundary || position < 0)
                {
                    startJump (looping ? loopStart : 0);
                    continue;
                }

                segment = (int) std::min ((int64_t) segment, boundary - position);
            }

            if (fadeRemaining > 0)
                segment = std::min (segment, fadeRemaining);

            readInto (source, position, dst, numOutChannels, segment);

            // Crossfade from the voice we jumped away from.
            if (fadeRemaining > 0)
            {
                const auto done = fadeLength - fadeRemaining;

                for (int i = 0; i < segment; ++i)
                {
                    const auto angle = (float) (done + i + 1) / (float) fadeLength * (std::numbers::pi_v<float> * 0.5f);
                    const auto gIn = std::sin (angle);
                    const auto gOut = std::cos (angle);

                    for (int ch = 0; ch < numOutChannels; ++ch)
                        dst[ch][i] = dst[ch][i] * gIn + sampleAt (source, oldPosition + i, ch, numOutChannels) * gOut;
                }

                oldPosition += segment;
                fadeRemaining -= segment;
            }

            // Pause / resume ramp.
            if (playRamp != playTarget)
            {
                for (int i = 0; i < segment; ++i)
                {
                    if (playRamp != playTarget)
                        playRamp += playRamp < playTarget ? 1 : -1;

                    const auto g = (float) playRamp / (float) fadeLength;

                    for (int ch = 0; ch < numOutChannels; ++ch)
                        dst[ch][i] *= g;
                }
            }

            position += segment;
            written += segment;
        }
    }
}
