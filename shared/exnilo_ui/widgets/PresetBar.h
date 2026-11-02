#pragma once

namespace exnilo
{
    /** The preset pill under the hero knob: ◀  Preset Name  ▶. Clicking the name opens the preset menu
        (the owner builds it, so each product decides how its presets are grouped). */
    class PresetBar : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        PresetBar();

        void setPresetName (const juce::String& name, bool modified);
        std::function<void()> onPrevious, onNext, onShowMenu;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        enum class Zone { none, previous, name, next };
        Zone zoneAt (juce::Point<int>) const;
        juce::Rectangle<int> previousArea, nextArea;
        juce::String presetName { "Init" };
        bool isModified = false;
        Zone hovered = Zone::none;
    };
}
