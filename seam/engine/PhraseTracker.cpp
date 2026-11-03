#include "PhraseTracker.h"

#include <algorithm>
#include <cmath>

namespace seam::engine
{
    long long PhrasePosition::sampleAt (double eventPpq) const noexcept
    {
        // Small epsilon so an event that lands exactly on a sample boundary is not pushed to the next sample by rounding.
        return static_cast<long long> (std::floor ((eventPpq - blockStartPpq) * samplesPerQuarter + 1.0e-6));
    }

    double PhrasePosition::phraseProgress() const noexcept
    {
        const double phraseLength = phraseBars * quartersPerBar;
        return phraseLength > 0.0 ? std::clamp ((blockStartPpq - phraseStartPpq) / phraseLength, 0.0, 1.0) : 0.0;
    }

    double PhraseTracker::quartersPerBar (int numerator, int denominator) noexcept
    {
        if (numerator <= 0 || denominator <= 0)
            return 4.0;
        return numerator * 4.0 / denominator;
    }

    double PhraseTracker::gridFloor (double value) noexcept
    {
        return std::floor (value + 1.0e-9);
    }

    PhrasePosition PhraseTracker::update (const TransportSnapshot& transport, int phraseBars) noexcept
    {
        PhrasePosition pos;
        pos.phraseBars = std::max (1, phraseBars);
        pos.quartersPerBar = quartersPerBar (transport.timeSigNumerator, transport.timeSigDenominator);
        pos.quartersPerBeat = transport.timeSigDenominator > 0 ? 4.0 / transport.timeSigDenominator : 1.0;

        const bool usable = transport.playing && transport.hasPosition && transport.bpm > 1.0
                            && transport.sampleRate > 0.0 && std::isfinite (transport.ppq);
        if (! usable)
        {
            havePrevious = false;
            return pos;
        }

        pos.valid = true;
        pos.samplesPerQuarter = transport.sampleRate * 60.0 / transport.bpm;
        pos.quartersPerSample = 1.0 / pos.samplesPerQuarter;
        pos.blockStartPpq = transport.ppq;
        pos.blockEndPpq = transport.ppq + transport.numSamples * pos.quartersPerSample;

        // Continuous if the host position is within a couple of samples of where the last block ended.
        const double tolerance = 2.0 * std::max (pos.quartersPerSample, previousQuartersPerSample);
        const bool continuous = havePrevious && std::abs (transport.ppq - previousEndPpq) <= tolerance;
        pos.jumped = ! continuous;
        pos.eventFromPpq = continuous ? std::min (previousEndPpq, pos.blockEndPpq) : pos.blockStartPpq;

        havePrevious = true;
        previousEndPpq = pos.blockEndPpq;
        previousQuartersPerSample = pos.quartersPerSample;

        // Phrase grid. Negative positions (pre-roll) wrap like any other.
        const double phraseLength = pos.phraseBars * pos.quartersPerBar;
        pos.barsSinceStart = transport.ppq / pos.quartersPerBar;
        const double phraseIndex = gridFloor (transport.ppq / phraseLength);
        pos.phraseStartPpq = phraseIndex * phraseLength;
        pos.seamPpq = pos.phraseStartPpq + phraseLength;
        pos.barsToSeam = (pos.seamPpq - transport.ppq) / pos.quartersPerBar;
        pos.barInPhrase = std::clamp (static_cast<int> (gridFloor ((transport.ppq - pos.phraseStartPpq) / pos.quartersPerBar)),
                                      0, pos.phraseBars - 1);
        return pos;
    }
}
