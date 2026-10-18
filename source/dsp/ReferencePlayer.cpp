#include "ReferencePlayer.h"
#include "reference/ReferenceAudio.h"

#include <algorithm>

namespace benchmark::dsp
{
    void ReferencePlayer::syncTo (const ReferenceAudio& reference) noexcept
    {
        if (current != &reference)
        {
            current = &reference;
            position = 0;
        }
    }

    void ReferencePlayer::render (const ReferenceAudio& reference, float* const* out, int numOutChannels, int numSamples) noexcept
    {
        syncTo (reference);

        const auto& source = reference.samples;
        const auto length = (int64_t) source.getNumSamples();
        const auto sourceChannels = source.getNumChannels();

        if (length == 0 || sourceChannels == 0)
        {
            for (int ch = 0; ch < numOutChannels; ++ch)
                std::fill (out[ch], out[ch] + numSamples, 0.0f);
            return;
        }

        int written = 0;

        while (written < numSamples)
        {
            position %= length;
            const auto todo = (int) std::min ((int64_t) (numSamples - written), length - position);
            const auto start = (int) position;

            if (numOutChannels == 1 && sourceChannels >= 2)
            {
                const auto* l = source.getReadPointer (0, start);
                const auto* r = source.getReadPointer (1, start);
                auto* o = out[0] + written;

                for (int i = 0; i < todo; ++i)
                    o[i] = 0.5f * (l[i] + r[i]);
            }
            else
            {
                for (int ch = 0; ch < numOutChannels; ++ch)
                {
                    const auto* src = source.getReadPointer (std::min (ch, sourceChannels - 1), start);
                    std::copy (src, src + todo, out[ch] + written);
                }
            }

            written += todo;
            position += todo;
        }

        position %= length;
    }

    void ReferencePlayer::advance (const ReferenceAudio& reference, int numSamples) noexcept
    {
        syncTo (reference);

        if (const auto length = (int64_t) reference.samples.getNumSamples(); length > 0)
            position = (position + numSamples) % length;
    }
}
