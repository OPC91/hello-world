#include "FillComponents.h"

namespace seam
{
    //==============================================================================
    BreakZone::BreakZone (SeamProcessor& p) : processor (p)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Drop a breakbeat (WAV, AIFF, FLAC) here, or click to browse. Right-click for the built-in break.");
        refresh();
    }

    bool BreakZone::isAudioFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;flac");
    }

    juce::Colour BreakZone::colourFor (engine::SliceType type, const exnilo::GenreTheme& t)
    {
        switch (type)
        {
            case engine::SliceType::kick:  return t.accentA;
            case engine::SliceType::snare: return t.accentB;
            case engine::SliceType::ghost: return t.accentB.withAlpha (0.55f);
            case engine::SliceType::hat:   return exnilo::palette::ink.withAlpha (0.7f);
        }
        return exnilo::palette::dim;
    }

    void BreakZone::refresh()
    {
        view = processor.getFillWorker().getView();
        repaint();
    }

    bool BreakZone::isInterestedInFileDrag (const juce::StringArray& files)
    {
        return std::any_of (files.begin(), files.end(), [] (const juce::String& f) { return isAudioFile (f); });
    }

    void BreakZone::filesDropped (const juce::StringArray& files, int, int)
    {
        dragOver = false;
        for (const auto& f : files)
            if (isAudioFile (f))
            {
                processor.loadBreakFile (juce::File (f));
                break;
            }
        refresh();
    }

    void BreakZone::browse()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a breakbeat", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<BreakZone> (this)] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && fc.getResult().existsAsFile())
                                  {
                                      safe->processor.loadBreakFile (fc.getResult());
                                      safe->refresh();
                                  }
                              });
    }

    void BreakZone::mouseUp (const juce::MouseEvent& e)
    {
        if (! getLocalBounds().contains (e.getPosition()))
            return;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem ("Load a break...", [this] { browse(); });
            menu.addItem ("Use the built-in break", ! view.builtIn, false, [this] { processor.useBuiltInBreak(); refresh(); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
            return;
        }
        browse();
    }

    void BreakZone::paint (juce::Graphics& g)
    {
        const auto& t = exnilo::themeOf (*this);
        const auto area = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (t.tint (exnilo::palette::bg2).withAlpha (0.9f));
        g.fillRoundedRectangle (area, 10.0f);
        g.setColour (dragOver ? t.accentA : t.tint (exnilo::palette::line, 0.1f));
        const float dash[] { 5.0f, 4.0f };
        juce::Path outline;
        outline.addRoundedRectangle (area.reduced (0.5f), 10.0f);
        juce::Path dashed;
        juce::PathStrokeType (dragOver ? 2.0f : 1.0f).createDashedStroke (dashed, outline, dash, 2);
        g.fillPath (dashed);

        auto inner = area.reduced (10.0f, 6.0f);
        auto label = inner.removeFromBottom (16.0f);

        // Waveform + slice markers.
        if (! view.peaks.empty())
        {
            const float w = inner.getWidth() / static_cast<float> (view.peaks.size());
            const float mid = inner.getCentreY();
            g.setColour (t.accentA.withAlpha (0.45f));
            for (size_t i = 0; i < view.peaks.size(); ++i)
            {
                const float h = std::min (1.0f, view.peaks[i]) * inner.getHeight() * 0.5f;
                g.fillRect (inner.getX() + static_cast<float> (i) * w, mid - h, std::max (1.0f, w), std::max (0.5f, h * 2.0f));
            }
            for (const auto& [x, type] : view.markers)
            {
                g.setColour (colourFor (type, t));
                const float px = inner.getX() + x * inner.getWidth();
                g.fillRect (px - 0.75f, inner.getY(), 1.5f, inner.getHeight());
                g.fillEllipse (px - 2.5f, inner.getY() - 1.0f, 5.0f, 5.0f);
            }
        }

        juce::String text;
        if (view.loading) text = "Slicing...";
        else if (view.error.isNotEmpty()) text = view.error;
        else if (view.builtIn) text = "Built-in break  |  drop a WAV to use yours";
        else text = view.name + "  |  " + view.summary;
        g.setColour (view.error.isNotEmpty() ? exnilo::palette::meterHigh : exnilo::palette::dim);
        g.setFont (exnilo::fonts::regular (12.5f));
        g.drawFittedText (text, label.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

    //==============================================================================
    MidiDragHandle::MidiDragHandle (SeamProcessor& p) : processor (p)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag onto a track: the current fill as a MIDI clip (C1 = slice 1, upward)");
    }

    juce::File MidiDragHandle::writeMidiFile() const
    {
        const auto bytes = processor.getFillWorker().exportMidi (processor.getLastBpm(), processor.getLastNumerator(),
                                                                processor.getLastDenominator());
        if (bytes.empty())
            return {};
        const auto styleIndex = juce::roundToInt (processor.getState().getRawParameterValue (ids::style)->load());
        const auto name = "SEAM fill " + choices::style[juce::jlimit (0, 3, styleIndex)] + " "
                          + juce::String (processor.getFillWorker().getSeed()) + ".mid";
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SEAM").getChildFile (name);
        file.getParentDirectory().createDirectory();
        if (! file.replaceWithData (bytes.data(), bytes.size()))
            return {};
        return file;
    }

    void MidiDragHandle::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragStarted || e.getDistanceFromDragStart() < 6)
            return;
        dragStarted = true;
        const auto file = writeMidiFile();
        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }

    void MidiDragHandle::paint (juce::Graphics& g)
    {
        const auto& t = exnilo::themeOf (*this);
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        const float r = area.getHeight() * 0.5f;
        g.setColour (t.tint (exnilo::palette::surface2, 0.1f).brighter (isMouseOver() ? 0.08f : 0.0f));
        g.fillRoundedRectangle (area, r);
        g.setGradientFill (juce::ColourGradient (t.accentA, area.getX(), 0.0f, t.accentB, area.getRight(), 0.0f, false));
        g.drawRoundedRectangle (area.reduced (0.75f), r, 1.5f);

        // Grip dots + label.
        auto content = area.reduced (14.0f, 0.0f);
        auto grip = content.removeFromLeft (14.0f);
        g.setColour (exnilo::palette::dim);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                g.fillEllipse (grip.getX() + static_cast<float> (col) * 6.0f, grip.getCentreY() - 6.0f + static_cast<float> (row) * 6.0f, 2.6f, 2.6f);
        g.setColour (exnilo::palette::ink);
        g.setFont (exnilo::fonts::bold (14.0f));
        g.drawText ("DRAG MIDI", content, juce::Justification::centred, false);
    }
}
