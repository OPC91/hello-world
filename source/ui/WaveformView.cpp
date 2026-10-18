#include "WaveformView.h"
#include "Theme.h"
#include "PluginProcessor.h"
#include "Parameters.h"

namespace benchmark::ui
{
    namespace
    {
        juce::String formatTime (double seconds)
        {
            seconds = juce::jmax (0.0, seconds);
            const auto minutes = (int) (seconds / 60.0);
            const auto rest = seconds - minutes * 60.0;
            return juce::String (minutes) + ":" + juce::String (rest, 1).paddedLeft ('0', 4);
        }

        juce::String describeRegion (const LoopRegion& r)
        {
            return formatTime (r.start) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + formatTime (r.end);
        }
    }

    //==============================================================================
    /** Play / pause button. */
    class WaveformView::IconButton final : public juce::Button
    {
    public:
        IconButton() : juce::Button ("Play / Pause") { setWantsKeyboardFocus (false); }

        bool showPause = false;

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto& theme = getTheme (*this);
            getLookAndFeel().drawButtonBackground (g, *this, {}, highlighted, down);

            auto icon = getLocalBounds().toFloat().reduced (getHeight() * 0.3f);
            g.setColour (isEnabled() ? Colours::textPrimary : Colours::textMuted);

            if (showPause)
            {
                const auto barWidth = icon.getWidth() * 0.32f;
                g.fillRoundedRectangle (icon.withWidth (barWidth), theme.px (1.0f));
                g.fillRoundedRectangle (icon.withLeft (icon.getRight() - barWidth), theme.px (1.0f));
            }
            else
            {
                juce::Path p;
                p.addTriangle (icon.getX() + icon.getWidth() * 0.1f, icon.getY(),
                               icon.getX() + icon.getWidth() * 0.1f, icon.getBottom(),
                               icon.getRight(), icon.getCentreY());
                g.fillPath (p);
            }
        }
    };

    //==============================================================================
    /** Two-option segmented control bound to a choice parameter. */
    class WaveformView::Segmented final : public juce::Component,
                                          public juce::SettableTooltipClient
    {
    public:
        Segmented (juce::RangedAudioParameter& p, juce::StringArray labelsIn)
            : labels (std::move (labelsIn)),
              attachment (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
        {
            setTitle (p.getName (64));
            attachment.sendInitialUpdate();
        }

        void paint (juce::Graphics& g) override
        {
            auto& theme = getTheme (*this);
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            const auto corner = bounds.getHeight() * 0.5f;

            g.setColour (Colours::panelRaised);
            g.fillRoundedRectangle (bounds, corner);
            g.setColour (Colours::outline);
            g.drawRoundedRectangle (bounds, corner, 1.0f);

            const auto segmentWidth = bounds.getWidth() / (float) labels.size();

            for (int i = 0; i < labels.size(); ++i)
            {
                auto segment = bounds.withX (bounds.getX() + segmentWidth * (float) i).withWidth (segmentWidth);

                if (i == selected)
                {
                    g.setColour (Colours::reference.withAlpha (0.2f));
                    g.fillRoundedRectangle (segment.reduced (theme.px (2.0f)), corner);
                }

                g.setColour (i == selected ? Colours::reference : Colours::textSecondary);
                g.setFont (theme.font (11.0f, true));
                g.drawText (labels[i], segment, juce::Justification::centred);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto index = juce::jlimit (0, labels.size() - 1, (int) (e.position.x / (float) getWidth() * (float) labels.size()));
            attachment.setValueAsCompleteGesture ((float) index);
        }

    private:
        juce::StringArray labels;
        juce::ParameterAttachment attachment;
        int selected = 0;
    };

    //==============================================================================
    /** One of the three loop marker slots. */
    class WaveformView::MarkerButton final : public juce::Component,
                                             public juce::TooltipClient
    {
    public:
        explicit MarkerButton (int slotIndex) : slot (slotIndex) { setTitle ("Loop marker " + juce::String (slot + 1)); }

        std::function<void (int, const juce::MouseEvent&)> onClick;
        std::optional<LoopRegion> region;
        bool isCurrent = false;

        void paint (juce::Graphics& g) override
        {
            auto& theme = getTheme (*this);
            const auto bounds = getLocalBounds().toFloat().reduced (theme.px (2.0f));
            const auto filled = region.has_value();

            if (filled)
            {
                g.setColour (isCurrent ? Colours::reference : Colours::reference.withAlpha (0.25f));
                g.fillRoundedRectangle (bounds, theme.px (4.0f));
            }

            g.setColour (filled ? Colours::reference : (isMouseOver() ? Colours::textSecondary : Colours::outline));
            g.drawRoundedRectangle (bounds, theme.px (4.0f), theme.px (1.2f));

            g.setColour (isCurrent ? Colours::background : (filled ? Colours::textPrimary : Colours::textMuted));
            g.setFont (theme.font (11.0f, true));
            g.drawText (juce::String (slot + 1), bounds, juce::Justification::centred);
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (onClick)
                onClick (slot, e);
        }

        juce::String getTooltip() override
        {
            if (region.has_value())
                return "Marker " + juce::String (slot + 1) + ": " + describeRegion (*region)
                       + "\nClick to loop it. Right-click to overwrite or clear.";

            return "Empty marker. Click to save the current loop here.";
        }

    private:
        int slot;
    };

    //==============================================================================
    WaveformView::WaveformView (BenchmarkProcessor& p)
        : processor (p), library (p.getReferenceLibrary())
    {
        setTitle ("Reference transport");

        playButton = std::make_unique<IconButton>();
        playButton->setTooltip ("Play / pause the reference (Free mode)");
        playButton->onClick = [this]
        {
            auto& transport = processor.getTransport();
            transport.freePlaying.store (! transport.freePlaying.load());
            updateControls();
        };
        addAndMakeVisible (*playButton);

        modeSwitch = std::make_unique<Segmented> (*p.getValueTreeState().getParameter (ParamIDs::syncMode),
                                                  juce::StringArray { "Free", "Sync" });
        modeSwitch->setTooltip ("Free: the reference plays on its own, with seeking and loops.\n"
                                "Sync: the reference follows your DAW's play/stop and position.");
        addAndMakeVisible (*modeSwitch);

        loopToggle.setWantsKeyboardFocus (false);
        loopToggle.setColour (juce::ToggleButton::tickColourId, Colours::reference);
        loopToggle.setTooltip ("Loop the selected region (drag on the waveform to set one)");
        loopToggle.onClick = [this]
        {
            if (library.getActiveIndex() >= 0)
                library.setLoopEnabled (library.getActiveIndex(), loopToggle.getToggleState());
        };
        addAndMakeVisible (loopToggle);

        for (int i = 0; i < ReferenceLibrary::numMarkers; ++i)
        {
            markerButtons[(size_t) i] = std::make_unique<MarkerButton> (i);
            markerButtons[(size_t) i]->onClick = [this] (int slot, const juce::MouseEvent& e) { markerClicked (slot, e); };
            addAndMakeVisible (*markerButtons[(size_t) i]);
        }

        library.addChangeListener (this);
        updateControls();
        startTimerHz (30);
    }

    WaveformView::~WaveformView()
    {
        library.removeChangeListener (this);
    }

    //==============================================================================
    const ReferenceLibrary::Entry* WaveformView::getActiveEntry() const
    {
        return library.getActive();
    }

    double WaveformView::getLengthSeconds() const
    {
        const auto* entry = getActiveEntry();
        return entry != nullptr && entry->overview != nullptr ? entry->overview->getLengthSeconds() : 0.0;
    }

    bool WaveformView::isSyncMode() const
    {
        return processor.getValueTreeState().getRawParameterValue (ParamIDs::syncMode)->load() >= 0.5f;
    }

    float WaveformView::secondsToX (double seconds) const
    {
        const auto length = getLengthSeconds();
        if (length <= 0.0)
            return (float) waveArea.getX();
        return (float) waveArea.getX() + (float) (seconds / length) * (float) waveArea.getWidth();
    }

    double WaveformView::xToSeconds (float x) const
    {
        const auto proportion = juce::jlimit (0.0, 1.0, (double) (x - (float) waveArea.getX()) / juce::jmax (1, waveArea.getWidth()));
        return proportion * getLengthSeconds();
    }

    void WaveformView::message (const juce::String& text)
    {
        if (onMessage)
            onMessage (text);
    }

    //==============================================================================
    void WaveformView::resized()
    {
        auto& theme = getTheme (*this);
        const auto px = [&theme] (float v) { return juce::roundToInt (theme.px (v)); };

        auto area = getLocalBounds();
        headerArea = area.removeFromTop (px (30.0f)).reduced (px (12.0f), 0);
        footerArea = area.removeFromBottom (px (36.0f)).reduced (px (10.0f), px (6.0f));
        waveArea = area.reduced (px (12.0f), px (2.0f));

        auto footer = footerArea;
        playButton->setBounds (footer.removeFromLeft (px (28.0f)));
        footer.removeFromLeft (px (8.0f));
        modeSwitch->setBounds (footer.removeFromLeft (px (96.0f)));
        footer.removeFromLeft (px (12.0f));
        loopToggle.setBounds (footer.removeFromLeft (px (64.0f)));

        for (int i = ReferenceLibrary::numMarkers - 1; i >= 0; --i)
        {
            markerButtons[(size_t) i]->setBounds (footer.removeFromRight (px (26.0f)));
            footer.removeFromRight (px (2.0f));
        }

        imageBounds = {};
        repaint();
    }

    void WaveformView::rebuildWaveformImages()
    {
        const auto* entry = getActiveEntry();
        const auto overview = entry != nullptr ? entry->overview : nullptr;

        imageSource = overview;
        imageBounds = waveArea;

        if (overview == nullptr || waveArea.isEmpty() || overview->getNumBuckets() == 0)
        {
            playedImage = unplayedImage = {};
            return;
        }

        const auto w = waveArea.getWidth();
        const auto h = waveArea.getHeight();
        const auto centre = (float) h * 0.5f;
        const auto numBuckets = overview->getNumBuckets();

        const auto render = [&] (juce::Colour peakColour, juce::Colour rmsColour)
        {
            juce::Image image (juce::Image::ARGB, w, h, true);
            juce::Graphics g (image);

            for (int x = 0; x < w; ++x)
            {
                const auto b0 = (int) ((int64_t) x * numBuckets / w);
                const auto b1 = juce::jmax (b0 + 1, (int) ((int64_t) (x + 1) * numBuckets / w));
                float peak = 0.0f, rms = 0.0f;

                for (int b = b0; b < juce::jmin (b1, numBuckets); ++b)
                {
                    peak = juce::jmax (peak, overview->peak[(size_t) b]);
                    rms = juce::jmax (rms, overview->rms[(size_t) b]);
                }

                const auto peakHeight = juce::jmin (1.0f, peak) * centre;
                const auto rmsHeight = juce::jmin (1.0f, rms * 1.41f) * centre;

                g.setColour (peakColour);
                g.fillRect ((float) x, centre - peakHeight, 1.0f, juce::jmax (1.0f, peakHeight * 2.0f));
                g.setColour (rmsColour);
                g.fillRect ((float) x, centre - rmsHeight, 1.0f, juce::jmax (1.0f, rmsHeight * 2.0f));
            }

            return image;
        };

        playedImage = render (Colours::reference.withAlpha (0.45f), Colours::reference);
        unplayedImage = render (Colours::textMuted.withAlpha (0.45f), Colours::textSecondary.withAlpha (0.8f));
    }

    //==============================================================================
    void WaveformView::paint (juce::Graphics& g)
    {
        auto& theme = getTheme (*this);
        const auto bounds = getLocalBounds().toFloat();
        const auto corner = theme.px (8.0f);

        g.setColour (Colours::panel);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

        const auto* entry = getActiveEntry();
        const auto sync = isSyncMode();
        auto& transport = processor.getTransport();

        // Header: title, track name, time.
        auto header = headerArea.toFloat();
        g.setColour (Colours::textSecondary);
        g.setFont (theme.font (11.0f, true).withExtraKerningFactor (0.1f));
        const auto title = juce::String ("REFERENCE TRANSPORT");
        g.drawText (title, header, juce::Justification::centredLeft);
        header.removeFromLeft (juce::GlyphArrangement::getStringWidth (theme.font (11.0f, true).withExtraKerningFactor (0.1f), title) + theme.px (12.0f));

        const auto length = getLengthSeconds();
        const auto sampleRate = processor.getCurrentSampleRate();

        if (entry != nullptr && length > 0.0 && sampleRate > 0.0)
        {
            const auto now = (double) transport.playhead.load() / sampleRate;
            g.setColour (Colours::textPrimary);
            g.setFont (theme.font (12.0f, true));
            g.drawText (formatTime (juce::jlimit (0.0, length, now)) + "  /  " + formatTime (length), header,
                        juce::Justification::centredRight);

            g.setColour (Colours::textSecondary);
            g.setFont (theme.font (12.0f));
            g.drawText (entry->name, header.withTrimmedRight (theme.px (120.0f)), juce::Justification::centredLeft, true);
        }

        // Waveform.
        const auto wave = waveArea.toFloat();
        g.setColour (Colours::background.withAlpha (0.5f));
        g.fillRoundedRectangle (wave.expanded (theme.px (4.0f), theme.px (2.0f)), theme.px (4.0f));

        if (entry == nullptr || entry->overview == nullptr)
        {
            g.setColour (Colours::textMuted);
            g.setFont (theme.font (12.0f));
            const auto text = entry == nullptr ? juce::String ("Load a reference to see its waveform")
                                               : (entry->status == ReferenceLibrary::Status::loading ? juce::String ("Loading...")
                                                                                                     : juce::String ("No waveform available"));
            g.drawText (text, wave, juce::Justification::centred);
            return;
        }

        if (imageSource != entry->overview || imageBounds != waveArea)
            rebuildWaveformImages();

        const auto playheadX = sampleRate > 0.0 ? secondsToX ((double) transport.playhead.load() / sampleRate) : wave.getX();

        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (waveArea.withRight (juce::roundToInt (playheadX)));
            g.drawImageAt (playedImage, waveArea.getX(), waveArea.getY());
        }
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (waveArea.withLeft (juce::roundToInt (playheadX)));
            g.drawImageAt (unplayedImage, waveArea.getX(), waveArea.getY());
        }

        // Loop region (the one being dragged, or the saved one).
        const auto region = dragRegion.has_value() ? dragRegion : entry->loop;

        if (region.has_value())
        {
            const auto active = (dragRegion.has_value() || entry->loopEnabled) && ! sync;
            const auto x0 = secondsToX (region->start);
            const auto x1 = secondsToX (region->end);
            const auto loopRect = juce::Rectangle<float> (x0, wave.getY() - theme.px (2.0f), x1 - x0, wave.getHeight() + theme.px (4.0f));

            g.setColour (Colours::reference.withAlpha (active ? 0.12f : 0.05f));
            g.fillRect (loopRect);
            g.setColour (Colours::reference.withAlpha (active ? 0.9f : 0.35f));
            g.fillRect (loopRect.withWidth (theme.px (1.5f)));
            g.fillRect (loopRect.withLeft (loopRect.getRight() - theme.px (1.5f)));

            // Edge handles.
            for (const auto x : { x0, x1 })
                g.fillRoundedRectangle (juce::Rectangle<float> (theme.px (6.0f), theme.px (10.0f)).withCentre ({ x, wave.getY() + theme.px (4.0f) }),
                                        theme.px (2.0f));

            if (sync && ! dragRegion.has_value())
            {
                g.setFont (theme.font (10.0f));
                g.drawText ("loop inactive in Sync", loopRect.reduced (theme.px (4.0f)), juce::Justification::topLeft, true);
            }
        }

        // Hover guide.
        if (hoverX >= wave.getX() && hoverX <= wave.getRight() && dragMode == DragMode::none && ! sync)
        {
            g.setColour (Colours::textSecondary.withAlpha (0.35f));
            g.fillRect (hoverX, wave.getY(), 1.0f, wave.getHeight());
        }

        // Playhead.
        g.setColour (Colours::textPrimary);
        g.fillRect (playheadX - theme.px (0.75f), wave.getY() - theme.px (3.0f), theme.px (1.5f), wave.getHeight() + theme.px (6.0f));

        if (sync)
        {
            g.setColour (Colours::textMuted);
            g.setFont (theme.font (10.0f));
            const auto status = ! transport.hostPositionAvailable.load() ? juce::String ("host gives no position: playing freely")
                                                                          : (transport.hostPlaying.load() ? juce::String ("following host")
                                                                                                          : juce::String ("waiting for host to play"));
            g.drawText (status, footerArea.toFloat().withTrimmedLeft ((float) (loopToggle.getRight() - footerArea.getX()) + theme.px (12.0f))
                                    .withTrimmedRight ((float) (footerArea.getRight() - markerButtons[0]->getX()) + theme.px (8.0f)),
                        juce::Justification::centredLeft, true);
        }
    }

    //==============================================================================
    WaveformView::DragMode WaveformView::hitTestLoop (juce::Point<float> position) const
    {
        const auto* entry = getActiveEntry();

        if (entry != nullptr && entry->loop.has_value())
        {
            const auto tolerance = getTheme (const_cast<WaveformView&> (*this)).px (6.0f);
            const auto x0 = secondsToX (entry->loop->start);
            const auto x1 = secondsToX (entry->loop->end);

            if (std::abs (position.x - x0) <= tolerance) return DragMode::moveStart;
            if (std::abs (position.x - x1) <= tolerance) return DragMode::moveEnd;
            if (position.x > x0 && position.x < x1)      return DragMode::moveLoop;
        }

        return DragMode::creating;
    }

    void WaveformView::mouseMove (const juce::MouseEvent& e)
    {
        const auto inWave = waveArea.expanded (0, 4).contains (e.getPosition()) && getLengthSeconds() > 0.0;
        hoverX = inWave ? e.position.x : -1.0f;

        auto newCursor = juce::MouseCursor::NormalCursor;
        if (inWave)
        {
            const auto mode = hitTestLoop (e.position);
            newCursor = (mode == DragMode::moveStart || mode == DragMode::moveEnd) ? juce::MouseCursor::LeftRightResizeCursor
                                                                                  : juce::MouseCursor::IBeamCursor;
        }

        setMouseCursor (newCursor);
        repaint (waveArea.expanded (2));
    }

    void WaveformView::mouseExit (const juce::MouseEvent&)
    {
        hoverX = -1.0f;
        repaint (waveArea.expanded (2));
    }

    void WaveformView::mouseDown (const juce::MouseEvent& e)
    {
        dragMode = DragMode::none;

        if (! waveArea.expanded (0, 4).contains (e.getPosition()) || getLengthSeconds() <= 0.0)
            return;

        if (e.mods.isPopupMenu())
        {
            showContextMenu();
            return;
        }

        dragMode = DragMode::pending;
        dragAnchorSeconds = xToSeconds (e.position.x);

        if (const auto* entry = getActiveEntry(); entry != nullptr && entry->loop.has_value())
            dragOriginalLoop = *entry->loop;
    }

    void WaveformView::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragMode == DragMode::none)
            return;

        if (dragMode == DragMode::pending)
        {
            if (e.getDistanceFromDragStart() < 4)
                return;

            dragMode = hitTestLoop (e.mouseDownPosition);
        }

        const auto now = xToSeconds (e.position.x);
        const auto length = getLengthSeconds();

        switch (dragMode)
        {
            case DragMode::creating:  dragRegion = LoopRegion { juce::jmin (dragAnchorSeconds, now), juce::jmax (dragAnchorSeconds, now) }; break;
            case DragMode::moveStart: dragRegion = LoopRegion { juce::jmin (now, dragOriginalLoop.end), juce::jmax (now, dragOriginalLoop.end) }; break;
            case DragMode::moveEnd:   dragRegion = LoopRegion { juce::jmin (now, dragOriginalLoop.start), juce::jmax (now, dragOriginalLoop.start) }; break;
            case DragMode::moveLoop:
            {
                const auto delta = juce::jlimit (-dragOriginalLoop.start, length - dragOriginalLoop.end, now - dragAnchorSeconds);
                dragRegion = LoopRegion { dragOriginalLoop.start + delta, dragOriginalLoop.end + delta };
                break;
            }
            case DragMode::none:
            case DragMode::pending:
                break;
        }

        repaint();
    }

    void WaveformView::mouseUp (const juce::MouseEvent& e)
    {
        const auto mode = std::exchange (dragMode, DragMode::none);

        if (mode == DragMode::pending)
        {
            seekTo (xToSeconds (e.position.x));
        }
        else if (dragRegion.has_value())
        {
            const auto region = *dragRegion;
            dragRegion.reset();

            if (region.getLength() >= ReferenceLibrary::minLoopSeconds)
                commitLoop (region, mode == DragMode::creating || mode == DragMode::moveLoop);
        }

        repaint();
    }

    void WaveformView::seekTo (double seconds)
    {
        if (isSyncMode())
        {
            message ("Sync mode follows your DAW's transport. Switch to Free to seek.");
            return;
        }

        const auto sampleRate = processor.getCurrentSampleRate();
        if (sampleRate > 0.0)
            processor.getTransport().requestSeek ((int64_t) std::llround (seconds * sampleRate));
    }

    void WaveformView::commitLoop (std::optional<LoopRegion> region, bool seekToStart)
    {
        const auto index = library.getActiveIndex();
        if (index < 0)
            return;

        library.setLoop (index, region);

        if (region.has_value() && seekToStart && ! isSyncMode())
            seekTo (region->start);
    }

    void WaveformView::markerClicked (int slot, const juce::MouseEvent& e)
    {
        const auto index = library.getActiveIndex();
        const auto* entry = getActiveEntry();

        if (entry == nullptr)
            return;

        const auto& marker = entry->markers[(size_t) slot];

        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Save current loop here", entry->loop.has_value());
            menu.addItem (2, "Clear marker", marker.has_value());

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (markerButtons[(size_t) slot].get()),
                                [this, slot, id = entry->id] (int result)
                                {
                                    const auto* active = getActiveEntry();
                                    if (active == nullptr || active->id != id)
                                        return;

                                    if (result == 1)
                                        library.setMarker (library.getActiveIndex(), slot, active->loop);
                                    else if (result == 2)
                                        library.setMarker (library.getActiveIndex(), slot, std::nullopt);
                                });
            return;
        }

        if (marker.has_value())
        {
            library.recallMarker (index, slot);

            if (isSyncMode())
                message ("Marker loaded. Loops play in Free mode.");
        }
        else if (entry->loop.has_value())
        {
            library.setMarker (index, slot, entry->loop);
            message ("Loop saved to marker " + juce::String (slot + 1));
        }
        else
        {
            message ("Drag across the waveform to make a loop, then click a marker to save it");
        }
    }

    void WaveformView::showContextMenu()
    {
        const auto* entry = getActiveEntry();
        if (entry == nullptr)
            return;

        juce::PopupMenu menu;
        menu.addItem (1, "Clear loop", entry->loop.has_value());
        menu.addItem (2, "Loop whole track");
        menu.addSeparator();

        for (int slot = 0; slot < ReferenceLibrary::numMarkers; ++slot)
            menu.addItem (10 + slot, "Save loop to marker " + juce::String (slot + 1), entry->loop.has_value());

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [this, id = entry->id] (int result)
                            {
                                const auto* active = getActiveEntry();
                                if (active == nullptr || active->id != id)
                                    return;

                                const auto index = library.getActiveIndex();

                                if (result == 1)
                                    library.setLoop (index, std::nullopt);
                                else if (result == 2)
                                    commitLoop (LoopRegion { 0.0, getLengthSeconds() }, false);
                                else if (result >= 10)
                                    library.setMarker (index, result - 10, active->loop);
                            });
    }

    juce::String WaveformView::getTooltip()
    {
        if (! waveArea.contains (getMouseXYRelative()) || getLengthSeconds() <= 0.0)
            return {};

        if (isSyncMode())
            return "Sync mode: the reference follows your DAW's transport. You can still edit loops and markers.";

        return "Click to jump. Drag to make a loop, then drag its edges or middle to change it. Right-click for more.";
    }

    //==============================================================================
    void WaveformView::updateControls()
    {
        const auto* entry = getActiveEntry();
        const auto sync = isSyncMode();
        auto& transport = processor.getTransport();

        playButton->showPause = sync ? transport.hostPlaying.load() : transport.freePlaying.load();
        playButton->setEnabled (! sync && entry != nullptr);
        playButton->repaint();

        loopToggle.setEnabled (entry != nullptr && entry->loop.has_value() && ! sync);
        loopToggle.setToggleState (entry != nullptr && entry->loopEnabled, juce::dontSendNotification);

        for (int i = 0; i < ReferenceLibrary::numMarkers; ++i)
        {
            auto& button = *markerButtons[(size_t) i];
            button.region = entry != nullptr ? entry->markers[(size_t) i] : std::nullopt;
            button.isCurrent = entry != nullptr && button.region.has_value() && entry->loop == button.region && entry->loopEnabled;
            button.setEnabled (entry != nullptr);
            button.repaint();
        }
    }

    void WaveformView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        updateControls();
        repaint();
    }

    void WaveformView::timerCallback()
    {
        auto& transport = processor.getTransport();
        const auto playhead = transport.playhead.load();

        // Keep play / sync state in step with the host and the parameter.
        const auto controlState = (isSyncMode() ? 1 : 0) | (transport.hostPlaying.load() ? 2 : 0)
                                | (transport.freePlaying.load() ? 4 : 0) | (transport.hostPositionAvailable.load() ? 8 : 0);

        if (controlState != lastControlState)
        {
            lastControlState = controlState;
            updateControls();
            repaint();
        }

        if (playhead != lastPlayhead)
        {
            lastPlayhead = playhead;
            repaint();
        }
    }
}
