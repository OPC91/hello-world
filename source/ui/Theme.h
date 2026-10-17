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

    private:
        float scale = 1.0f;
    };

    // Components fetch the editor's Theme through their LookAndFeel.
    inline Theme& getTheme (juce::Component& c)
    {
        if (auto* t = dynamic_cast<Theme*> (&c.getLookAndFeel()))
            return *t;

        static Theme fallback;
        return fallback;
    }
}
