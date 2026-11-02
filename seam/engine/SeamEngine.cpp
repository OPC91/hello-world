#include "SeamEngine.h"

#include <algorithm>
#include <cmath>

namespace seam::engine
{
    void SeamEngine::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        maxBlock = std::max (1, maxBlockSize);
        reset();
    }

    void SeamEngine::reset()
    {
        tracker.reset();
        position = {};
        lastTelemetry = {};
    }

    void SeamEngine::process (float* const* channels, int numChannels, int numSamples, const TransportSnapshot& transport,
                              const Settings& settings) noexcept
    {
        (void) channels;
        (void) numChannels;

        auto t = transport;
        t.numSamples = numSamples;
        position = tracker.update (t, settings.phraseBars);

        Telemetry tel;
        tel.playing = position.valid;
        tel.phraseBars = position.phraseBars;
        tel.barsPerBeat = position.quartersPerBeat / position.quartersPerBar;
        if (position.valid)
        {
            tel.barsToSeam = position.barsToSeam;
            tel.barInPhrase = position.barInPhrase;
            const double beats = (position.blockStartPpq - position.phraseStartPpq) / position.quartersPerBeat;
            tel.beatPhase = static_cast<float> (beats - std::floor (beats));
        }
        tel.effective = settings.transition;
        tel.bypassed = true;
        lastTelemetry = tel;
    }
}
