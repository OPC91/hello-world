namespace exnilo
{
    //==============================================================================
    Segmented::Segmented (juce::StringArray l) : labels (std::move (l)) {}

    void Segmented::setLabels (juce::StringArray newLabels)
    {
        labels = std::move (newLabels);
        selected = juce::jlimit (0, juce::jmax (0, labels.size() - 1), selected);
        repaint();
    }

    void Segmented::setSelected (int index, juce::NotificationType notification)
    {
        index = juce::jlimit (0, juce::jmax (0, labels.size() - 1), index);
        if (index == selected)
            return;
        selected = index;
        repaint();
        if (notification != juce::dontSendNotification && onChange)
            onChange (selected);
    }

    int Segmented::indexAt (juce::Point<float> p) const
    {
        if (labels.isEmpty() || ! getLocalBounds().toFloat().contains (p))
            return -1;
        return juce::jlimit (0, labels.size() - 1, static_cast<int> (p.x / static_cast<float> (getWidth()) * static_cast<float> (labels.size())));
    }

    void Segmented::mouseDown (const juce::MouseEvent& e)
    {
        if (isEnabled())
            if (const int i = indexAt (e.position); i >= 0)
                setSelected (i);
    }

    void Segmented::mouseMove (const juce::MouseEvent& e)
    {
        const int i = indexAt (e.position);
        if (i != hovered) { hovered = i; repaint(); }
    }

    void Segmented::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        repaint();
    }

    void Segmented::paint (juce::Graphics& g)
    {
        const auto& t = themeOf (*this);
        const auto area = getLocalBounds().toFloat().reduced (0.5f);
        const float radius = area.getHeight() * 0.5f;
        const float alpha = isEnabled() ? 1.0f : 0.45f;

        g.setColour (t.tint (palette::bg2).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (area, radius);
        g.setColour (t.tint (palette::line, 0.1f).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (area, radius, 1.0f);

        if (labels.isEmpty())
            return;

        const float w = area.getWidth() / static_cast<float> (labels.size());
        g.setFont (fonts::bold (juce::jmin (15.0f, area.getHeight() * 0.5f)));
        for (int i = 0; i < labels.size(); ++i)
        {
            const auto cell = juce::Rectangle<float> (area.getX() + w * static_cast<float> (i), area.getY(), w, area.getHeight()).reduced (2.5f);
            if (i == selected)
            {
                g.setGradientFill (juce::ColourGradient (t.accentA.withMultipliedAlpha (alpha), cell.getX(), 0.0f,
                                                         t.accentB.withMultipliedAlpha (alpha), cell.getRight() + w, 0.0f, false));
                g.fillRoundedRectangle (cell, cell.getHeight() * 0.5f);
                g.setColour (palette::onAccent);
            }
            else
            {
                if (i == hovered && isEnabled())
                {
                    g.setColour (t.tint (palette::surface2, 0.1f));
                    g.fillRoundedRectangle (cell, cell.getHeight() * 0.5f);
                }
                g.setColour (palette::dim.withMultipliedAlpha (alpha));
            }
            g.drawFittedText (labels[i], cell.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
    }

    //==============================================================================
    SegmentedAttachment::SegmentedAttachment (juce::RangedAudioParameter& parameter, Segmented& c, juce::UndoManager* undo)
        : control (c),
          attachment (parameter, [this, &parameter] (float value)
          {
              control.setSelected (juce::roundToInt (parameter.convertFrom0to1 (parameter.convertTo0to1 (value))), juce::dontSendNotification);
          }, undo)
    {
        control.onChange = [this] (int index)
        {
            attachment.setValueAsCompleteGesture (static_cast<float> (index));
        };
        attachment.sendInitialUpdate();
    }

    SegmentedAttachment::~SegmentedAttachment() { control.onChange = nullptr; }

    //==============================================================================
    void OutMeter::push (float left, float right, float dt)
    {
        const std::array<float, 2> in { left, right };
        for (size_t i = 0; i < 2; ++i)
        {
            const float db = juce::Decibels::gainToDecibels (in[i], -60.0f);
            const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); // -60 .. +6 dB
            level[i] = norm > level[i] ? norm : level[i] + (norm - level[i]) * (1.0f - std::exp (-dt * 9.0f));
            if (norm >= peak[i]) { peak[i] = norm; peakHold[i] = 1.2f; }
            else if ((peakHold[i] -= dt) <= 0.0f) peak[i] = juce::jmax (level[i], peak[i] - dt * 0.6f);
        }
        repaint();
    }

    void OutMeter::paint (juce::Graphics& g)
    {
        const auto& t = themeOf (*this);
        auto area = getLocalBounds().toFloat();
        auto label = area.removeFromBottom (22.0f);
        const float gap = 6.0f;
        const float barW = (area.getWidth() - gap) * 0.5f;

        for (int i = 0; i < 2; ++i)
        {
            const auto bar = juce::Rectangle<float> (area.getX() + static_cast<float> (i) * (barW + gap), area.getY(), barW, area.getHeight());
            g.setColour (t.tint (palette::bg2).darker (0.2f));
            g.fillRoundedRectangle (bar, 3.0f);

            const float lv = level[static_cast<size_t> (i)];
            if (lv > 0.0f)
            {
                const auto filled = bar.withTop (bar.getBottom() - bar.getHeight() * lv);
                juce::ColourGradient grad (palette::meterLow, bar.getX(), bar.getBottom(), palette::meterHigh, bar.getX(), bar.getY(), false);
                grad.addColour (0.62, palette::meterMid);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (filled, 3.0f);
            }

            const float pk = peak[static_cast<size_t> (i)];
            if (pk > 0.0f)
            {
                g.setColour (pk > 0.9f ? palette::meterHigh : palette::ink.withAlpha (0.85f));
                g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * pk - 1.5f, barW, 3.0f);
            }
        }

        // 0 dB tick marks
        g.setColour (palette::dim.withAlpha (0.5f));
        for (const float db : { 0.0f, -12.0f, -24.0f })
        {
            const float y = area.getBottom() - area.getHeight() * (db + 60.0f) / 66.0f;
            g.fillRect (area.getX() - 3.0f, y, 3.0f, 1.0f);
            g.fillRect (area.getRight(), y, 3.0f, 1.0f);
        }

        drawCaption (g, "OUT", label, juce::Justification::centredBottom);
    }

    //==============================================================================
    Wordmark::Wordmark (std::vector<Part> p) : parts (std::move (p))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void Wordmark::mouseUp (const juce::MouseEvent& e)
    {
        if (onClick && getLocalBounds().contains (e.getPosition()))
            onClick();
    }

    void Wordmark::paint (juce::Graphics& g)
    {
        const auto& t = themeOf (*this);
        const auto area = getLocalBounds().toFloat();
        const auto font = fonts::heavy (area.getHeight() * 0.78f).withExtraKerningFactor (0.04f);
        float x = area.getX();

        for (const auto& part : parts)
        {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (font, part.text, x, area.getCentreY() + font.getAscent() * 0.36f);
            const float w = juce::GlyphArrangement::getStringWidth (font, part.text);
            juce::Path path;
            glyphs.createPath (path);

            switch (part.ink)
            {
                case Ink::plain:    g.setColour (palette::ink); break;
                case Ink::accentA:  g.setColour (t.accentA); break;
                case Ink::gradient: g.setGradientFill (juce::ColourGradient (t.accentA, x, 0.0f, t.accentB, x + w, 0.0f, false)); break;
            }
            g.fillPath (path);
            x += w;
        }

        x += area.getHeight() * 0.35f;
        g.setColour (isMouseOver() ? palette::ink : palette::dim);
        g.setFont (fonts::bold (area.getHeight() * 0.36f));
        g.drawText ("by EXNILO", juce::Rectangle<float> (x, area.getY(), area.getRight() - x, area.getHeight()).translated (0.0f, area.getHeight() * 0.08f),
                    juce::Justification::centredLeft, false);
    }

    //==============================================================================
    void Card::paint (juce::Graphics& g)
    {
        const auto& t = themeOf (*this);
        drawCard (g, getLocalBounds().toFloat(), t);
        if (title.isNotEmpty())
            drawCaption (g, title, juce::Rectangle<float> (20.0f, 14.0f, static_cast<float> (getWidth()) - 40.0f, 20.0f));
    }

    void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification j, juce::Colour colour)
    {
        g.setColour (colour);
        g.setFont (fonts::bold (13.5f).withExtraKerningFactor (0.08f));
        g.drawText (text, area, j, false);
    }
}
