#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Pattern.h"

namespace bf
{

enum class EnvMode   { OneShot = 0, ADSR = 1 };
enum class MidiPatternMode { Latch = 0, Hold = 1, Off = 2 };

constexpr int kNumChokeGroups = 4;
constexpr int kNumSubStyles   = 7;
constexpr int kNumGrooves     = 6; // 5 factory + "User"

/** Parameter IDs. Per-track IDs are "t<n>_<name>" with n = 1..8. */
namespace ParamID
{
    inline constexpr const char* pattern         = "pattern";
    inline constexpr const char* swing           = "swing";
    inline constexpr const char* swing8          = "swing8";
    inline constexpr const char* groove          = "groove";
    inline constexpr const char* humanizeTime    = "humTime";
    inline constexpr const char* humanizeVel     = "humVel";
    inline constexpr const char* velocityDepth   = "velDepth";
    inline constexpr const char* accentAmount    = "accent";
    inline constexpr const char* subStyle        = "substyle";
    inline constexpr const char* variation       = "variation";
    inline constexpr const char* drive           = "drive";
    inline constexpr const char* driveOn         = "driveOn";
    inline constexpr const char* limiterOn       = "limiterOn";
    inline constexpr const char* masterVolume    = "masterVol";
    inline constexpr const char* multiOut        = "multiOut";
    inline constexpr const char* midiOut         = "midiOut";
    inline constexpr const char* midiPatternMode = "patMidiMode";

    // Per-track suffixes
    inline constexpr const char* volume     = "vol";
    inline constexpr const char* pan        = "pan";
    inline constexpr const char* pitch      = "pitch";
    inline constexpr const char* fine       = "fine";
    inline constexpr const char* start      = "start";
    inline constexpr const char* envMode    = "env";
    inline constexpr const char* attack     = "atk";
    inline constexpr const char* decay      = "dec";
    inline constexpr const char* sustain    = "sus";
    inline constexpr const char* release    = "rel";
    inline constexpr const char* lowPass    = "lp";
    inline constexpr const char* highPass   = "hp";
    inline constexpr const char* resonance  = "res";
    inline constexpr const char* choke      = "choke";
    inline constexpr const char* mute       = "mute";
    inline constexpr const char* solo       = "solo";
    inline constexpr const char* velFilter  = "velFlt";
    inline constexpr const char* velPitch   = "velPitch";
    inline constexpr const char* swingOn    = "swOn";
    inline constexpr const char* swingAmt   = "swAmt";

    juce::String track (int trackIndex, const char* suffix);
}

juce::StringArray subStyleNames();
juce::StringArray grooveNames();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Plain per-block copy of all parameter values, so the engine (and the offline renderer)
    never touch the APVTS directly. */
struct TrackParams
{
    float gain = 1.0f;          // linear
    float pan = 0.0f;           // -1..1
    float pitch = 0.0f;         // semitones (coarse + fine)
    float start = 0.0f;         // 0..1 of sample length
    EnvMode envMode = EnvMode::OneShot;
    float attackMs = 0.0f;
    float decayMs = 5000.0f;    // >= kDecayInfinite means "play the full sample"
    float sustain = 1.0f;
    float releaseMs = 50.0f;
    float lowPassHz = 20000.0f;
    float highPassHz = 20.0f;
    float resonance = 0.0f;     // 0..1
    int chokeGroup = 0;         // 0 = none
    bool mute = false;
    bool solo = false;
    float velToFilter = 0.0f;   // -1..1
    float velToPitch = 0.0f;    // semitones at full velocity
    bool swingOverride = false;
    float swing = 0.5f;         // 0.5..0.75
};

struct EngineParams
{
    int pattern = 0;            // 0-based
    float swing = 0.5f;         // 0.5..0.75
    bool swing8 = false;
    int groove = 0;
    float humanizeTimeMs = 0.0f;
    float humanizeVel = 0.0f;   // 0..0.3
    float velocityDepth = 0.8f; // 0..1
    float accentAmount = 0.3f;  // 0..1
    float drive = 0.0f;         // 0..1
    bool driveOn = false;
    bool limiterOn = true;
    float masterGain = 1.0f;
    bool multiOut = false;
    bool midiOut = false;
    MidiPatternMode midiPatternMode = MidiPatternMode::Latch;
    std::array<TrackParams, kNumTracks> tracks {};
};

constexpr float kDecayInfiniteMs = 4999.0f;

/** Caches raw parameter pointers so a full EngineParams can be read cheaply on the audio thread. */
class ParamCache
{
public:
    explicit ParamCache (juce::AudioProcessorValueTreeState&);
    void read (EngineParams&) const noexcept;

private:
    struct TrackPtrs
    {
        std::atomic<float>* p[20] {};
    };

    std::atomic<float>* global[17] {};
    std::array<TrackPtrs, kNumTracks> tracks;
};

} // namespace bf
