#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "reference/ReferenceLibrary.h"

namespace benchmark::ui
{
    /** The list of loaded references: select, add, remove, relocate missing files. */
    class ReferenceListPanel final : public juce::Component,
                                     public juce::TooltipClient,
                                     private juce::ChangeListener,
                                     private juce::Timer
    {
    public:
        explicit ReferenceListPanel (ReferenceLibrary& library);
        ~ReferenceListPanel() override;

        void setDropHighlight (bool shouldHighlight);
        void openAddDialog();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;

        juce::String getTooltip() override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        juce::Rectangle<int> getListArea() const;
        juce::Rectangle<int> getRowBounds (int index) const;
        juce::Rectangle<int> getRemoveBounds (int index) const;
        int getRowAt (juce::Point<int> position) const;

        void paintRow (juce::Graphics&, int index);
        void openLocateDialog (int index);
        void showRowMenu (int index);

        ReferenceLibrary& library;
        juce::TextButton addButton { "+  Add references" };
        std::unique_ptr<juce::FileChooser> chooser;

        int hoverRow = -1;
        bool dropHighlight = false;
        float spinnerAngle = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceListPanel)
    };
}
