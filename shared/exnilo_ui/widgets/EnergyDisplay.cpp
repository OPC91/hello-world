namespace exnilo
{
    EnergyDisplay::EnergyDisplay()
    {
        setInterceptsMouseClicks (false, false);
        for (auto& d : drops)
        {
            d.x = rng.nextFloat();
            d.y = rng.nextFloat();
            d.speed = 0.6f + rng.nextFloat() * 0.9f;
            d.length = 0.04f + rng.nextFloat() * 0.12f;
            d.alpha = 0.25f + rng.nextFloat() * 0.6f;
        }
        for (auto& m : motes)
        {
            m.x = rng.nextFloat();
            m.y = rng.nextFloat();
            m.vx = (rng.nextFloat() - 0.5f) * 0.02f;
            m.vy = (rng.nextFloat() - 0.5f) * 0.02f;
            m.phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        }
        setAnimating (true);
    }

    void EnergyDisplay::setAnimating (bool shouldAnimate)
    {
        if (shouldAnimate && vblank == nullptr)
        {
            lastFrame = juce::Time::getMillisecondCounterHiRes();
            vblank = std::make_unique<juce::VBlankAttachment> (this, [this]
            {
                const double now = juce::Time::getMillisecondCounterHiRes();
                const float dt = static_cast<float> (juce::jlimit (0.0, 0.1, (now - lastFrame) * 0.001));
                lastFrame = now;
                advance (dt);
            });
        }
        else if (! shouldAnimate)
        {
            vblank.reset();
        }
    }

    void EnergyDisplay::advance (float dt)
    {
        time += dt;
        energy += (targetEnergy - energy) * (1.0f - std::exp (-dt * 8.0f));

        // Rings fire faster as the energy climbs.
        if (energy > 0.02f)
        {
            spawnClock += dt;
            const float interval = juce::jmap (energy, 1.3f, 0.14f);
            if (spawnClock >= interval)
            {
                spawnClock = 0.0f;
                for (auto& r : rings)
                {
                    if (! r.active)
                    {
                        r = { 0.0f, juce::jmap (energy, 1.8f, 0.9f), rng.nextFloat() * 0.6f - 0.3f, true };
                        break;
                    }
                }
            }
        }

        for (auto& r : rings)
            if (r.active && (r.age += dt) >= r.life)
                r.active = false;

        for (auto& d : drops)
        {
            d.y += d.speed * dt * (0.25f + 1.6f * energy);
            if (d.y - d.length > 1.0f)
            {
                d.y = -rng.nextFloat() * 0.2f;
                d.x = rng.nextFloat();
            }
        }

        for (auto& m : motes)
        {
            m.x = std::fmod (m.x + m.vx * dt * (1.0f + 4.0f * energy) + 1.0f, 1.0f);
            m.y = std::fmod (m.y + m.vy * dt * (1.0f + 4.0f * energy) + 1.0f, 1.0f);
        }

        repaint();
    }

    void EnergyDisplay::addShape (juce::Path& p, RingShape shape, juce::Point<float> c, float r, float rotation)
    {
        int sides = 0;
        float offset = 0.0f;
        switch (shape)
        {
            case RingShape::square:   sides = 4; offset = juce::MathConstants<float>::pi / 4.0f; break;
            case RingShape::triangle: sides = 3; offset = 0.0f; break;
            case RingShape::hexagon:  sides = 6; offset = 0.0f; break;
            case RingShape::circle:
            case RingShape::streaks:
            case RingShape::sparkles: break;
        }

        if (sides == 0)
        {
            p.addEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
            return;
        }

        for (int i = 0; i <= sides; ++i)
        {
            const float a = offset + rotation + juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (sides);
            const auto pt = c.getPointOnCircumference (r, a);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        p.closeSubPath();
    }

    void EnergyDisplay::paint (juce::Graphics& g)
    {
        const auto& t = themeOf (*this);
        const auto area = getLocalBounds().toFloat();
        const float radius = 18.0f;

        juce::Path clip;
        clip.addRoundedRectangle (area, radius);
        g.saveState();
        g.reduceClipRegion (clip);

        // Background.
        g.setGradientFill (juce::ColourGradient (t.tint (palette::surface, 0.07f + 0.08f * energy), area.getCentreX(), area.getCentreY(),
                                                 t.tint (palette::bg2, 0.04f), area.getX(), area.getY(), true));
        g.fillRect (area);

        const auto centre = area.getCentre().translated (0.0f, -area.getHeight() * 0.06f);
        const float extent = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;

        // Rain / rising streaks.
        if (t.rain || t.rings == RingShape::streaks)
        {
            const bool rising = ! t.rain;
            const int count = juce::roundToInt (static_cast<float> (drops.size()) * (0.08f + 0.92f * energy));
            for (int i = 0; i < count; ++i)
            {
                const auto& d = drops[static_cast<size_t> (i)];
                const float x = area.getX() + d.x * area.getWidth();
                const float yNorm = rising ? 1.0f - d.y : d.y;
                const float y0 = area.getY() + yNorm * area.getHeight();
                const float len = d.length * area.getHeight() * (0.6f + energy);
                g.setColour ((i % 3 == 0 ? t.accentA : t.accentB).withAlpha (d.alpha * (0.15f + 0.55f * energy)));
                g.drawLine (x, y0, x, rising ? y0 + len : y0 - len, 1.6f);
            }
        }

        // Static outline shape framing the core, appearing with the energy.
        if (energy > 0.01f && t.rings != RingShape::circle && t.rings != RingShape::streaks && t.rings != RingShape::sparkles)
        {
            juce::Path frame;
            addShape (frame, t.rings, centre, extent * 1.05f, time * 0.05f * energy);
            g.setColour (t.accentB.withAlpha (0.18f + 0.5f * energy));
            g.strokePath (frame, juce::PathStrokeType (3.0f + 2.0f * energy));
        }

        // Rings.
        for (const auto& r : rings)
        {
            if (! r.active)
                continue;
            const float p = r.age / r.life;
            const float rr = extent * (0.22f + 0.95f * p);
            juce::Path ring;
            addShape (ring, t.rings == RingShape::streaks || t.rings == RingShape::sparkles ? RingShape::circle : t.rings,
                      centre, rr, r.spin * p * 2.0f);
            g.setColour (t.at (p).withAlpha ((1.0f - p) * (0.35f + 0.55f * energy)));
            g.strokePath (ring, juce::PathStrokeType (2.0f + 2.5f * (1.0f - p)));
        }

        // Motes drifting in the background (always a few, like a quiet room).
        for (size_t i = 0; i < motes.size(); ++i)
        {
            const auto& m = motes[i];
            const auto pt = juce::Point<float> (area.getX() + m.x * area.getWidth(), area.getY() + m.y * area.getHeight());
            const float twinkle = 0.5f + 0.5f * std::sin (time * 2.0f + m.phase);
            const float s = t.rings == RingShape::sparkles ? 2.0f + 5.0f * energy * twinkle : 3.0f;
            g.setColour ((i % 2 == 0 ? t.accentA : t.accentB).withAlpha (0.35f + 0.4f * twinkle * (0.3f + energy)));
            if (t.rings == RingShape::sparkles && energy > 0.05f)
            {
                g.fillRect (juce::Rectangle<float> (s * 2.0f, 1.5f).withCentre (pt));
                g.fillRect (juce::Rectangle<float> (1.5f, s * 2.0f).withCentre (pt));
            }
            else
            {
                g.fillEllipse (juce::Rectangle<float> (s, s).withCentre (pt));
            }
        }

        // Core: swells with energy, pulses on the beat.
        const float pulse = beatPhase >= 0.0f ? std::pow (1.0f - beatPhase, 3.0f) * energy : 0.0f;
        const float coreR = extent * (0.16f + 0.2f * energy) * (1.0f + 0.08f * pulse);
        {
            const float glowR = coreR * (2.2f + 1.2f * energy);
            juce::ColourGradient glow (t.accentB.withAlpha (0.35f + 0.4f * energy), centre.x, centre.y,
                                       t.accentB.withAlpha (0.0f), centre.x + glowR, centre.y, true);
            g.setGradientFill (glow);
            g.fillEllipse (juce::Rectangle<float> (glowR * 2.0f, glowR * 2.0f).withCentre (centre));

            juce::ColourGradient core (juce::Colours::white.interpolatedWith (t.accentB, 0.15f), centre.x - coreR * 0.2f, centre.y - coreR * 0.2f,
                                       t.accentB.interpolatedWith (juce::Colours::white, 0.25f), centre.x + coreR, centre.y, true);
            g.setGradientFill (core);
            g.fillEllipse (juce::Rectangle<float> (coreR * 2.0f, coreR * 2.0f).withCentre (centre));

            // Orbiting arcs around the core.
            const float arcR = coreR * 1.45f;
            g.setColour (t.accentB.withAlpha (0.45f + 0.45f * energy));
            for (int i = 0; i < 2; ++i)
            {
                const float a = time * (0.6f + 2.5f * energy) + static_cast<float> (i) * juce::MathConstants<float>::pi;
                juce::Path arc;
                arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, a, a + 1.1f + energy, true);
                g.strokePath (arc, juce::PathStrokeType (2.0f + 2.5f * energy, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        if (overlay)
            overlay (g, area, t);

        g.restoreState();
        g.setColour (t.tint (palette::line, 0.08f).withAlpha (0.55f));
        g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
    }
}
