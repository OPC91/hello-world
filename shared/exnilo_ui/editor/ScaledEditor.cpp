namespace exnilo
{
    ScaledEditor::ScaledEditor (juce::AudioProcessor& owner, int w, int h, float initialScale)
        : juce::AudioProcessorEditor (owner), baseWidth (w), baseHeight (h)
    {
        setLookAndFeel (&lookAndFeel);
        tooltips.setLookAndFeel (&lookAndFeel);
        addAndMakeVisible (content);
        content.setBounds (0, 0, baseWidth, baseHeight);

        setResizable (true, true);
        setResizeLimits (juce::roundToInt (static_cast<float> (baseWidth) * minScale), juce::roundToInt (static_cast<float> (baseHeight) * minScale),
                         juce::roundToInt (static_cast<float> (baseWidth) * maxScale), juce::roundToInt (static_cast<float> (baseHeight) * maxScale));
        if (auto* c = getConstrainer())
            c->setFixedAspectRatio (static_cast<double> (baseWidth) / static_cast<double> (baseHeight));

        scale = juce::jlimit (minScale, maxScale, initialScale);
        setSize (juce::roundToInt (static_cast<float> (baseWidth) * scale), juce::roundToInt (static_cast<float> (baseHeight) * scale));
    }

    ScaledEditor::~ScaledEditor()
    {
        tooltips.setLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    void ScaledEditor::setScale (float newScale)
    {
        newScale = juce::jlimit (minScale, maxScale, newScale);
        setSize (juce::roundToInt (static_cast<float> (baseWidth) * newScale), juce::roundToInt (static_cast<float> (baseHeight) * newScale));
    }

    void ScaledEditor::resized()
    {
        const float newScale = juce::jlimit (minScale, maxScale, static_cast<float> (getWidth()) / static_cast<float> (baseWidth));
        content.setTransform (juce::AffineTransform::scale (newScale));
        content.setBounds (0, 0, baseWidth, baseHeight);

        if (std::abs (newScale - scale) > 0.0001f)
        {
            scale = newScale;
            if (onScaleChanged)
                onScaleChanged (scale);
        }

        // Keep the corner grip on top of the scaled content.
        if (resizableCorner != nullptr)
            resizableCorner->toFront (false);
    }

    void ScaledEditor::paint (juce::Graphics& g)
    {
        const auto& t = lookAndFeel.getTheme();
        const auto area = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (t.tint (palette::bg, 0.05f), area.getCentreX(), area.getY(),
                                                 t.tint (palette::bg, 0.02f).darker (0.15f), area.getCentreX(), area.getBottom(), false));
        g.fillAll();

        // Faint dot grid, as in Nil2Max's background.
        g.setColour (t.tint (palette::line, 0.05f).withAlpha (0.22f));
        const float step = 24.0f * scale;
        for (float y = step * 0.5f; y < area.getBottom(); y += step)
            for (float x = step * 0.5f; x < area.getRight(); x += step)
                g.fillRect (x, y, 1.2f, 1.2f);
    }
}
