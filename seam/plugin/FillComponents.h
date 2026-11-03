#pragma once

#include "PluginProcessor.h"

#include <exnilo_ui/exnilo_ui.h>

namespace seam
{
    /** Drop a breakbeat here (or click to browse). Shows the waveform with each slice marked by type. */
    class BreakZone final : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
    {
    public:
        explicit BreakZone (SeamProcessor&);

        void refresh();   ///< call when the worker's view changes
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
        void filesDropped (const juce::StringArray& files, int, int) override;

        static bool isAudioFile (const juce::String& path);
        static juce::Colour colourFor (engine::SliceType, const exnilo::GenreTheme&);

    private:
        void browse();
        SeamProcessor& processor;
        BreakView view;
        bool dragOver = false;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    /** Drag onto a DAW track to drop the current fill as a MIDI clip (C1 = slice 1). */
    class MidiDragHandle final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit MidiDragHandle (SeamProcessor&);
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

        /** Writes the fill's MIDI file to the temp folder (also used by tests). */
        juce::File writeMidiFile() const;

    private:
        SeamProcessor& processor;
        bool dragStarted = false;
    };
}
