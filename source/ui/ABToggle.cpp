#include "ABToggle.h"
#include "Theme.h"

namespace benchmark::ui
{
    ABToggle::ABToggle (juce::RangedAudioParameter& listenReferenceParam)
        : param (listenReferenceParam),
          attachment (listenReferenceParam, [this] (float v) { parameterChanged (v); })
    {
        setTitle ("Mix / Reference");
        setDescription ("Switches the output between your mix and the active reference track. Shortcut: space bar.");
        attachment.sendInitialUpdate();
        highlightPosition = isReference ? 1.0f : 0.0f;
    }

    ABToggle::~ABToggle()
    {
        stopTimer();
    }

    void ABToggle::toggle()
    {
        select (! isReference);
    }

    void ABToggle::setReferenceName (const juce::String& name)
    {
        if (name != referenceName)
        {
            referenceName = name;
            repaint();
        }
    }

    void ABToggle::select (bool reference)
    {
        attachment.setValueAsCompleteGesture (reference ? 1.0f : 0.0f);
    }

    void ABToggle::parameterChanged (float newValue)
    {
        isReference = newValue >= 0.5f;

        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);

        startTimerHz (60);
    }

    void ABToggle::timerCallback()
    {
        const auto target = isReference ? 1.0f : 0.0f;
        highlightPosition += (target - highlightPosition) * 0.35f;

        if (std::abs (target - highlightPosition) < 0.002f)
        {
            highlightPosition = target;
            stopTimer();
        }

        repaint();
    }

    void ABToggle::mouseDown (const juce::MouseEvent& e)
    {
        select (e.position.x >= (float) getWidth() * 0.5f);
    }

    void ABToggle::paint (juce::Graphics& g)
    {
        auto& theme = getTheme (*this);
        const auto bounds = getLocalBounds().toFloat();
        const auto corner = theme.panelCorner();
        paintPanel (g, bounds, theme);

        // Sliding highlight, coloured by blending between the two source colours.
        const auto inset = theme.px (4.0f);
        auto half = bounds.reduced (inset).withWidth ((bounds.getWidth() - inset * 2.0f) * 0.5f);
        half.setX (bounds.getX() + inset + half.getWidth() * highlightPosition);

        const auto accent = Colours::mix.interpolatedWith (Colours::reference, highlightPosition);
        g.setColour (accent.withAlpha (0.18f));
        g.fillRoundedRectangle (half, corner * 0.8f);
        g.setColour (accent);
        g.drawRoundedRectangle (half, corner * 0.8f, theme.px (1.5f));

        const auto drawLabel = [&] (juce::Rectangle<float> area, const juce::String& text, const juce::String& sub,
                                    juce::Colour colour, float weight)
        {
            const auto textColour = Colours::textMuted.interpolatedWith (colour, weight);
            g.setColour (textColour);
            g.setFont (theme.font (22.0f, true).withExtraKerningFactor (0.12f));
            g.drawText (text, area.withTrimmedBottom (area.getHeight() * 0.3f), juce::Justification::centredBottom);
            g.setColour (textColour.withMultipliedAlpha (0.7f));
            g.setFont (theme.font (11.0f));
            g.drawText (sub, area.withTrimmedTop (area.getHeight() * 0.7f).reduced (theme.px (10.0f), 0.0f),
                        juce::Justification::centredTop, true);
        };

        auto left = bounds.withWidth (bounds.getWidth() * 0.5f);
        auto right = left.withX (bounds.getCentreX());
        drawLabel (left, "MIX", "host input", Colours::mix, 1.0f - highlightPosition);
        drawLabel (right, "REFERENCE", referenceName.isNotEmpty() ? referenceName : juce::String ("no reference ready"),
                   Colours::reference, highlightPosition);
    }

    std::unique_ptr<juce::AccessibilityHandler> ABToggle::createAccessibilityHandler()
    {
        struct ValueInterface final : juce::AccessibilityTextValueInterface
        {
            explicit ValueInterface (ABToggle& t) : owner (t) {}
            bool isReadOnly() const override { return true; }
            juce::String getCurrentValueAsString() const override { return owner.isReference ? "Reference" : "Mix"; }
            void setValueAsString (const juce::String&) override {}
            ABToggle& owner;
        };

        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::toggleButton,
            juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press, [this] { toggle(); }),
            juce::AccessibilityHandler::Interfaces { std::make_unique<ValueInterface> (*this) });
    }
}
