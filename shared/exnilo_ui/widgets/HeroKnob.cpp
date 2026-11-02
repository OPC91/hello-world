namespace exnilo
{
    HeroKnob::HeroKnob (juce::String labelText)
        : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
          label (std::move (labelText))
    {
        setRotaryParameters (startAngle + juce::MathConstants<float>::twoPi, endAngle + juce::MathConstants<float>::twoPi, true);
        setMouseDragSensitivity (300);
        setVelocityModeParameters (1.0, 1, 0.0, false, juce::ModifierKeys::ctrlAltCommandModifiers);
        setPopupDisplayEnabled (false, false, nullptr);
    }

    void HeroKnob::setLiveProportion (float proportion)
    {
        if (std::abs (proportion - liveProportion) > 0.002f)
        {
            liveProportion = proportion;
            repaint();
        }
    }

    void HeroKnob::paint (juce::Graphics& g)
    {
        const auto& t = themeOf (*this);
        const auto bounds = getLocalBounds().toFloat();
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto centre = bounds.getCentre();
        const float value = static_cast<float> (valueToProportionOfLength (getValue()));
        const float shown = juce::jmax (value, liveProportion);
        const float sweep = endAngle - startAngle;

        const float tickOuter = size * 0.5f - 2.0f;
        const float ringR = size * 0.385f;
        const float thickness = size * 0.085f;
        const float bodyR = size * 0.29f;

        // Glow behind the knob, growing with the value.
        if (shown > 0.0f)
        {
            const float glowR = size * (0.42f + 0.1f * shown);
            juce::ColourGradient glow (t.accentB.withAlpha (0.10f + 0.32f * shown), centre.x, centre.y,
                                       t.accentB.withAlpha (0.0f), centre.x + glowR, centre.y, true);
            glow.addColour (0.55, t.at (0.6f).withAlpha (0.06f + 0.16f * shown));
            g.setGradientFill (glow);
            g.fillEllipse (juce::Rectangle<float> (glowR * 2.0f, glowR * 2.0f).withCentre (centre));
        }

        // Tick marks.
        constexpr int ticks = 21;
        for (int i = 0; i < ticks; ++i)
        {
            const float p = static_cast<float> (i) / static_cast<float> (ticks - 1);
            const float angle = startAngle + p * sweep;
            const bool major = (i == 0 || i == ticks - 1 || i == ticks / 2);
            const float len = major ? size * 0.035f : size * 0.02f;
            const auto a = centre.getPointOnCircumference (tickOuter - len, angle);
            const auto b = centre.getPointOnCircumference (tickOuter, angle);
            const bool lit = p <= shown + 0.001f && shown > 0.0f;
            g.setColour (lit ? t.at (p) : t.tint (palette::line, 0.15f));
            g.drawLine ({ a, b }, major ? 2.6f : 2.0f);
        }

        // Track.
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, startAngle, endAngle, true);
        g.setColour (t.tint (palette::bg2, 0.04f).darker (0.2f));
        g.strokePath (track, juce::PathStrokeType (thickness + 10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (t.tint (palette::surface, 0.06f).darker (0.35f));
        g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Live (automatic) value: a soft arc under the main one.
        if (liveProportion > value + 0.002f)
            ExniloLookAndFeel::strokeGradientArc (g, centre, ringR, startAngle, startAngle + liveProportion * sweep, sweep,
                                                  thickness, t, 0.35f);

        // Value arc with a glow pass underneath.
        if (value > 0.0005f)
        {
            const float valueAngle = startAngle + value * sweep;
            ExniloLookAndFeel::strokeGradientArc (g, centre, ringR, startAngle, valueAngle, sweep, thickness + 9.0f, t, 0.18f);
            ExniloLookAndFeel::strokeGradientArc (g, centre, ringR, startAngle, valueAngle, sweep, thickness, t, 1.0f);
        }

        // Body.
        const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (t.tint (palette::surface2, 0.16f).brighter (0.08f), centre.x, body.getY(),
                                                 t.tint (palette::bg2, 0.08f), centre.x, body.getBottom(), false));
        g.fillEllipse (body);
        g.setColour (t.at (value).withAlpha (0.55f + 0.4f * value));
        g.drawEllipse (body.reduced (1.0f), 2.0f);

        // Indicator dot.
        const float angle = startAngle + value * sweep;
        const auto dot = centre.getPointOnCircumference (bodyR + (ringR - bodyR) * 0.32f, angle);
        g.setColour (t.at (value));
        g.fillEllipse (juce::Rectangle<float> (size * 0.04f, size * 0.04f).withCentre (dot));

        // Readout.
        const auto text = juce::String (juce::roundToInt (value * 100.0f)) + "%";
        g.setColour (palette::ink);
        g.setFont (fonts::heavy (size * 0.17f));
        g.drawText (text, body.withTrimmedBottom (bodyR * 0.25f).toNearestInt(), juce::Justification::centred, false);

        g.setColour (palette::dim);
        g.setFont (fonts::bold (size * 0.052f));
        g.drawText (label, body.withTrimmedTop (bodyR * 1.15f).withHeight (size * 0.08f).toNearestInt(), juce::Justification::centred, false);

        if (! isEnabled())
        {
            g.setColour (t.tint (palette::bg).withAlpha (0.5f));
            g.fillEllipse (bounds.withSizeKeepingCentre (size, size));
        }
    }
}
