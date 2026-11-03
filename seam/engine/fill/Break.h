#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace seam::engine
{
    enum class SliceType { kick = 0, snare = 1, ghost = 2, hat = 3 };
    inline constexpr int numSliceTypes = 4;
    const char* sliceTypeName (SliceType) noexcept;

    /** One hit in the break. Positions are in samples of the break's own sample rate. */
    struct Slice
    {
        size_t start = 0;
        size_t length = 0;
        SliceType type = SliceType::snare;
        float peak = 0.0f;          ///< peak level of the hit
        float centroid = 0.0f;      ///< spectral centroid of its attack (Hz)
        float lowRatio = 0.0f;      ///< share of attack energy below 150 Hz
    };

    /** A sliced break, ready for the audio thread. Immutable once published. */
    struct BreakKit
    {
        double sampleRate = 44100.0;
        std::vector<float> left, right;
        std::vector<Slice> slices;
        std::string name;
        std::uint64_t id = 0;   ///< unique per loaded break; fill patterns name the kit they were made for

        /** Slice indices of each type, in break order. */
        std::array<std::vector<int>, numSliceTypes> byType;
        void indexByType();
        bool empty() const noexcept { return slices.empty(); }
    };

    /** The slice summary the fill generator needs (no audio). */
    struct KitSummary
    {
        std::array<std::vector<int>, numSliceTypes> byType;
        std::vector<double> lengthSeconds;  ///< per slice
        int numSlices = 0;

        static KitSummary of (const BreakKit&);
    };

    /** A built-in break so SEAM makes fills before any WAV is dropped: a synthesised two-step drum loop
        (kicks, snares, ghost notes, hats) at 170 BPM, one bar long. Deterministic. */
    BreakKit makeBuiltInBreak (double sampleRate);
    /** The onsets (seconds) and types of the built-in break, for tests. */
    struct BuiltInHit { double time; SliceType type; float level; };
    const std::vector<BuiltInHit>& builtInBreakHits();
}
