#include "Engine.h"

#include <cmath>

namespace bf
{

void Engine::prepare (double sr, int)
{
    sampleRate = sr;
    playPos = -1;
}

void Engine::setKick (juce::AudioBuffer<float> sample)
{
    kick = std::move (sample);
    playPos = -1;
}

void Engine::process (juce::AudioBuffer<float>& out, const TransportInfo& transport)
{
    const int numSamples = out.getNumSamples();
    int triggerAt = -1;

    if (transport.isPlaying && transport.bpm > 0.0)
    {
        const double samplesPerPpq = sampleRate * 60.0 / transport.bpm;
        const double ppqEnd = transport.ppqStart + numSamples / samplesPerPpq;
        const double nextBar = std::ceil (transport.ppqStart / 4.0) * 4.0;

        if (nextBar < ppqEnd)
            triggerAt = juce::jlimit (0, numSamples - 1, (int) std::lround ((nextBar - transport.ppqStart) * samplesPerPpq));
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (i == triggerAt)
            playPos = 0;

        if (playPos >= 0 && playPos < kick.getNumSamples())
        {
            const float s = kick.getSample (0, playPos++) * 0.8f;

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, i, s);
        }
    }
}

} // namespace bf
