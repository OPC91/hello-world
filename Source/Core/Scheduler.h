#pragma once

#include <cmath>
#include <limits>

#include <juce_core/juce_core.h>

#include "GrooveEngine.h"
#include "Pattern.h"

namespace bf
{

/** Everything the scheduler needs to turn grid steps into timed, groove-processed hits. */
struct GrooveContext
{
    std::array<float, kNumTracks> swing {};   // resolved per-track swing (0.5..0.75)
    bool swingEighths = false;
    GrooveTemplate grooveTemplate {};
    float humanizeTimeMs = 0.0f;              // 0..20
    float humanizeVel = 0.0f;                 // 0..0.3
    std::uint32_t seed = 1;
    float accentAmount = 0.3f;                // 0..1
    std::array<bool, kNumTracks> trackEnabled {};

    GrooveContext()
    {
        swing.fill (0.5f);
        trackEnabled.fill (true);
    }
};

/** A single scheduled drum hit, in pattern time. */
struct HitEvent
{
    int track = 0;
    double ppq = 0.0;          // absolute host position in quarter notes
    float velocity = 1.0f;     // 0..1 after groove, accent and humanize
    float pitch = 0.0f;        // step pitch offset in semitones
    double lengthPpq = 0.25;   // gate length
    int sample = 0;            // sample variant: 0 = track sample, 1..15 = per-hit sample
};

namespace scheduling
{
    /** Largest distance (in quarter notes) any groove processing can move a hit from its grid
        position: nudge (half a step) + template offset (half a step) + 8th swing + humanize. */
    inline double maxShiftPpq (double bpm, float humanizeTimeMs) noexcept
    {
        return 0.25 + 0.25 + 0.25 + 0.0625 + (double) humanizeTimeMs * 0.001 * bpm / 60.0;
    }

    inline std::int64_t floorDiv (std::int64_t a, std::int64_t b) noexcept
    {
        const auto q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    /** Calls fn(const HitEvent&) for every hit of `pattern` whose final (grooved) time lies in
        [fromPpq, toPpq). The pattern loops from ppq 0. Results are deterministic for a given
        context, so neighbouring ranges never double-trigger or drop a hit. */
    template <typename Fn>
    void forEachHit (const Pattern& pattern, const GrooveContext& ctx, double fromPpq, double toPpq, double bpm, Fn&& fn)
    {
        if (toPpq <= fromPpq || pattern.length <= 0)
            return;

        const double stepLen = pattern.stepLengthPpq();
        const double margin = maxShiftPpq (bpm, ctx.humanizeTimeMs);
        const double humanizePpq = (double) ctx.humanizeTimeMs * 0.001 * bpm / 60.0;

        const auto firstStep = (std::int64_t) std::floor ((fromPpq - margin) / stepLen);
        const auto lastStep  = (std::int64_t) std::ceil ((toPpq + margin) / stepLen);

        for (auto global = firstStep; global <= lastStep; ++global)
        {
            const auto loop = floorDiv (global, pattern.length);
            const int index = (int) (global - loop * pattern.length);
            const double stepStart = (double) global * stepLen;
            const int sixteenth = groove::sixteenthInBar (stepStart);
            const bool accented = pattern.accents[(size_t) index];

            for (int t = 0; t < kNumTracks; ++t)
            {
                if (! ctx.trackEnabled[(size_t) t])
                    continue;

                const Step& step = pattern.tracks[(size_t) t].steps[(size_t) index];

                if (! step.on)
                    continue;

                if (step.probability < 100
                    && groove::random01 (ctx.seed ^ 0x5bd1e995u, global, (std::uint32_t) t, 7u) * 100.0f >= (float) step.probability)
                    continue;

                const int ratchets = juce::jlimit (1, 4, (int) step.ratchet);
                const double subLen = stepLen / ratchets;

                for (int r = 0; r < ratchets; ++r)
                {
                    double time = groove::applySwing (stepStart + r * subLen, ctx.swing[(size_t) t], ctx.swingEighths);
                    time += (double) step.nudge * stepLen;
                    time += (double) ctx.grooveTemplate.timing[(size_t) sixteenth] * 0.25;

                    if (humanizePpq > 0.0)
                        time += humanizePpq * groove::randomBipolar (ctx.seed, global, (std::uint32_t) t, (std::uint32_t) r);

                    if (time < fromPpq || time >= toPpq)
                        continue;

                    float vel = (float) step.velocity / 127.0f;
                    vel *= ctx.grooveTemplate.velocity[(size_t) sixteenth];

                    if (accented)
                        vel *= 1.0f + ctx.accentAmount;

                    if (r > 0)
                        vel *= std::pow (0.86f, (float) r); // natural decay across a ratchet roll

                    if (ctx.humanizeVel > 0.0f)
                        vel *= 1.0f + ctx.humanizeVel * groove::randomBipolar (ctx.seed ^ 0x27d4eb2du, global, (std::uint32_t) t, (std::uint32_t) r);

                    HitEvent e;
                    e.track = t;
                    e.ppq = time;
                    e.velocity = juce::jlimit (1.0f / 127.0f, 1.0f, vel);
                    e.pitch = (float) step.pitch;
                    e.sample = step.sample;
                    e.lengthPpq = ratchets > 1 ? subLen * 0.9 : juce::jmax (0.01, (double) step.length * stepLen);
                    fn (e);
                }
            }
        }
    }
}

/** Sample-accurate block scheduler that keeps consecutive host blocks seamless. */
class Scheduler
{
public:
    struct BlockHit
    {
        HitEvent event;
        int sampleOffset = 0;
    };

    static constexpr int maxHitsPerBlock = 512;

    /** Forget the previous block (call on transport stop or a position jump). */
    void reset() noexcept { lastEndPpq = std::numeric_limits<double>::quiet_NaN(); }

    /** Collects the hits that fall inside samples [startSample, endSample) of a block whose first
        sample sits at `blockStartPpq`. Hits are appended to `out` (up to maxHitsPerBlock).

        If the block continues on from the previous call (within a sample of tolerance), the range
        starts exactly where the previous one ended, so host rounding can neither duplicate nor
        drop a hit at a block boundary. Each block is re-anchored to the host's position, so the
        timing never drifts. */
    int collect (const Pattern& pattern, const GrooveContext& ctx, double blockStartPpq, double bpm, double sampleRate,
                 int startSample, int endSample, std::array<BlockHit, maxHitsPerBlock>& out, int numInOut) noexcept;

private:
    double lastEndPpq = std::numeric_limits<double>::quiet_NaN();
};

} // namespace bf
