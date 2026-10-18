#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace benchmark::ui
{
    // Original dark palette. Mix is cool (cyan), Reference is warm (amber).
    namespace Colours
    {
        inline const juce::Colour background   { 0xff111317 };
        inline const juce::Colour panel        { 0xff191c22 };
        inline const juce::Colour panelRaised  { 0xff21252d };
        inline const juce::Colour outline      { 0xff2c313b };
        inline const juce::Colour textPrimary  { 0xffe8eaee };
        inline const juce::Colour textSecondary{ 0xff8a919e };
        inline const juce::Colour textMuted    { 0xff596070 };
        inline const juce::Colour mix          { 0xff3ec7d6 };
        inline const juce::Colour reference    { 0xfff2a541 };
        inline const juce::Colour warning      { 0xffe86a5c };

        // Chart marks: the same hues stepped for the dark chart surface
        // (validated: OKLCH L 0.48-0.67, CVD separation 17 dE, >= 3:1 contrast).
        inline const juce::Colour mixChart       { 0xff22a3b0 };
        inline const juce::Colour referenceChart { 0xffc98026 };
        inline const juce::Colour grid           { 0xff262a32 };
    }

    // Design is laid out on a 1000 x 640 grid and scaled with the window.
    inline constexpr int baseWidth = 1000;
    inline constexpr int baseHeight = 640;

    class Theme final : public juce::LookAndFeel_V4
    {
    public:
        Theme();

        void setScale (float newScale) noexcept { scale = newScale; }
        float getScale() const noexcept { return scale; }

        juce::Font font (float baseSize, bool bold = false) const;
        float px (float baseValue) const noexcept { return baseValue * scale; }

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
