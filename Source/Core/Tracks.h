#pragma once

#include "Pattern.h"

namespace bf
{

/** Drum role of a track: decides which kit samples it can use and how the generator treats it. */
enum class Role
{
    Kick,
    Snare,
    GhostSnare,
    ClosedHat,
    OpenHat,
    Ride,
    Perc,
    FX
};

struct TrackDefaults
{
    const char* name;
    Role role;
    int midiNote;    // General MIDI drum map
    int chokeGroup;  // 0 = none
};

inline constexpr std::array<TrackDefaults, kNumTracks> kTrackDefaults {{
    { "Kick",        Role::Kick,       36, 0 },
    { "Snare",       Role::Snare,      38, 0 },
    { "Ghost Snare", Role::GhostSnare, 40, 0 },
    { "Closed Hat",  Role::ClosedHat,  42, 1 },
    { "Open Hat",    Role::OpenHat,    46, 1 },
    { "Ride",        Role::Ride,       51, 0 },
    { "Perc",        Role::Perc,       37, 0 },
    { "FX",          Role::FX,         49, 0 },
}};

/** Default track index for each role (the role order matches the default track order). */
constexpr int trackForRole (Role r) noexcept { return (int) r; }

/** MIDI notes C1..D#2 (36..51, using the C3 = 60 convention) select patterns 1..16. */
constexpr int kPatternSelectBaseNote = 36;

/** MIDI notes C3..G3 (60..67) audition tracks 1..8 directly. */
constexpr int kTrackTriggerBaseNote = 60;

} // namespace bf
