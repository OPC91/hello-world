#include "PatternGenerator.h"
#include "PatternTools.h"

#include <juce_core/juce_core.h>

namespace bf::PatternGenerator
{

namespace
{
    using Table = std::array<float, 16>;

    /** Everything that defines a sub-style, per 16th position within one bar. */
    struct StyleDef
    {
        Table kick;
        Table snareExtra;          // full-velocity snares beyond the backbeat
        float ghostDensity;        // multiplier on the ghost-note chances
        Table hat;                 // closed hat
        std::array<int, 4> hatContour; // velocity on 1 / e / & / a
        float hatRollChance;
        int hatRollRatchet;
        Table openHat;
        float rideChance;          // chance the pattern uses the ride at all
        Table ride;
        Table perc;
        float fxChance;
        float snareLate;           // backbeat lay-back as a fraction of a step
        float hatLooseness;        // random hat nudge range (fraction of a step)
    };

    //                         0     1     2     3     4     5     6     7     8     9    10    11    12    13    14    15
    const StyleDef liquid {
        Table { 1.00f, 0.00f, 0.12f, 0.00f, 0.00f, 0.00f, 0.10f, 0.05f, 0.00f, 0.00f, 0.95f, 0.15f, 0.00f, 0.00f, 0.10f, 0.00f },
        Table { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.06f },
        0.8f,
        Table { 0.85f, 0.50f, 0.95f, 0.50f, 0.85f, 0.50f, 0.95f, 0.50f, 0.85f, 0.50f, 0.95f, 0.50f, 0.85f, 0.50f, 0.95f, 0.55f },
        { 74, 50, 96, 56 }, 0.10f, 2,
        Table { 0, 0, 0, 0, 0, 0, 0.30f, 0, 0, 0, 0, 0, 0, 0, 0.40f, 0 },
        0.30f,
        Table { 0.9f, 0, 0.7f, 0, 0.9f, 0, 0.7f, 0, 0.9f, 0, 0.7f, 0, 0.9f, 0, 0.7f, 0 },
        Table { 0, 0, 0, 0.25f, 0, 0, 0, 0.30f, 0, 0, 0, 0.25f, 0, 0, 0, 0.30f },
        0.35f, 0.02f, 0.03f
    };

    const StyleDef jumpUp {
        Table { 1.00f, 0.00f, 0.25f, 0.00f, 0.00f, 0.00f, 0.15f, 0.30f, 0.00f, 0.00f, 0.90f, 0.20f, 0.00f, 0.00f, 0.25f, 0.00f },
        Table { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.12f },
        0.35f,
        Table { 0.30f, 0.15f, 1.00f, 0.15f, 0.30f, 0.15f, 1.00f, 0.15f, 0.30f, 0.15f, 1.00f, 0.15f, 0.30f, 0.15f, 1.00f, 0.25f },
        { 70, 48, 104, 52 }, 0.15f, 2,
        Table { 0, 0, 0.45f, 0, 0, 0, 0, 0, 0, 0, 0.45f, 0, 0, 0, 0, 0 },
        0.05f,
        Table { 0.9f, 0, 0, 0, 0.9f, 0, 0, 0, 0.9f, 0, 0, 0, 0.9f, 0, 0, 0 },
        Table { 0, 0, 0, 0.20f, 0, 0, 0.25f, 0, 0, 0, 0, 0.20f, 0, 0, 0.25f, 0 },
        0.6f, 0.0f, 0.015f
    };

    const StyleDef neuro {
        Table { 1.00f, 0.00f, 0.00f, 0.25f, 0.00f, 0.00f, 0.30f, 0.00f, 0.00f, 0.00f, 0.75f, 0.35f, 0.00f, 0.00f, 0.15f, 0.00f },
        Table { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.08f },
        0.45f,
        Table { 0.40f, 0.25f, 0.90f, 0.25f, 0.40f, 0.25f, 0.90f, 0.25f, 0.40f, 0.25f, 0.90f, 0.25f, 0.40f, 0.25f, 0.90f, 0.30f },
        { 72, 46, 98, 50 }, 0.35f, 2,
        Table { 0, 0, 0, 0, 0, 0, 0.20f, 0, 0, 0, 0, 0, 0, 0, 0.25f, 0 },
        0.05f,
        Table { 0.9f, 0, 0, 0, 0.9f, 0, 0, 0, 0.9f, 0, 0, 0, 0.9f, 0, 0, 0 },
        Table { 0, 0, 0, 0, 0, 0.2f, 0, 0, 0, 0.2f, 0, 0, 0, 0.25f, 0, 0 },
        0.5f, 0.0f, 0.01f
    };

