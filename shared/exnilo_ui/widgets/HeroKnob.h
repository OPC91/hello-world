#pragma once

namespace exnilo
{
    /** The big glowing knob in the middle of every Exnilo plugin (INTENSITY in Nil2Max, TRANSITION in SEAM).

        A gradient arc in the theme colours, lit tick marks, a glow that grows with the value and a large percentage
        readout. Drag up/down or sideways; ctrl/cmd-drag for fine control; double-click to reset.
        An optional "live" value (e.g. where an automatic ramp has pushed the effect) is drawn as a thin outer arc. */
    class HeroKnob : public juce::Slider
    {
    public:
        explicit HeroKnob (juce::String labelText);

        void setLabel (juce::String text) { label = std::move (text); repaint(); }
        /** Proportion 0..1 shown as the thin outer "live" arc; negative hides it. */
        void setLiveProportion (float proportion);

        void paint (juce::Graphics&) override;

        static constexpr float startAngle = -2.35619449f; // -135 degrees from 12 o'clock
        static constexpr float endAngle = 2.35619449f;

    private:
        juce::String label;
        float liveProportion = -1.0f;
    };
}
