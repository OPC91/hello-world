#pragma once

namespace exnilo
{
    /** The animated energy display: a glowing core that swells with the effect, rings firing outward in the theme's
        shape (hexagons for drum & bass, squares for techno ...), and rain, streaks or sparkles for some themes.
        Products can paint their own information on top through `overlay` (SEAM draws its bar countdown there). */
    class EnergyDisplay : public juce::Component
    {
    public:
        EnergyDisplay();

        /** 0..1. Smoothed internally, so it can be set from a timer at any rate. */
        void setEnergy (float newEnergy) noexcept { targetEnergy = juce::jlimit (0.0f, 1.0f, newEnergy); }
        /** Position within the current beat, 0..1 (negative when the transport is stopped): the core pulses on beats. */
        void setBeatPhase (float phase) noexcept { beatPhase = phase; }
        /** Sets whether the animation runs (off-screen editors should stop it). */
        void setAnimating (bool shouldAnimate);

        std::function<void (juce::Graphics&, juce::Rectangle<float> area, const GenreTheme&)> overlay;

        void paint (juce::Graphics&) override;
        /** Advances the animation by dt seconds (called on vblank; public so snapshot tests can step it). */
        void advance (float dt);

    private:
        struct Ring { float age = 0.0f, life = 1.0f, spin = 0.0f; bool active = false; };
        struct Drop { float x = 0.0f, y = 0.0f, speed = 0.0f, length = 0.0f, alpha = 0.0f; };
        struct Mote { float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f, phase = 0.0f; };

        static void addShape (juce::Path&, RingShape, juce::Point<float> centre, float radius, float rotation);

        std::array<Ring, 24> rings;
        std::array<Drop, 70> drops;
        std::array<Mote, 14> motes;
        juce::Random rng { 0x5ea4 };
        float energy = 0.0f, targetEnergy = 0.0f, beatPhase = -1.0f;
        float spawnClock = 0.0f, time = 0.0f;
        double lastFrame = 0.0;
        std::unique_ptr<juce::VBlankAttachment> vblank;
    };
}
