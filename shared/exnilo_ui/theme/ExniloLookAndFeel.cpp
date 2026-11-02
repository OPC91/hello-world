namespace exnilo
{
    ExniloLookAndFeel::ExniloLookAndFeel()
        : theme (&themes::byId (themes::drumAndBass))
    {
        setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme());
        setTheme (*theme);
    }

    void ExniloLookAndFeel::setTheme (const GenreTheme& newTheme)
    {
        theme = &newTheme;
        const auto surface = theme->tint (palette::surface);

        setColour (juce::ResizableWindow::backgroundColourId, theme->tint (palette::bg));
        setColour (juce::Label::textColourId, palette::ink);
        setColour (juce::TextButton::buttonColourId, theme->tint (palette::surface2));
        setColour (juce::TextButton::textColourOffId, palette::ink);
        setColour (juce::TextButton::textColourOnId, palette::onAccent);
        setColour (juce::ComboBox::backgroundColourId, theme->tint (palette::surface2));
        setColour (juce::ComboBox::textColourId, palette::ink);
        setColour (juce::ComboBox::arrowColourId, palette::ink);
        setColour (juce::PopupMenu::backgroundColourId, surface);
        setColour (juce::PopupMenu::textColourId, palette::ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme->tint (palette::surface2, 0.15f));
        setColour (juce::PopupMenu::highlightedTextColourId, palette::ink);
        setColour (juce::PopupMenu::headerTextColourId, palette::dim);
        setColour (juce::TooltipWindow::backgroundColourId, surface);
        setColour (juce::TooltipWindow::textColourId, palette::ink);
        setColour (juce::TooltipWindow::outlineColourId, palette::line);
        setColour (juce::TextEditor::backgroundColourId, theme->tint (palette::bg2));
        setColour (juce::TextEditor::textColourId, palette::ink);
        setColour (juce::TextEditor::outlineColourId, palette::line);
        setColour (juce::TextEditor::focusedOutlineColourId, theme->accentA);
        setColour (juce::TextEditor::highlightColourId, theme->accentA.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, theme->accentA);
        setColour (juce::AlertWindow::backgroundColourId, surface);
        setColour (juce::AlertWindow::textColourId, palette::ink);
        setColour (juce::AlertWindow::outlineColourId, palette::line);
        setColour (juce::Slider::textBoxTextColourId, palette::ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    juce::Typeface::Ptr ExniloLookAndFeel::getTypefaceForFont (const juce::Font& font)
    {
        return juce::LookAndFeel_V4::getTypefaceForFont (font);
    }

    //==============================================================================
    void ExniloLookAndFeel::strokeGradientArc (juce::Graphics& g, juce::Point<float> centre, float radius, float startAngle,
                                               float endAngle, float fullSweep, float thickness, const GenreTheme& t, float alpha)
    {
        const float span = endAngle - startAngle;
        if (span < 0.001f || fullSweep <= 0.0f)
            return;

        const int segments = juce::jmax (2, static_cast<int> (span / 0.04f));
        const float step = span / static_cast<float> (segments);
        const auto colourAt = [&] (float angle) { return t.at ((angle - startAngle) / fullSweep).withMultipliedAlpha (alpha); };

        for (int i = 0; i < segments; ++i)
        {
            const float a0 = startAngle + step * static_cast<float> (i);
            const float a1 = juce::jmin (endAngle, a0 + step * 1.2f); // overlap so there are no hairline gaps
            juce::Path p;
            p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, a0, a1, true);
            g.setColour (colourAt (a0 + step * 0.5f));
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }

        // Rounded end caps.
        for (const float angle : { startAngle, endAngle })
        {
            g.setColour (colourAt (angle));
            g.fillEllipse (juce::Rectangle<float> (thickness, thickness).withCentre (centre.getPointOnCircumference (radius, angle)));
        }
    }

    const GenreTheme& themeOf (const juce::Component& c)
    {
        if (auto* lnf = dynamic_cast<ExniloLookAndFeel*> (&c.getLookAndFeel()))
            return lnf->getTheme();
        return themes::all().front();
    }

    void drawCard (juce::Graphics& g, juce::Rectangle<float> area, const GenreTheme& t, float radius)
    {
        g.setGradientFill (juce::ColourGradient (t.tint (palette::surface, 0.07f), area.getTopLeft(),
                                                 t.tint (palette::bg2, 0.05f), area.getBottomLeft(), false));
        g.fillRoundedRectangle (area, radius);
        g.setColour (t.tint (palette::line, 0.08f).withAlpha (0.55f));
        g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
    }

    //==============================================================================
    void ExniloLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
    {
        const auto area = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (1.0f);
        const float radius = area.getHeight() * 0.5f;
        const bool hero = box.getProperties().getWithDefault ("exnilo.hero", false);

        g.setColour (theme->tint (hero ? palette::surface : palette::surface2, 0.08f).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
        g.fillRoundedRectangle (area, radius);

        if (hero)
        {
            g.setGradientFill (juce::ColourGradient (theme->accentA, area.getX(), 0.0f, theme->accentB, area.getRight(), 0.0f, false));
            g.drawRoundedRectangle (area.reduced (1.0f), radius - 1.0f, 2.5f);
        }
        else
        {
            g.setColour (theme->tint (palette::line, 0.1f).withMultipliedAlpha (box.isMouseOver() ? 1.6f : 1.0f));
            g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
        }

        // Arrow
        const float arrowSize = juce::jmin (10.0f, area.getHeight() * 0.28f);
        const auto arrowCentre = juce::Point<float> (area.getRight() - radius - 2.0f, area.getCentreY());
        juce::Path arrow;
        arrow.addTriangle (arrowCentre.x - arrowSize * 0.6f, arrowCentre.y - arrowSize * 0.3f,
                           arrowCentre.x + arrowSize * 0.6f, arrowCentre.y - arrowSize * 0.3f,
                           arrowCentre.x, arrowCentre.y + arrowSize * 0.4f);
        g.setColour (palette::ink.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
        g.fillPath (arrow);
    }

    juce::Font ExniloLookAndFeel::getComboBoxFont (juce::ComboBox& box)
    {
        return fonts::bold (juce::jmin (17.0f, static_cast<float> (box.getHeight()) * 0.45f));
    }

    void ExniloLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        const int arrowSpace = box.getHeight() / 2 + 12;
        label.setBounds (arrowSpace / 2, 1, box.getWidth() - arrowSpace - arrowSpace / 2 + 8, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, palette::ink);
    }

    juce::PopupMenu::Options ExniloLookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
    {
        return juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label).withStandardItemHeight (28);
    }

    //==============================================================================
    void ExniloLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        g.fillAll (theme->tint (palette::surface, 0.08f));
        g.setColour (theme->tint (palette::line, 0.1f));
        g.drawRect (0, 0, width, height);
    }

    void ExniloLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                               bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                               const juce::String& shortcutKeyText, const juce::Drawable* icon,
                                               const juce::Colour* textColour)
    {
        if (isSeparator)
        {
            g.setColour (theme->tint (palette::line));
            g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
            return;
        }

        auto r = area.reduced (4, 1);
        if (isHighlighted && isActive)
        {
            g.setColour (theme->tint (palette::surface2, 0.15f));
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
        }

        if (isTicked)
        {
            g.setColour (theme->accentA);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ static_cast<float> (r.getX()) + 12.0f, static_cast<float> (r.getCentreY()) }));
        }

        auto textArea = r.withTrimmedLeft (26).withTrimmedRight (hasSubMenu ? 22 : 8);
        g.setFont (isTicked ? fonts::bold (15.0f) : fonts::regular (15.0f));
        g.setColour (textColour != nullptr ? *textColour : (isActive ? palette::ink : palette::dim.withAlpha (0.5f)));
        g.drawFittedText (text, textArea, juce::Justification::centredLeft, 1);

        if (shortcutKeyText.isNotEmpty())
        {
            g.setColour (palette::dim);
            g.setFont (fonts::regular (13.0f));
            g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight);
        }

        if (hasSubMenu)
        {
            juce::Path arrow;
            const float x = static_cast<float> (r.getRight()) - 14.0f, y = static_cast<float> (r.getCentreY());
            arrow.addTriangle (x, y - 4.0f, x, y + 4.0f, x + 5.0f, y);
            g.setColour (palette::dim);
            g.fillPath (arrow);
        }
        juce::ignoreUnused (icon);
    }

    void ExniloLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
    {
        g.setFont (fonts::bold (12.0f));
        g.setColour (theme->accentA);
        g.drawFittedText (name.toUpperCase(), area.reduced (12, 0), juce::Justification::bottomLeft, 1);
    }

    juce::Font ExniloLookAndFeel::getPopupMenuFont() { return fonts::regular (15.0f); }

    //==============================================================================
    void ExniloLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                  bool isMouseOverButton, bool isButtonDown)
    {
        auto area = button.getLocalBounds().toFloat().reduced (1.0f);
        const float radius = juce::jmin (area.getHeight() * 0.5f, static_cast<float> (button.getProperties().getWithDefault ("exnilo.radius", 1000.0)));
        const bool on = button.getToggleState();
        const bool accent = button.getProperties().getWithDefault ("exnilo.accent", false);

        if (on || accent)
        {
            g.setGradientFill (juce::ColourGradient (theme->accentA, area.getX(), 0.0f, theme->accentB, area.getRight(), 0.0f, false));
            g.fillRoundedRectangle (area, radius);
            if (isMouseOverButton || isButtonDown)
            {
                g.setColour (juce::Colours::white.withAlpha (isButtonDown ? 0.18f : 0.1f));
                g.fillRoundedRectangle (area, radius);
            }
            return;
        }

        g.setColour (theme->tint (palette::surface2, 0.08f).brighter (isButtonDown ? 0.15f : (isMouseOverButton ? 0.08f : 0.0f)));
        g.fillRoundedRectangle (area, radius);
        g.setColour (theme->tint (palette::line, 0.1f));
        g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
    }

    juce::Font ExniloLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return fonts::bold (juce::jmin (18.0f, static_cast<float> (buttonHeight) * 0.46f));
    }

    void ExniloLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
    {
        const bool on = button.getToggleState() || static_cast<bool> (button.getProperties().getWithDefault ("exnilo.accent", false));
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour ((on ? palette::onAccent : palette::ink).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
    }

    void ExniloLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isMouseOverButton, bool)
    {
        const auto bounds = button.getLocalBounds().toFloat();
        const float trackH = juce::jmin (22.0f, bounds.getHeight() - 4.0f);
        const float trackW = trackH * 1.75f;
        const auto track = juce::Rectangle<float> (bounds.getX() + 1.0f, bounds.getCentreY() - trackH * 0.5f, trackW, trackH);
        const bool on = button.getToggleState();
        const float alpha = button.isEnabled() ? 1.0f : 0.45f;

        if (on)
        {
            g.setColour (theme->accentA.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (track, trackH * 0.5f);
        }
        else
        {
            g.setColour (theme->tint (palette::bg2).withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (track, trackH * 0.5f);
            g.setColour (theme->tint (palette::line, 0.1f).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (track.reduced (0.5f), trackH * 0.5f, 1.0f);
        }

        const float thumb = trackH - 6.0f;
        const float thumbX = on ? track.getRight() - thumb - 3.0f : track.getX() + 3.0f;
        g.setColour ((on ? juce::Colour (0xfff4fffd) : palette::dim).withMultipliedAlpha (alpha * (isMouseOverButton ? 1.0f : 0.92f)));
        g.fillEllipse (thumbX, track.getY() + 3.0f, thumb, thumb);

        const auto textArea = bounds.withTrimmedLeft (trackW + 12.0f);
        g.setFont (on ? fonts::bold (juce::jmin (17.0f, bounds.getHeight() * 0.62f)) : fonts::regular (juce::jmin (17.0f, bounds.getHeight() * 0.62f)));
        g.setColour ((on ? palette::ink : palette::dim).withMultipliedAlpha (alpha));
        g.drawFittedText (button.getButtonText(), textArea.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    //==============================================================================
    void ExniloLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                              float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
    {
        const auto area = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (width), static_cast<float> (height)).reduced (4.0f);
        const float size = juce::jmin (area.getWidth(), area.getHeight());
        const auto centre = area.getCentre();
        const float ringR = size * 0.5f - 3.0f;
        const float thickness = juce::jmax (3.0f, size * 0.09f);
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (theme->tint (palette::bg2));
        g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (sliderPos > 0.001f)
            strokeGradientArc (g, centre, ringR, rotaryStartAngle, angle, rotaryEndAngle - rotaryStartAngle, thickness, *theme,
                               slider.isEnabled() ? 1.0f : 0.4f);

        const float bodyR = ringR - thickness - 2.0f;
        g.setGradientFill (juce::ColourGradient (theme->tint (palette::surface2, 0.12f).brighter (0.1f), centre.x, centre.y - bodyR,
                                                 theme->tint (palette::bg2), centre.x, centre.y + bodyR, false));
        g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre));

        const auto dot = centre.getPointOnCircumference (bodyR - thickness * 0.9f, angle);
        g.setColour (theme->at (sliderPos));
        g.fillEllipse (juce::Rectangle<float> (thickness * 1.1f, thickness * 1.1f).withCentre (dot));
    }

    void ExniloLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float,
                                              float, juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
            return;
        }

        const float h = 8.0f;
        const auto track = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y) + (static_cast<float> (height) - h) * 0.5f, static_cast<float> (width), h);
        g.setColour (theme->tint (palette::bg2));
        g.fillRoundedRectangle (track, h * 0.5f);

        const auto filled = track.withRight (sliderPos);
        g.setGradientFill (juce::ColourGradient (theme->accentA, track.getX(), 0.0f, theme->accentB, track.getRight(), 0.0f, false));
        g.fillRoundedRectangle (filled, h * 0.5f);

        // Marker at the preset's design value (100% of a 0-150% range sits at two thirds).
        if (slider.getProperties().getWithDefault ("exnilo.markUnity", false))
        {
            const float unityX = static_cast<float> (slider.getPositionOfValue (1.0));
            g.setColour (palette::ink.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (1.5f, h + 8.0f).withCentre ({ unityX, track.getCentreY() }));
        }

        g.setColour (palette::ink);
        g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ sliderPos, track.getCentreY() }));
        g.setColour (theme->at ((sliderPos - track.getX()) / juce::jmax (1.0f, track.getWidth())));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ sliderPos, track.getCentreY() }));
    }

    void ExniloLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
    {
        g.setColour (palette::dim.withAlpha (isMouseOver || isMouseDragging ? 0.9f : 0.45f));
        for (int i = 1; i <= 4; ++i)
        {
            const float o = static_cast<float> (i) * static_cast<float> (juce::jmin (w, h)) / 4.6f;
            g.drawLine (static_cast<float> (w) - o, static_cast<float> (h), static_cast<float> (w), static_cast<float> (h) - o, 1.3f);
        }
    }

    void ExniloLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        const auto area = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
        g.setColour (theme->tint (palette::surface, 0.08f));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (theme->tint (palette::line, 0.1f));
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (palette::ink);
        g.setFont (fonts::regular (14.0f));
        g.drawFittedText (text, area.reduced (10.0f, 4.0f).toNearestInt(), juce::Justification::centredLeft, 4);
    }

    juce::Rectangle<int> ExniloLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                              juce::Rectangle<int> parentArea)
    {
        const auto font = fonts::regular (14.0f);
        const int w = juce::jmin (320, juce::GlyphArrangement::getStringWidthInt (font, tipText) + 24);
        const int lines = juce::jmax (1, juce::GlyphArrangement::getStringWidthInt (font, tipText) / 296 + 1);
        const int h = 10 + lines * 18;
        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
            .constrainedWithin (parentArea);
    }
}
