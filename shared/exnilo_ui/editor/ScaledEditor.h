#pragma once

namespace exnilo
{
    /** Base for Exnilo editors: the UI is laid out once at a fixed base size inside `content`, and the whole thing is
        scaled from 75% to 200% with the corner grip (aspect ratio locked). The scale is remembered by the product
        (getScale / setScale) so it survives reopening the editor. */
    class ScaledEditor : public juce::AudioProcessorEditor
    {
    public:
        ScaledEditor (juce::AudioProcessor&, int baseWidth, int baseHeight, float initialScale);
        ~ScaledEditor() override;

        static constexpr float minScale = 0.75f;
        static constexpr float maxScale = 2.0f;

        float getScale() const noexcept { return scale; }
        void setScale (float newScale);
        /** Called whenever the user resizes (to persist the scale). */
        std::function<void (float)> onScaleChanged;

        void resized() override;
        void paint (juce::Graphics&) override;

        ExniloLookAndFeel& getExniloLookAndFeel() noexcept { return lookAndFeel; }

    protected:
        /** Put every child component in here, positioned in base (100%) coordinates. */
        juce::Component content;
        const int baseWidth, baseHeight;

    private:
        ExniloLookAndFeel lookAndFeel;
        juce::TooltipWindow tooltips { this, 600 };
        float scale = 1.0f;
    };
}
