#pragma once

#include "PluginProcessor.h"
#include "FillComponents.h"

#include <exnilo_ui/exnilo_ui.h>

namespace seam
{
    class SeamEditor final : public exnilo::ScaledEditor, public juce::FileDragAndDropTarget, private juce::Timer
    {
    public:
        explicit SeamEditor (SeamProcessor&);
        ~SeamEditor() override;

        static constexpr int baseWidth = 960;
        static constexpr int baseHeight = 640;

        void setAdvancedVisible (bool);

        /** Steps the UI as if `seconds` had passed (offscreen snapshot tests). */
        void advanceForSnapshot (float seconds);

        // A break dropped anywhere on the editor goes to the fill panel.
        bool isInterestedInFileDrag (const juce::StringArray& files) override { return breakZone.isInterestedInFileDrag (files); }
        void filesDropped (const juce::StringArray& files, int x, int y) override { breakZone.filesDropped (files, x, y); }

    private:
        void timerCallback() override;
        void layout();
        void applyTheme (const juce::String& id);
        void paintCountdown (juce::Graphics&, juce::Rectangle<float> area, const exnilo::GenreTheme&);
        void updateTagline();
        void showPresetMenu();
        void stepPreset (int delta);
        void savePresetAs();


        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        SeamProcessor& seamProcessor;
        juce::AudioProcessorValueTreeState& params;

        // Header
        exnilo::Wordmark wordmark { { { "SE", exnilo::Wordmark::Ink::plain }, { "AM", exnilo::Wordmark::Ink::gradient } } };
        juce::ComboBox themeBox;
        juce::TextButton advButton { "ADV" };

        // Main row
        exnilo::EnergyDisplay energy;
        exnilo::HeroKnob knob { "TRANSITION" };
        exnilo::Card options { "OPTIONS" };
        exnilo::Segmented phrase { choices::phrase };
        juce::ToggleButton autoToggle { "Auto ramp" };
        juce::ComboBox autoBarsBox, autoCurveBox;
        juce::ToggleButton vacuumToggle { "Vacuum" };
        exnilo::Segmented vacuumSize { choices::vacuumSize };
        juce::ToggleButton tapeStopToggle { "Tape stop" };
        exnilo::OutMeter meter;

        // Fill row
        struct FillCard : exnilo::Card
        {
            FillCard() : exnilo::Card ("FILL") {}
            void paintOverChildren (juce::Graphics& g) override
            {
                exnilo::drawCaption (g, "COMPLEXITY", { 598.0f, 102.0f, 120.0f, 18.0f }, juce::Justification::centred);
                g.setColour (exnilo::palette::dim);
                g.setFont (exnilo::fonts::regular (13.0f));
                g.drawText (description, juce::Rectangle<float> (70.0f, 12.0f, 820.0f, 22.0f), juce::Justification::centredLeft, true);
            }
            juce::String description;
        } fillCard;
        BreakZone breakZone { seamProcessor };
        exnilo::Segmented style { choices::style };
        exnilo::Segmented fillLength { juce::StringArray { "1 bar", "2 bars", "4 bars" } };
        exnilo::Segmented fillMode { choices::fillMode };
        juce::Slider complexity { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::TextButton newFillButton { "NEW FILL" };
        MidiDragHandle midiDrag { seamProcessor };

        // Advanced (over the main row when ADV is on)
        exnilo::AdvancedPanel advanced;

        // Footer
        exnilo::PresetBar presetBar;
        juce::Label tagline, hint;

        std::unique_ptr<SliderAttachment> knobAttachment, complexityAttachment;
        std::unique_ptr<ButtonAttachment> autoAttachment, vacuumAttachment, tapeStopAttachment;
        std::unique_ptr<ComboAttachment> autoBarsAttachment, autoCurveAttachment;
        std::vector<std::unique_ptr<exnilo::SegmentedAttachment>> segmentedAttachments;

        // Latest transport state for the countdown overlay.
        bool playing = false;
        double barsToSeam = 0.0;
        int barInPhrase = 0, phraseBars = 16;
        double lastTick = 0.0;
        int lastViewVersion = -1;
        int presetCheckCountdown = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeamEditor)
    };
}
