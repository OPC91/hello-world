namespace exnilo
{
    PresetBar::PresetBar()
    {
        setTooltip ("Click for presets. Arrows step through them.");
    }

    void PresetBar::setPresetName (const juce::String& name, bool modified)
    {
        if (name != presetName || modified != isModified)
        {
            presetName = name;
            isModified = modified;
            repaint();
        }
    }

    void PresetBar::resized()
    {
        const int h = getHeight();
        previousArea = getLocalBounds().removeFromLeft (h + 8);
        nextArea = getLocalBounds().removeFromRight (h + 8);
    }

    PresetBar::Zone PresetBar::zoneAt (juce::Point<int> p) const
    {
        if (! getLocalBounds().contains (p)) return Zone::none;
        if (previousArea.contains (p)) return Zone::previous;
        if (nextArea.contains (p)) return Zone::next;
        return Zone::name;
    }

    void PresetBar::mouseUp (const juce::MouseEvent& e)
    {
        switch (zoneAt (e.getPosition()))
        {
            case Zone::previous: if (onPrevious) onPrevious(); break;
            case Zone::next:     if (onNext) onNext(); break;
            case Zone::name:     if (onShowMenu) onShowMenu(); break;
            case Zone::none:     break;
        }
    }

    void PresetBar::mouseMove (const juce::MouseEvent& e)
    {
        const auto z = zoneAt (e.getPosition());
        if (z != hovered) { hovered = z; repaint(); }
    }

    void PresetBar::mouseExit (const juce::MouseEvent&)
    {
        hovered = Zone::none;
        repaint();
    }

    void PresetBar::paint (juce::Graphics& g)
    {
        const auto& t = themeOf (*this);
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        const float radius = area.getHeight() * 0.5f;

        g.setColour (t.tint (palette::surface2, 0.1f).brighter (hovered == Zone::name ? 0.06f : 0.0f));
        g.fillRoundedRectangle (area, radius);
        g.setColour (t.tint (palette::line, 0.1f).brighter (0.2f));
        g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.2f);

        auto arrow = [&] (juce::Rectangle<int> zone, bool left, bool hover)
        {
            const auto c = zone.toFloat().getCentre();
            const float s = area.getHeight() * 0.2f;
            juce::Path p;
            if (left) p.addTriangle (c.x + s * 0.7f, c.y - s, c.x + s * 0.7f, c.y + s, c.x - s * 0.8f, c.y);
            else      p.addTriangle (c.x - s * 0.7f, c.y - s, c.x - s * 0.7f, c.y + s, c.x + s * 0.8f, c.y);
            g.setColour (hover ? t.accentA : palette::ink);
            g.fillPath (p);
        };
        arrow (previousArea, true, hovered == Zone::previous);
        arrow (nextArea, false, hovered == Zone::next);

        g.setColour (palette::ink);
        g.setFont (fonts::bold (area.getHeight() * 0.44f));
        const auto nameArea = area.toNearestInt().withTrimmedLeft (previousArea.getWidth()).withTrimmedRight (nextArea.getWidth());
        g.drawFittedText (presetName + (isModified ? " *" : ""), nameArea, juce::Justification::centred, 1, 0.85f);
    }
}
