#pragma once

#include <array>
#include <cstdint>

namespace bf
{

constexpr int kNumTracks   = 8;
constexpr int kMaxSteps    = 32;
constexpr int kNumPatterns = 16;

enum class Resolution : std::uint8_t
{
    Sixteenth    = 0,
    ThirtySecond = 1
};

/** One cell of the step grid. Plain data so whole banks can be copied lock-free. */
struct Step
{
    bool on = false;
    std::uint8_t velocity = 100;    // 1..127
    std::uint8_t probability = 100; // 0..100 %
    std::uint8_t ratchet = 1;       // 1..4 hits inside the step
    float nudge = 0.0f;             // -0.5..+0.5 of a step
    std::int8_t pitch = 0;          // -12..+12 semitones
    float length = 1.0f;            // gate length in steps (0.1..16), used by ADSR voices and MIDI out

    bool operator== (const Step&) const = default;
};

struct TrackPattern
{
    std::array<Step, kMaxSteps> steps {};

    bool operator== (const TrackPattern&) const = default;
};

struct Pattern
{
    std::array<TrackPattern, kNumTracks> tracks {};
    std::array<bool, kMaxSteps> accents {};
    int length = 16; // 16 or 32
    Resolution resolution = Resolution::Sixteenth;

    double stepLengthPpq() const noexcept { return resolution == Resolution::Sixteenth ? 0.25 : 0.125; }
    double lengthPpq() const noexcept     { return stepLengthPpq() * length; }

    /** Number of steps that make up one quarter note at the pattern's resolution. */
    int stepsPerBeat() const noexcept     { return resolution == Resolution::Sixteenth ? 4 : 8; }

    bool isEmpty() const noexcept;
    void clear() noexcept;

    bool operator== (const Pattern&) const = default;
};

struct PatternBank
{
    std::array<Pattern, kNumPatterns> patterns {};

    bool operator== (const PatternBank&) const = default;
};

} // namespace bf
