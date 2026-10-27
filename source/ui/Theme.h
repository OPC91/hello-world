#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace benchmark::ui
{
    // Exnilo palette, matched to Nil2Max (Trance theme) and exnilo.co.uk.
    // Mix is cyan, Reference is pink; violet is the neutral brand accent.
    namespace Colours
    {
        inline const juce::Colour background    { 0xff0b1026 };
        inline const juce::Colour backgroundTop { 0xff141c3d };
        inline const juce::Colour panel         { 0xff151d40 };
        inline const juce::Colour panelLow      { 0xff111836 }; // bottom of the panel gradient
        inline const juce::Colour panelRaised   { 0xff1c2956 };
        inline const juce::Colour outline       { 0xff2a3570 };
        inline const juce::Colour textPrimary   { 0xfff4f3ff };
        inline const juce::Colour textSecondary { 0xff98a6da };
        inline const juce::Colour textMuted     { 0xff5b6692 };
        inline const juce::Colour mix           { 0xff39d0ff };
        inline const juce::Colour reference     { 0xffff5fa2 };
        inline const juce::Colour violet        { 0xffb46bff };
        inline const juce::Colour amber         { 0xffffb547 };
        inline const juce::Colour warning       { 0xffff7466 };

        // Chart marks: the same hues stepped for the navy chart surface
        // (validated: OKLCH L 0.48-0.67, CVD separation 10 dE, >= 3:1 contrast).
        inline const juce::Colour mixChart       { 0xff1b9dcf };
        inline const juce::Colour referenceChart { 0xffe2559a };
        inline const juce::Colour grid           { 0xff212b58 };
    }

    class Theme;

    /** Window background: navy gradient, soft violet glow and a faint dot grid. */
    void paintBackground (juce::Graphics&, juce::Rectangle<int> bounds, const Theme&);

    /** A rounded card like Nil2Max's panels. */
    void paintPanel (juce::Graphics&, juce::Rectangle<float> bounds, const Theme&, bool highlighted = false);

    /** Pill outline in the cyan-to-violet brand gradient. */
    void strokeBrandPill (juce::Graphics&, juce::Rectangle<float> bounds, float thickness);

    /** Small uppercase section label ("OPTIONS" style). */
    void paintSectionLabel (juce::Graphics&, const Theme&, const juce::String& text, juce::Rectangle<float> area,
                            juce::Colour colour = Colours::textSecondary);

    // Design is laid out on a 1000 x 640 grid and scaled with the window.
    inline constexpr int baseWidth = 1000;
    inline constexpr int baseHeight = 660;

    class Theme final : public juce::LookAndFeel_V4
    {
    public:
        Theme();

        void setScale (float newScale) noexcept { scale = newScale; }
        float getScale() const noexcept { return scale; }

        juce::Font font (float baseSize, bool bold = false) const;
        float px (float baseValue) const noexcept { return baseValue * scale; }
        float panelCorner() const noexcept { return px (14.0f); }

        void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getPopupMenuFont() override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

    private:
        float scale = 1.0f;
    };

    /** Formats a loudness value, e.g. "-9.4" or "--" when there is no measurement. */
    juce::String formatLufs (float lufs);

    /** Formats a duration as m:ss. */
    juce::String formatDuration (double seconds);

    // Components fetch the editor's Theme through their LookAndFeel.
    inline Theme& getTheme (juce::Component& c)
    {
        if (auto* t = dynamic_cast<Theme*> (&c.getLookAndFeel()))
            return *t;

        static Theme fallback;
        return fallback;
    }
}
