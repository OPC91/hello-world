#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/ABToggle.h"
#include "ui/AnalysisPanel.h"
#include "ui/LoudnessPanel.h"
#include "ui/ReferenceListPanel.h"
#include "ui/Theme.h"
#include "ui/WaveformView.h"

namespace benchmark
{
    class BenchmarkEditor final : public juce::AudioProcessorEditor,
                                  public juce::FileDragAndDropTarget,
                                  private juce::ChangeListener,
                                  private juce::Timer
    {
    public:
        explicit BenchmarkEditor (BenchmarkProcessor&);
        ~BenchmarkEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void visibilityChanged() override;
        void mouseDown (const juce::MouseEvent&) override;

        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray& files, int, int) override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void showMessage (const juce::String& text);
        void updateReferenceName();

        BenchmarkProcessor& processor;
        ui::Theme theme;
        juce::TooltipWindow tooltips { this, 600 };

        ui::ABToggle abToggle;
        ui::ReferenceListPanel referenceList;
        ui::LoudnessPanel loudness;
        ui::WaveformView waveform;
        ui::AnalysisPanel analysis;

        juce::Rectangle<int> headerArea;
        juce::String message;
        juce::uint32 messageExpiry = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BenchmarkEditor)
    };
}
