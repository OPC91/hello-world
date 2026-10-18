#include "ReferenceListPanel.h"
#include "Theme.h"

namespace benchmark::ui
{
    using Status = ReferenceLibrary::Status;

    ReferenceListPanel::ReferenceListPanel (ReferenceLibrary& lib)
        : library (lib)
    {
        setTitle ("References");
        addButton.setWantsKeyboardFocus (false);
        addButton.onClick = [this] { openAddDialog(); };
        addAndMakeVisible (addButton);

        library.addChangeListener (this);
    }

    ReferenceListPanel::~ReferenceListPanel()
    {
        library.removeChangeListener (this);
    }

    void ReferenceListPanel::setDropHighlight (bool shouldHighlight)
    {
        if (dropHighlight != shouldHighlight)
        {
            dropHighlight = shouldHighlight;
            repaint();
        }
    }

    void ReferenceListPanel::openAddDialog()
    {
        chooser = std::make_unique<juce::FileChooser> ("Add reference tracks", juce::File(), ReferenceDecoder::getSupportedWildcard());

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (! fc.getResults().isEmpty())
                                      library.addFiles (fc.getResults());
                              });
    }

    void ReferenceListPanel::openLocateDialog (int index)
    {
        const auto& entry = library[index];
        const auto startDir = entry.file.getParentDirectory();

        chooser = std::make_unique<juce::FileChooser> ("Locate \"" + entry.file.getFileName() + "\"",
                                                       startDir.isDirectory() ? startDir : juce::File(),
                                                       ReferenceDecoder::getSupportedWildcard());

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, id = entry.id] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() == juce::File())
                                      return;

                                  for (int i = 0; i < library.size(); ++i)
                                      if (library[i].id == id)
                                          library.relocate (i, fc.getResult());
                              });
    }

    //==============================================================================
    juce::Rectangle<int> ReferenceListPanel::getListArea() const
    {
        auto& theme = getTheme (const_cast<ReferenceListPanel&> (*this));
        auto area = getLocalBounds().reduced (juce::roundToInt (theme.px (8.0f)));
        area.removeFromTop (juce::roundToInt (theme.px (24.0f)));
        area.removeFromBottom (juce::roundToInt (theme.px (40.0f)));
        return area;
    }

    juce::Rectangle<int> ReferenceListPanel::getRowBounds (int index) const
    {
        auto& theme = getTheme (const_cast<ReferenceListPanel&> (*this));
        const auto area = getListArea();
        const auto rowHeight = juce::jmin (juce::roundToInt (theme.px (36.0f)), area.getHeight() / ReferenceLibrary::maxReferences);
        return { area.getX(), area.getY() + index * rowHeight, area.getWidth(), rowHeight };
    }

    juce::Rectangle<int> ReferenceListPanel::getRemoveBounds (int index) const
    {
        const auto row = getRowBounds (index);
        return row.withLeft (row.getRight() - row.getHeight()).reduced (row.getHeight() / 5);
    }

    int ReferenceListPanel::getRowAt (juce::Point<int> position) const
    {
        for (int i = 0; i < library.size(); ++i)
            if (getRowBounds (i).contains (position))
                return i;
        return -1;
    }

    void ReferenceListPanel::resized()
    {
        auto& theme = getTheme (*this);
        auto area = getLocalBounds().reduced (juce::roundToInt (theme.px (8.0f)));
        addButton.setBounds (area.removeFromBottom (juce::roundToInt (theme.px (32.0f))));
    }

    //==============================================================================
    void ReferenceListPanel::paint (juce::Graphics& g)
    {
        auto& theme = getTheme (*this);
        const auto bounds = getLocalBounds().toFloat();
        const auto corner = theme.px (8.0f);

        g.setColour (Colours::panel);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (dropHighlight ? Colours::reference : Colours::outline);
        g.drawRoundedRectangle (bounds.reduced (dropHighlight ? 1.0f : 0.5f), corner, dropHighlight ? 2.0f : 1.0f);

        auto header = getLocalBounds().reduced (juce::roundToInt (theme.px (12.0f)), 0)
                          .removeFromTop (juce::roundToInt (theme.px (30.0f)));
        g.setColour (Colours::textSecondary);
        g.setFont (theme.font (11.0f, true).withExtraKerningFactor (0.1f));
        g.drawText ("REFERENCES", header, juce::Justification::centredLeft);
        g.setColour (Colours::textMuted);
        g.setFont (theme.font (11.0f));
        g.drawText (juce::String (library.size()) + " / " + juce::String (ReferenceLibrary::maxReferences),
                    header, juce::Justification::centredRight);

        if (library.size() == 0 || dropHighlight)
        {
            g.setColour (dropHighlight ? Colours::reference : Colours::textMuted);
            g.setFont (theme.font (12.0f));
            const auto text = dropHighlight ? juce::String ("Drop to add references")
                                            : juce::String ("Drag WAV, AIFF, FLAC or MP3 files here,\nor use the button below.");
            g.drawFittedText (text, getListArea().withTrimmedTop (getRowBounds (library.size()).getY() - getListArea().getY()),
                              juce::Justification::centred, 3);
        }

        for (int i = 0; i < library.size(); ++i)
            paintRow (g, i);
    }

    void ReferenceListPanel::paintRow (juce::Graphics& g, int index)
    {
        auto& theme = getTheme (*this);
        const auto& entry = library[index];
        const auto isActive = index == library.getActiveIndex();
        const auto isHover = index == hoverRow;
        auto row = getRowBounds (index).toFloat().reduced (0.0f, theme.px (1.5f));

        if (isActive || isHover)
        {
            g.setColour (isActive ? Colours::panelRaised : Colours::panelRaised.withAlpha (0.5f));
            g.fillRoundedRectangle (row, theme.px (4.0f));
        }

        if (isActive)
        {
            g.setColour (Colours::reference);
            g.fillRoundedRectangle (row.withWidth (theme.px (3.0f)), theme.px (1.5f));
        }

        auto content = row.reduced (theme.px (10.0f), 0.0f);

        // Index number.
        g.setColour (isActive ? Colours::reference : Colours::textMuted);
        g.setFont (theme.font (11.0f, true));
        g.drawText (juce::String (index + 1), content.removeFromLeft (theme.px (18.0f)), juce::Justification::centredLeft);

        // Right-hand status/info column.
        auto info = content.removeFromRight (theme.px (78.0f));
        const auto problem = entry.status == Status::missing || entry.status == Status::failed;

        if (isHover)
        {
            const auto cross = getRemoveBounds (index).toFloat().reduced (theme.px (3.0f));
            g.setColour (Colours::textSecondary);
            g.drawLine ({ cross.getTopLeft(), cross.getBottomRight() }, theme.px (1.4f));
            g.drawLine ({ cross.getBottomLeft(), cross.getTopRight() }, theme.px (1.4f));
            info.removeFromRight (theme.px (20.0f));
        }

        g.setFont (theme.font (11.0f));

        switch (entry.status)
        {
            case Status::ready:
                g.setColour (Colours::textSecondary);
                g.drawText (formatLufs (entry.info.integratedStereo) + " LUFS", info, juce::Justification::centredRight);
                break;

            case Status::loading:
            {
                const auto progress = entry.getProgress();
                const auto barArea = info.withSizeKeepingCentre (info.getWidth(), theme.px (3.0f));
                g.setColour (Colours::outline);
                g.fillRoundedRectangle (barArea, theme.px (1.5f));
                g.setColour (Colours::reference);
                g.fillRoundedRectangle (barArea.withWidth (barArea.getWidth() * progress), theme.px (1.5f));

                // Spinner left of the bar.
                const auto r = theme.px (5.0f);
                const auto centre = juce::Point<float> (info.getX() - r * 2.0f, info.getCentreY());
                juce::Path arc;
                arc.addCentredArc (centre.x, centre.y, r, r, spinnerAngle, 0.0f, juce::MathConstants<float>::pi * 1.3f, true);
                g.strokePath (arc, juce::PathStrokeType (theme.px (1.5f)));
                break;
            }

            case Status::waiting:
                g.setColour (Colours::textMuted);
                g.drawText ("queued", info, juce::Justification::centredRight);
                break;

            case Status::missing:
                g.setColour (Colours::warning);
                g.drawText ("missing", info, juce::Justification::centredRight);
                break;

            case Status::failed:
                g.setColour (Colours::warning);
                g.drawText ("error", info, juce::Justification::centredRight);
                break;
        }

        // Name.
        g.setColour (problem ? Colours::warning.withAlpha (0.85f) : (isActive ? Colours::textPrimary : Colours::textSecondary));
        g.setFont (theme.font (12.5f, isActive));
        g.drawText (entry.name, content.withTrimmedRight (theme.px (6.0f)), juce::Justification::centredLeft, true);
    }

    //==============================================================================
    void ReferenceListPanel::mouseMove (const juce::MouseEvent& e)
    {
        const auto row = getRowAt (e.getPosition());

        if (row != hoverRow)
        {
            hoverRow = row;
            repaint();
        }
    }

    void ReferenceListPanel::mouseExit (const juce::MouseEvent&)
    {
        hoverRow = -1;
        repaint();
    }

    void ReferenceListPanel::mouseDown (const juce::MouseEvent& e)
    {
        const auto row = getRowAt (e.getPosition());

        if (row < 0)
            return;

        if (e.mods.isPopupMenu())
        {
            showRowMenu (row);
            return;
        }

        if (getRemoveBounds (row).contains (e.getPosition()))
        {
            library.remove (row);
            hoverRow = -1;
            return;
        }

        const auto status = library[row].status;

        if (status == Status::missing || status == Status::failed)
            openLocateDialog (row);
        else
            library.setActive (row);
    }

    void ReferenceListPanel::showRowMenu (int row)
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Make active", row != library.getActiveIndex());
        menu.addItem (2, "Locate file...");
        menu.addItem (3, "Show in file browser", library[row].file.exists());
        menu.addSeparator();
        menu.addItem (4, "Remove");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [this, id = library[row].id] (int result)
                            {
                                int index = -1;
                                for (int i = 0; i < library.size(); ++i)
                                    if (library[i].id == id)
                                        index = i;

                                if (index < 0)
                                    return;

                                switch (result)
                                {
                                    case 1: library.setActive (index); break;
                                    case 2: openLocateDialog (index); break;
                                    case 3: library[index].file.revealToUser(); break;
                                    case 4: library.remove (index); break;
                                    default: break;
                                }
                            });
    }

    juce::String ReferenceListPanel::getTooltip()
    {
        const auto row = getRowAt (getMouseXYRelative());

        if (row < 0)
            return {};

        const auto& entry = library[row];
        auto text = entry.file.getFullPathName();

        if (entry.status == Status::missing)
            text << "\nFile not found. Click to locate it.";
        else if (entry.status == Status::failed)
            text << "\n" << entry.message << ". Click to choose another file.";
        else if (entry.hasInfo)
            text << "\n" << formatDuration (entry.info.lengthSeconds)
                 << "  |  " << formatLufs (entry.info.integratedStereo) << " LUFS integrated"
                 << "  |  LRA " << juce::String (entry.info.loudnessRange, 1) << " LU"
                 << "  |  peak " << juce::String (juce::Decibels::gainToDecibels (entry.info.samplePeak), 1) << " dBFS";

        return text;
    }

    //==============================================================================
    void ReferenceListPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (library.isAnyLoading())
            startTimerHz (30);

        repaint();
    }

    void ReferenceListPanel::timerCallback()
    {
        spinnerAngle = std::fmod (spinnerAngle + 0.25f, juce::MathConstants<float>::twoPi);

        if (! library.isAnyLoading())
            stopTimer();

        repaint();
    }
}