    const StyleDef jungle {
        Table { 1.00f, 0.00f, 0.55f, 0.00f, 0.00f, 0.00f, 0.25f, 0.00f, 0.15f, 0.00f, 0.80f, 0.45f, 0.00f, 0.00f, 0.25f, 0.00f },
        Table { 0, 0, 0, 0, 0, 0, 0, 0.45f, 0, 0.40f, 0, 0, 0, 0, 0, 0.45f },
        1.0f,
        Table { 0.80f, 0.70f, 0.85f, 0.70f, 0.80f, 0.70f, 0.85f, 0.70f, 0.80f, 0.70f, 0.85f, 0.70f, 0.80f, 0.70f, 0.85f, 0.75f },
        { 86, 58, 82, 64 }, 0.20f, 2,
        Table { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.30f, 0 },
        0.55f,
        Table { 0.9f, 0, 0.8f, 0, 0.9f, 0, 0.8f, 0, 0.9f, 0, 0.8f, 0, 0.9f, 0, 0.8f, 0 },
        Table { 0, 0, 0, 0.3f, 0, 0, 0.2f, 0, 0, 0, 0, 0.3f, 0, 0, 0.2f, 0 },
        0.3f, 0.03f, 0.05f
    };

    const StyleDef halftime {
        Table { 1.00f, 0.00f, 0.00f, 0.30f, 0.00f, 0.00f, 0.35f, 0.00f, 0.00f, 0.00f, 0.00f, 0.40f, 0.00f, 0.15f, 0.20f, 0.00f },
        Table { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.10f },
        0.5f,
        Table { 0.85f, 0.25f, 0.85f, 0.25f, 0.85f, 0.25f, 0.85f, 0.25f, 0.85f, 0.25f, 0.85f, 0.25f, 0.85f, 0.25f, 0.85f, 0.30f },
        { 92, 50, 70, 56 }, 0.40f, 3,
        Table { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.40f, 0 },
        0.15f,
        Table { 0.9f, 0, 0, 0, 0.9f, 0, 0, 0, 0.9f, 0, 0, 0, 0.9f, 0, 0, 0 },
        Table { 0, 0, 0, 0, 0, 0, 0.25f, 0, 0, 0, 0, 0, 0, 0.25f, 0, 0.2f },
        0.4f, 0.02f, 0.03f
    };

    const StyleDef rollers {
        Table { 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.20f, 0.00f, 0.00f, 0.00f, 1.00f, 0.25f, 0.00f, 0.00f, 0.15f, 0.00f },
        Table { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.05f },
        0.9f,
        Table { 0.95f, 0.90f, 0.95f, 0.90f, 0.95f, 0.90f, 0.95f, 0.90f, 0.95f, 0.90f, 0.95f, 0.90f, 0.95f, 0.90f, 0.95f, 0.92f },
        { 92, 56, 78, 62 }, 0.15f, 2,
        Table { 0, 0, 0, 0, 0, 0, 0.30f, 0, 0, 0, 0, 0, 0, 0, 0.20f, 0 },
        0.25f,
        Table { 0.9f, 0, 0.8f, 0, 0.9f, 0, 0.8f, 0, 0.9f, 0, 0.8f, 0, 0.9f, 0, 0.8f, 0 },
        Table { 0, 0.15f, 0, 0.2f, 0, 0.15f, 0, 0.2f, 0, 0.15f, 0, 0.2f, 0, 0.15f, 0, 0.2f },
        0.3f, 0.01f, 0.02f
    };

