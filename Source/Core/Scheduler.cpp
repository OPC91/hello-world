#include "Scheduler.h"

namespace bf
{

int Scheduler::collect (const Pattern& pattern, const GrooveContext& ctx, double blockStartPpq, double bpm, double sampleRate,
                        int startSample, int endSample, std::array<BlockHit, maxHitsPerBlock>& out, int numInOut) noexcept
{
    if (endSample <= startSample || bpm <= 0.0 || sampleRate <= 0.0)
        return numInOut;

    const double samplesPerPpq = sampleRate * 60.0 / bpm;
    double fromPpq = blockStartPpq + startSample / samplesPerPpq;
    const double toPpq = blockStartPpq + endSample / samplesPerPpq;

    if (! std::isnan (lastEndPpq) && std::abs (fromPpq - lastEndPpq) * samplesPerPpq < 1.0)
        fromPpq = lastEndPpq;

    lastEndPpq = toPpq;

    const int firstNew = numInOut;
    int num = numInOut;

    // A hit plays on the sample nearest to its exact time, so the range that owns sample i is
    // [i - 0.5, i + 0.5) in sample units. Shifting membership by half a sample keeps ownership and
    // rounding consistent, so a hit can never be clamped into the wrong block or segment.
    const double halfSample = 0.5 / samplesPerPpq;

    scheduling::forEachHit (pattern, ctx, fromPpq - halfSample, toPpq - halfSample, bpm, [&] (const HitEvent& e) {
        if (num >= maxHitsPerBlock)
            return;

        const auto offset = (int) std::lround ((e.ppq - blockStartPpq) * samplesPerPpq);
        out[(size_t) num++] = { e, juce::jlimit (startSample, endSample - 1, offset) };
    });

    // Insertion sort by sample offset (the list is tiny and nearly sorted).
    for (int i = firstNew + 1; i < num; ++i)
    {
        auto h = out[(size_t) i];
        int j = i - 1;

        while (j >= firstNew && out[(size_t) j].sampleOffset > h.sampleOffset)
        {
            out[(size_t) j + 1] = out[(size_t) j];
            --j;
        }

        out[(size_t) j + 1] = h;
    }

    return num;
}

} // namespace bf
