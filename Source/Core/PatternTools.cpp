#include "PatternTools.h"
#include "GrooveEngine.h"

#include <juce_core/juce_core.h>

namespace bf::PatternTools
{

void addGhostNotes (Pattern& p, int ghostTrack, int snareTrack, std::uint32_t seed)
{
    const int per16 = stepsPerSixteenth (p);
    const int sixteenths = lengthInSixteenths (p);

    // Positions inside one bar (in 16ths) with the chance and velocity range of a ghost there.
    // The core set (7, 14, usually 9) gives the familiar "drag" into and out of the backbeat.
    struct Spot { int pos; float chance; int velLo, velHi; };
    static constexpr Spot spots[] = {
        { 7, 1.00f, 34, 46 }, { 9, 0.90f, 28, 40 }, { 14, 1.00f, 30, 42 },
        { 15, 0.45f, 24, 36 }, { 11, 0.40f, 26, 38 }, { 3, 0.30f, 22, 32 }, { 2, 0.15f, 20, 30 },
    };

    auto& ghosts = p.tracks[(size_t) ghostTrack].steps;
    const auto& snares = p.tracks[(size_t) snareTrack].steps;

    for (int bar = 0; bar * 16 < sixteenths; ++bar)
    {
        for (const auto& spot : spots)
        {
            const int sixteenth = bar * 16 + spot.pos;

            if (sixteenth >= sixteenths)
                continue;

            const int step = sixteenth * per16;

            if (snares[(size_t) step].on || ghosts[(size_t) step].on)
                continue;

            if (groove::random01 (seed, bar, (std::uint32_t) spot.pos, 1u) >= spot.chance)
                continue;

            auto& s = ghosts[(size_t) step];
            s = Step {};
            s.on = true;
            s.velocity = (std::uint8_t) juce::jmap (groove::random01 (seed, bar, (std::uint32_t) spot.pos, 2u),
                                                    (float) spot.velLo, (float) spot.velHi);
            // Ghosts sit a hair late, like a drummer's relaxed left hand.
            s.nudge = 0.02f + 0.04f * groove::random01 (seed, bar, (std::uint32_t) spot.pos, 3u);
        }
    }
}

namespace
{
    template <typename Array>
    void rotate (Array& a, int length, int steps)
    {
        if (length <= 0)
            return;

        steps = ((steps % length) + length) % length;

        if (steps == 0)
            return;

        std::rotate (a.begin(), a.begin() + (length - steps), a.begin() + length);
    }
}

void shift (Pattern& p, int steps)
{
    for (int t = 0; t < kNumTracks; ++t)
        shiftTrack (p, t, steps);

    rotate (p.accents, p.length, steps);
}

void shiftTrack (Pattern& p, int track, int steps)
{
    rotate (p.tracks[(size_t) track].steps, p.length, steps);
}

void setLengthAndResolution (Pattern& p, int newLength, Resolution newRes)
{
    newLength = newLength == 32 ? 32 : 16;

    if (newRes != p.resolution) // keep events at the same musical time
    {
        Pattern converted = p;
        converted.clear();
        converted.resolution = newRes;

        const bool toFiner = newRes == Resolution::ThirtySecond;

        for (int i = 0; i < p.length; ++i)
        {
            // Nudge is a fraction of a step, so it scales with the step size.
            const int target = toFiner ? i * 2 : i / 2;
            const bool offGrid = ! toFiner && (i % 2 == 1); // a 32nd between two 16ths

            if (target >= kMaxSteps)
                continue;

            for (int t = 0; t < kNumTracks; ++t)
            {
                Step src = p.tracks[(size_t) t].steps[(size_t) i];
                auto& dst = converted.tracks[(size_t) t].steps[(size_t) target];

                if (! src.on && src == Step {})
                    continue;

                src.nudge = toFiner ? juce::jlimit (-0.5f, 0.5f, src.nudge * 2.0f) : src.nudge * 0.5f;
                src.length = toFiner ? juce::jmin (16.0f, src.length * 2.0f) : juce::jmax (0.1f, src.length * 0.5f);

                if (offGrid)
                {
                    if (dst.on && src.on)
                    {
                        dst.ratchet = (std::uint8_t) juce::jmax (2, (int) dst.ratchet); // two 32nds -> a 16th roll
                        continue;
                    }

                    src.nudge = juce::jmin (0.5f, src.nudge + 0.5f);
                }

                if (! dst.on)
                    dst = src;
            }

            converted.accents[(size_t) target] = converted.accents[(size_t) target] || p.accents[(size_t) i];
        }

        p = converted;
    }
    // Growing a 16-step pattern to 32 repeats the first half so it keeps playing the same groove.
    else if (newLength == 32 && p.length == 16)
    {
        for (int t = 0; t < kNumTracks; ++t)
            for (int i = 0; i < 16; ++i)
                p.tracks[(size_t) t].steps[(size_t) i + 16] = p.tracks[(size_t) t].steps[(size_t) i];

        for (int i = 0; i < 16; ++i)
            p.accents[(size_t) i + 16] = p.accents[(size_t) i];
    }

    p.length = newLength;
}

} // namespace bf::PatternTools
