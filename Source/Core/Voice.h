#pragma once

#include "Params.h"
#include "SampleData.h"

namespace bf
{

/** Topology-preserving-transform state-variable filter (Cytomic/Zavalishin), one channel. */
struct SvfState
{
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }
};

struct SvfCoeffs
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.4142f;

    void set (float cutoffHz, float resonance, double sampleRate) noexcept;
};

/** Returns {low, high} outputs; the band output is not needed here. */
inline void svfProcess (SvfState& s, const SvfCoeffs& c, float x, float& low, float& high) noexcept
{
    const float v3 = x - s.ic2;
    const float v1 = c.a1 * s.ic1 + c.a2 * v3;
    const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
    s.ic1 = 2.0f * v1 - s.ic1;
    s.ic2 = 2.0f * v2 - s.ic2;
    low = v2;
    high = x - c.k * v1 - v2;
}

/** One playing instance of a track's sample. */
class Voice
{
public:
    struct Trigger
    {
        SampleData* sample = nullptr;
        int track = 0;
        int delaySamples = 0;      // start this many samples into the next render()
        float velocity = 1.0f;     // 0..1
        float pitchSemis = 0.0f;   // step pitch offset
        double gateSamples = 0.0;  // ADSR note length
        std::uint64_t age = 0;     // monotonically increasing, used for voice stealing
    };

    void start (const Trigger&, const TrackParams&, float velocityDepth, double sampleRate) noexcept;

    /** Starts a fast fade-out at the given sample offset of the next render() call. */
    void choke (int atSample) noexcept;

    /** Adds this voice's output to the two channel pointers. */
    void render (float* left, float* right, int numSamples, const TrackParams&, double sampleRate) noexcept;

    void kill() noexcept;

    bool isActive() const noexcept { return active; }
    int getTrack() const noexcept  { return track; }
    std::uint64_t getAge() const noexcept { return age; }

private:
    enum class Stage { Attack, Decay, Release, Done };

    void updateFilters (const TrackParams&, double sampleRate) noexcept;
    float nextEnvelope() noexcept;

    SampleData::Ptr sample;
    bool active = false;
    int track = 0;
    std::uint64_t age = 0;
    int delay = 0;
    int chokeAt = -1;

    double position = 0.0, increment = 1.0;
    float gainL = 1.0f, gainR = 1.0f;
    float velocity = 1.0f;

    EnvMode envMode = EnvMode::OneShot;
    Stage stage = Stage::Done;
    float env = 0.0f, attackStep = 1.0f, decayCoef = 1.0f, sustain = 1.0f, releaseCoef = 0.99f;
    double gateRemaining = 0.0;

    SvfCoeffs lpCoeffs, hpCoeffs;
    std::array<SvfState, 2> lp, hp;
    bool lpOn = false, hpOn = false;
    float filterMod = 1.0f;
};

} // namespace bf
