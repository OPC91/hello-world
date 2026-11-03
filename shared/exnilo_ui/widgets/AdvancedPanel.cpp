namespace exnilo
{
    AdvancedPanel::AdvancedPanel (juce::AudioProcessorValueTreeState& s, std::vector<Item> items) : state (s)
    {
        for (const auto& item : items)
        {
            auto row = std::make_unique<Row>();
            row->name = item.name;
            row->slider.setTooltip (item.tooltip);
            row->slider.getProperties().set ("exnilo.markUnity", true);
            row->slider.setDoubleClickReturnValue (true, 100.0);
            row->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, item.parameterId, row->slider);
            row->slider.getProperties().set ("exnilo.unityValue", 100.0);   // the design-amount marker
            row->value.setJustificationType (juce::Justification::centredRight);
            row->value.setFont (fonts::bold (15.0f));
            auto* rowPtr = row.get();
            row->slider.onValueChange = [rowPtr] { rowPtr->value.setText (juce::String (juce::roundToInt (rowPtr->slider.getValue())) + "%", juce::dontSendNotification); };
            row->slider.onValueChange();
            addAndMakeVisible (row->slider);
            addAndMakeVisible (row->value);
            rows.push_back (std::move (row));
        }

        resetButton.onClick = [this]
        {
            for (auto& row : rows)
                row->slider.setValue (100.0, juce::sendNotificationSync);
        };
        closeButton.getProperties().set ("exnilo.accent", true);
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (resetButton);
        addAndMakeVisible (closeButton);
    }

    void AdvancedPanel::paint (juce::Graphics& g)
    {
        const auto& t = themeOf (*this);
        drawCard (g, getLocalBounds().toFloat(), t);
        drawCaption (g, "ADVANCED", { 24.0f, 16.0f, 200.0f, 20.0f });
        g.setColour (palette::dim);
        g.setFont (fonts::regular (14.0f));
        g.drawText ("Scale each effect from 0 to 150% of the preset's design. Then save it as your own preset.",
                    juce::Rectangle<float> (130.0f, 16.0f, static_cast<float> (getWidth()) - 160.0f, 20.0f), juce::Justification::centredLeft, true);

        g.setFont (fonts::bold (16.0f));
        for (const auto& row : rows)
        {
            g.setColour (palette::ink);
            g.drawText (row->name, row->slider.getBounds().withX (24).withWidth (120), juce::Justification::centredLeft, false);
        }
    }

    void AdvancedPanel::resized()
    {
        int y = 52;
        const int rowHeight = juce::jmin (40, (getHeight() - 52 - 56) / juce::jmax (1, static_cast<int> (rows.size())));
        for (auto& row : rows)
        {
            row->slider.setBounds (150, y, getWidth() - 150 - 110, rowHeight);
            row->value.setBounds (getWidth() - 104, y, 80, rowHeight);
            y += rowHeight;
        }
        resetButton.setBounds (24, getHeight() - 48, 200, 34);
        closeButton.setBounds (getWidth() - 144, getHeight() - 48, 120, 34);
    }
}
