#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "reference/ReferenceLibrary.h"

namespace benchmark
{
    class BenchmarkProcessor;
}

namespace benchmark::ui
{
    /**
        Waveform overview of the active reference with playhead, click-to-seek,
        drag-to-loop, three loop markers and the Free / Sync transport controls.
    */
    class WaveformView final : public juce::Component,
                               public juce::TooltipClient,
                               private juce::ChangeListener,
                               private juce::Timer
    {
    public:
        explicit WaveformView (BenchmarkProcessor& processor);
        ~WaveformView() override;

        /** Plain-language feedback for the editor's message area. */
        std::function<void (const juce::String&)> onMessage;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        juce::String getTooltip() override;

    private:
        class IconButton;
        class Segmented;
        class MarkerButton;

        enum class DragMode { none, pending, creating, moveStart, moveEnd, moveLoop };

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        const ReferenceLibrary::Entry* getActiveEntry() const;
        double getLengthSeconds() const;
        bool isSyncMode() const;

        float secondsToX (double seconds) const;
        double xToSeconds (float x) const;
        DragMode hitTestLoop (juce::Point<float> position) const;

        void rebuildWaveformImages();
        void seekTo (double seconds);
        void commitLoop (std::optional<LoopRegion> region, bool seekToStart);
        void markerClicked (int slot, const juce::MouseEvent&);
        void showContextMenu();
        void updateControls();
        void message (const juce::String& text);
        void alignClicked();
        juce::String getSyncStatusText() const;

        BenchmarkProcessor& processor;
        ReferenceLibrary& library;

        std::unique_ptr<IconButton> playButton;
        std::unique_ptr<Segmented> modeSwitch;
        juce::ToggleButton loopToggle { "Loop" };
        juce::TextButton alignButton { "Align to session" };
        int lastAlignState = -1;
        int lastAlignProgress = -1;
        std::array<std::unique_ptr<MarkerButton>, ReferenceLibrary::numMarkers> markerButtons;

        juce::Rectangle<int> headerArea, waveArea, footerArea;
        juce::Image playedImage, unplayedImage;
        std::shared_ptr<const WaveformOverview> imageSource;
        juce::Rectangle<int> imageBounds;

        DragMode dragMode = DragMode::none;
        std::optional<LoopRegion> dragRegion;
        double dragAnchorSeconds = 0.0;
        LoopRegion dragOriginalLoop;
        float hoverX = -1.0f;

        int64_t lastPlayhead = -1;
        int lastControlState = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
    };
}
