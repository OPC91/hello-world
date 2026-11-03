#pragma once

namespace exnilo
{
    /** The Exnilo LookAndFeel. Holds the current genre theme; widgets read it through themeOf(). */
    class ExniloLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        ExniloLookAndFeel();

        void setTheme (const GenreTheme& newTheme);
        const GenreTheme& getTheme() const noexcept { return *theme; }

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

        // Combo box: pill with a gradient outline (the genre picker and small choice boxes).
        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                           int buttonW, int buttonH, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;

        // Popup menus.
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                const juce::String& shortcutKeyText, const juce::Drawable* icon,
                                const juce::Colour* textColour) override;
        void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
        juce::Font getPopupMenuFont() override;

        // Buttons.
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isMouseOverButton, bool isButtonDown) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

        /** Toggle buttons are drawn as switches with a label to the right (Nil2Max's options card). */
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isMouseOverButton, bool isButtonDown) override;

        // Sliders: rotary = small knob, linear = Advanced-panel bar.
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                               float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

        void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                               juce::Rectangle<int> parentArea) override;

        /** Draws an arc as short segments blending from accent A to accent B (JUCE has no conic gradient). The colour
            reaches accent B at startAngle + fullSweep, so a half-turned knob shows the halfway colour. */
        static void strokeGradientArc (juce::Graphics&, juce::Point<float> centre, float radius, float startAngle,
                                       float endAngle, float fullSweep, float thickness, const GenreTheme&, float alpha = 1.0f);

    private:
        const GenreTheme* theme;
    };

    /** The theme of whatever Exnilo LookAndFeel the component uses (Classic if it uses another LookAndFeel). */
    const GenreTheme& themeOf (const juce::Component&);

    /** A rounded surface card, like the panels in Nil2Max. */
    void drawCard (juce::Graphics&, juce::Rectangle<float> area, const GenreTheme&, float radius = 16.0f);
}
