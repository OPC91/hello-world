#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace bf
{

/** Host transport snapshot for one block. */
struct TransportInfo
{
    bool isPlaying = false;
    double bpm = 174.0;
    double ppqStart = 0.0;
};

/** Stage-1 engine: plays the kick on step 1 of every bar, locked to the host. */
class Engine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void setKick (juce::AudioBuffer<float> sample);

    void process (juce::AudioBuffer<float>& out, const TransportInfo& transport);

private:
    double sampleRate = 44100.0;
    juce::AudioBuffer<float> kick;
    int playPos = -1;
};

} // namespace bf