    const StyleDef minimal {
        Table { 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.85f, 0.00f, 0.00f, 0.00f, 0.10f, 0.00f },
        Table { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        0.4f,
        Table { 0.00f, 0.10f, 0.85f, 0.10f, 0.00f, 0.10f, 0.85f, 0.10f, 0.00f, 0.10f, 0.85f, 0.10f, 0.00f, 0.10f, 0.85f, 0.15f },
        { 70, 46, 88, 50 }, 0.05f, 2,
        Table { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.25f, 0 },
        0.35f,
        Table { 0, 0, 0.8f, 0, 0, 0, 0.8f, 0, 0, 0, 0.8f, 0, 0, 0, 0.8f, 0 },
        Table { 0, 0, 0, 0.3f, 0, 0, 0, 0.35f, 0, 0, 0, 0.3f, 0, 0, 0, 0.35f },
        0.2f, 0.02f, 0.025f
    };

    const StyleDef& styleDef (SubStyle s)
    {
        switch (s)
        {
            case SubStyle::JumpUp:   return jumpUp;
            case SubStyle::Neuro:    return neuro;
            case SubStyle::Jungle:   return jungle;
            case SubStyle::Halftime: return halftime;
            case SubStyle::Rollers:  return rollers;
            case SubStyle::Minimal:  return minimal;
            case SubStyle::Liquid:
            default:                 return liquid;
        }
    }

    /** Deterministic, platform-independent random numbers. */
    struct Rng
    {
        explicit Rng (std::uint32_t seed) : state ((std::uint64_t) seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}

        float next() noexcept // [0, 1)
        {
            state += 0x9E3779B97F4A7C15ull;
            auto z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z ^= z >> 31;
            return (float) ((double) (z >> 40) / (double) (1ull << 24));
        }

        float range (float lo, float hi) noexcept { return lo + (hi - lo) * next(); }
        int velocity (int centre, int spread) noexcept { return juce::jlimit (1, 127, centre + (int) std::lround (range ((float) -spread, (float) spread))); }
        bool chance (float p) noexcept { return next() < p; }

        std::uint64_t state;
    };

    /** Core hits (p >= 0.8) almost always play; optional hits scale with variation. */
    bool decide (Rng& rng, float p, float variation)
    {
        if (p <= 0.0f)
            return false;

        if (p >= 0.8f)
            return ! rng.chance (variation * 0.12f * (1.0f - p) * 5.0f);

        return rng.chance (juce::jmin (1.0f, p * 2.0f * variation));
    }

    /** Builds one role's hits across the pattern. Writes go through the context so locked
        tracks are never touched and the 1/16 vs 1/32 grid is handled in one place. */
    struct Builder
    {
        Builder (Pattern& p, const Options& o) : pattern (p), opts (o), def (styleDef (o.style)), rng (o.seed)
        {
            per16 = PatternTools::stepsPerSixteenth (p);
            sixteenths = PatternTools::lengthInSixteenths (p);
            bars = juce::jmax (1, (sixteenths + 15) / 16);
        }

        bool writable (int track) const { return ! opts.locked[(size_t) track]; }

        Step* stepAt (int track, int sixteenth)
        {
            if (sixteenth < 0 || sixteenth >= sixteenths)
                return nullptr;

            return &pattern.tracks[(size_t) track].steps[(size_t) (sixteenth * per16)];
        }

        bool isOn (int track, int sixteenth)
        {
            auto* s = stepAt (track, sixteenth);
            return s != nullptr && s->on;
        }

        void place (int track, int sixteenth, int velocity, float nudge = 0.0f, int ratchet = 1)
        {
            if (! writable (track))
                return;

            if (auto* s = stepAt (track, sixteenth))
            {
                *s = Step {};
                s->on = true;
                s->velocity = (std::uint8_t) juce::jlimit (1, 127, velocity);
                // Nudges are authored in 16ths; convert to the pattern's step size.
                s->nudge = juce::jlimit (-0.5f, 0.5f, nudge * (float) per16);
                s->ratchet = (std::uint8_t) juce::jlimit (1, 4, ratchet);
            }
        }

        void clearTrack (int track)
        {
            if (writable (track))
                pattern.tracks[(size_t) track] = TrackPattern {};
        }

        /** Second and later bars repeat bar 1 with variation-driven mutations. */
        bool mutateInLaterBar (int bar) { return bar > 0 && rng.chance (0.2f + 0.5f * opts.variation); }

        template <typename Fn>
        void forEachBarPosition (int track, const Table& table, Fn&& makeHit)
        {
            std::array<bool, 16> firstBar {};

            for (int bar = 0; bar < bars; ++bar)
            {
                for (int pos = 0; pos < 16; ++pos)
                {
                    const int s = bar * 16 + pos;

                    if (s >= sixteenths)
                        break;

                    bool on = bar == 0 || mutateInLaterBar (bar) ? decide (rng, table[(size_t) pos], opts.variation)
                                                                 : firstBar[(size_t) pos];
                    if (bar == 0)
                        firstBar[(size_t) pos] = on;

                    if (on)
                        makeHit (track, s, pos, bar);
                }
            }
        }

        Pattern& pattern;
        const Options& opts;
        const StyleDef& def;
        Rng rng;
        int per16 = 1, sixteenths = 16, bars = 1;
    };

    constexpr int kKick = 0, kSnare = 1, kGhost = 2, kClosed = 3, kOpen = 4, kRide = 5, kPerc = 6, kFx = 7;

    void buildKick (Builder& b)
    {
        b.clearTrack (kKick);
        const auto anchors = snareAnchors (b.opts.style);

        b.forEachBarPosition (kKick, b.def.kick, [&] (int t, int s, int pos, int) {
            // Kicks don't land on the backbeat: that space belongs to the snare.
            if (std::find (anchors.begin(), anchors.end(), pos) != anchors.end())
                return;

            const bool strong = b.def.kick[(size_t) pos] >= 0.8f;
            b.place (t, s, pos == 0 ? b.rng.velocity (122, 5) : strong ? b.rng.velocity (112, 8) : b.rng.velocity (96, 10));
        });

        // The downbeat kick of every bar is non-negotiable.
        for (int bar = 0; bar < b.bars; ++bar)
            if (! b.isOn (kKick, bar * 16))
                b.place (kKick, bar * 16, b.rng.velocity (122, 5));
    }

    void buildSnare (Builder& b)
    {
        b.clearTrack (kSnare);
        const auto anchors = snareAnchors (b.opts.style);

        for (int bar = 0; bar < b.bars; ++bar)
            for (int pos : anchors)
                b.place (kSnare, bar * 16 + pos, b.rng.velocity (120, 6), b.def.snareLate);

        b.forEachBarPosition (kSnare, b.def.snareExtra, [&] (int t, int s, int, int) {
            if (! b.isOn (kKick, s) && ! b.isOn (kSnare, s))
                b.place (t, s, b.rng.velocity (98, 12), b.def.snareLate * 0.5f);
        });
    }

    void buildGhosts (Builder& b)
    {
        b.clearTrack (kGhost);

        struct Spot { int pos; float chance; int vel; };
        static constexpr Spot spots[] = { { 7, 0.9f, 40 }, { 9, 0.7f, 34 }, { 14, 0.75f, 36 }, { 15, 0.4f, 30 },
                                          { 11, 0.35f, 32 }, { 3, 0.3f, 28 }, { 2, 0.15f, 26 }, { 13, 0.2f, 28 } };
        std::array<bool, 16> firstBar {};

        for (int bar = 0; bar < b.bars; ++bar)
        {
            for (const auto& spot : spots)
            {
                const int s = bar * 16 + spot.pos;

                if (s >= b.sixteenths || b.isOn (kSnare, s))
                    continue;

                const float p = juce::jmin (1.0f, spot.chance * b.def.ghostDensity * (0.6f + 0.8f * b.opts.variation));
                const bool on = bar == 0 || b.mutateInLaterBar (bar) ? b.rng.chance (p) : firstBar[(size_t) spot.pos];

                if (bar == 0)
                    firstBar[(size_t) spot.pos] = on;

                if (on)
                    b.place (kGhost, s, b.rng.velocity (spot.vel, 6), b.rng.range (0.02f, 0.06f));
            }
        }
    }

    void buildHats (Builder& b)
    {
        b.clearTrack (kClosed);

        b.forEachBarPosition (kClosed, b.def.hat, [&] (int t, int s, int pos, int) {
            const int contour = b.def.hatContour[(size_t) (pos % 4)];
            const float loose = b.def.hatLooseness * (0.5f + b.opts.variation);
            b.place (t, s, b.rng.velocity (contour, 7), b.rng.range (-loose, loose));
        });

        // Rolls: a ratcheted hit near the end of a bar, more likely with more variation.
        for (int bar = 0; bar < b.bars; ++bar)
        {
            if (! b.rng.chance (b.def.hatRollChance * (0.3f + b.opts.variation)))
                continue;

            static constexpr int rollSpots[] = { 15, 13, 11, 7 };
            const int s = bar * 16 + rollSpots[(int) (b.rng.next() * 4.0f) % 4];

            if (s < b.sixteenths)
                b.place (kClosed, s, b.rng.velocity (70, 8), 0.0f, b.def.hatRollRatchet);
        }
    }

    void buildOpenHat (Builder& b)
    {
        b.clearTrack (kOpen);

        b.forEachBarPosition (kOpen, b.def.openHat, [&] (int t, int s, int, int) {
            b.place (t, s, b.rng.velocity (96, 8));

            // The closed hat on the same spot would choke it instantly.
            if (b.writable (kClosed))
                if (auto* c = b.stepAt (kClosed, s))
                    c->on = false;
        });
    }

    void buildRide (Builder& b)
    {
        b.clearTrack (kRide);

        if (! b.rng.chance (b.def.rideChance * (0.5f + b.opts.variation)))
            return;

        b.forEachBarPosition (kRide, b.def.ride, [&] (int t, int s, int pos, int) {
            b.place (t, s, pos % 4 == 0 ? b.rng.velocity (84, 6) : b.rng.velocity (68, 6), b.rng.range (-0.02f, 0.02f));
        });
    }

    void buildPerc (Builder& b)
    {
        b.clearTrack (kPerc);

        b.forEachBarPosition (kPerc, b.def.perc, [&] (int t, int s, int, int) {
            if (! b.isOn (kSnare, s))
                b.place (t, s, b.rng.velocity (72, 12), b.rng.range (-0.03f, 0.03f));
        });
    }

    void buildFx (Builder& b)
    {
        b.clearTrack (kFx);

        if (! b.rng.chance (b.def.fxChance * (0.5f + b.opts.variation)))
            return;

        const double patternPpq = b.pattern.lengthPpq();

        if (b.opts.fxRiserLengthPpq > 0.0)
        {
            // A riser should swell into the loop point, so start it riser-length before the end.
            if (b.opts.fxRiserLengthPpq >= patternPpq)
                return;

            const double start16 = (patternPpq - b.opts.fxRiserLengthPpq) * 4.0;
            const int s = (int) std::lround (start16);
            b.place (kFx, juce::jlimit (0, b.sixteenths - 1, s), b.rng.velocity (100, 6), (float) (start16 - s));
        }
        else
        {
            b.place (kFx, 0, b.rng.velocity (100, 6));
        }
    }

    void runAll (Builder& b, int onlyTrack = -1)
    {
        using BuildFn = void (*) (Builder&);
        static constexpr BuildFn builders[kNumTracks] = { buildKick, buildSnare, buildGhosts, buildHats, buildOpenHat, buildRide, buildPerc, buildFx };

        for (int t = 0; t < kNumTracks; ++t)
            if (onlyTrack < 0 || onlyTrack == t)
                if (b.writable (t))
                    builders[t] (b);
    }
}

std::vector<int> snareAnchors (SubStyle s)
{
    if (s == SubStyle::Halftime)
        return { 8 };

    return { 4, 12 };
}

GrooveSuggestion suggestedGroove (SubStyle s)
{
    switch (s)
    {
        case SubStyle::Liquid:   return { 1, 0.54f };
        case SubStyle::JumpUp:   return { 0, 0.50f };
        case SubStyle::Neuro:    return { 0, 0.51f };
        case SubStyle::Jungle:   return { 3, 0.53f };
        case SubStyle::Halftime: return { 2, 0.58f };
        case SubStyle::Rollers:  return { 4, 0.56f };
        case SubStyle::Minimal:  return { 1, 0.53f };
    }

    return { 0, 0.5f };
}

Pattern generate (const Options& opts, const Pattern& base)
{
    Pattern p = base;
    p.length = opts.length == 32 ? 32 : 16;
    p.resolution = opts.resolution;

    // Locked tracks keep their steps; everything else (and the accent row) is rebuilt.
    for (int t = 0; t < kNumTracks; ++t)
        if (! opts.locked[(size_t) t])
            p.tracks[(size_t) t] = TrackPattern {};

    p.accents = {};

    Builder b (p, opts);
    runAll (b);
    return p;
}

void generateTrack (Pattern& pattern, int track, const Options& opts)
{
    if (track < 0 || track >= kNumTracks)
        return;

    Options o = opts;
    o.locked.fill (true);
    o.locked[(size_t) track] = false;
    o.length = pattern.length;
    o.resolution = pattern.resolution;

    Builder b (pattern, o);
    runAll (b, track);
}

Pattern generateFill (const Pattern& source, const Options& opts)
{
    Pattern fill;
    fill.resolution = source.resolution;
    fill.length = source.resolution == Resolution::ThirtySecond ? 32 : 16; // exactly one bar

    // First half of the bar: the source groove carries on.
    const int per16 = PatternTools::stepsPerSixteenth (source);
    const int sourceSixteenths = PatternTools::lengthInSixteenths (source);

    for (int t = 0; t < kNumTracks; ++t)
        for (int s16 = 0; s16 < 8 && s16 < sourceSixteenths; ++s16)
            for (int k = 0; k < per16; ++k)
                fill.tracks[(size_t) t].steps[(size_t) (s16 * per16 + k)] = source.tracks[(size_t) t].steps[(size_t) (s16 * per16 + k)];

    Options o = opts;
    o.length = fill.length;
    o.resolution = fill.resolution;
    o.locked = {};
    Builder b (fill, o);
    const float v = opts.variation;

    // Keep the bar anchored however the source was built.
    if (! b.isOn (kKick, 0))
        b.place (kKick, 0, b.rng.velocity (122, 4));

    if (opts.style != SubStyle::Halftime && ! b.isOn (kSnare, 4))
        b.place (kSnare, 4, b.rng.velocity (118, 5));

    // Second half: thin out the cymbals, push the kick, then a snare build.
    for (int s16 = 8; s16 < 16; ++s16)
        for (int t : { kOpen, kRide, kGhost, kFx })
            if (auto* st = b.stepAt (t, s16))
                *st = Step {};

    for (int s16 : { 8, 10, 12 })
        b.place (kClosed, s16, b.rng.velocity (78, 6));

    b.place (kKick, 8, b.rng.velocity (116, 5));

    if (b.rng.chance (0.5f + 0.4f * v))
        b.place (kKick, opts.style == SubStyle::Jungle ? 11 : 14, b.rng.velocity (104, 8));

    const int shape = (int) (b.rng.next() * 3.0f);

    if (shape == 0)
    {
        // Straight 16th build with a crescendo and a ratcheted finish.
        for (int s16 = 8; s16 < 16; ++s16)
            b.place (kSnare, s16, 64 + (s16 - 8) * 9, 0.0f, s16 >= 14 && b.rng.chance (0.4f + 0.5f * v) ? 2 : 1);
    }
    else if (shape == 1)
    {
        // Syncopated build: off-beat pushes and a triplet roll into the downbeat.
        for (int s16 : { 9, 10, 12, 13, 14 })
            b.place (kSnare, s16, b.rng.velocity (88 + (s16 - 9) * 6, 5));
        b.place (kSnare, 15, 124, 0.0f, 3);
    }
    else
    {
        // Break-chop: snares and ghosts trade places like an edited break.
        for (int s16 : { 9, 11, 12, 15 })
            b.place (kSnare, s16, b.rng.velocity (104, 10), 0.02f);
        for (int s16 : { 10, 13, 14 })
            b.place (kGhost, s16, b.rng.velocity (46, 6), 0.03f);
        b.place (kSnare, 14, 112, 0.0f, 2);
    }

    // Perc answers the snare on the last beat when the style uses perc.
    if (b.def.perc[3] > 0.0f || b.def.perc[7] > 0.0f)
        for (int s16 : { 12, 14 })
            if (! b.isOn (kSnare, s16) && b.rng.chance (0.5f))
                b.place (kPerc, s16, b.rng.velocity (90, 8));

    // Accent the build.
    for (int s16 = 12; s16 < 16; ++s16)
        fill.accents[(size_t) (s16 * b.per16)] = true;

    return fill;
}

} // namespace bf::PatternGenerator
