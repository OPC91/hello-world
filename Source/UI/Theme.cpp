#include "Theme.h"

namespace bf::ui
{

juce::Colour Colours::track (int index)
{
    static const juce::Colour hues[] = {
        juce::Colour (0xffff6a3d), // kick
        juce::Colour (0xffffc23d), // snare
        juce::Colour (0xffc79a5a), // ghost
        juce::Colour (0xff35c6d6), // closed hat
        juce::Colour (0xff3ad6a2), // open hat
        juce::Colour (0xff5b8cff), // ride
        juce::Colour (0xffb072ff), // perc
        juce::Colour (0xffff5fa8), // fx
    };

    return hues[(size_t) juce::jlimit (0, kNumTracks - 1, index)];
}

juce::Colour Colours::forVelocity (int trackIndex, int velocity)
{
    const float v = juce::jlimit (0.0f, 1.0f, (float) velocity / 127.0f);
    return cellOff.interpolatedWith (track (trackIndex), 0.25f + 0.75f * v).brighter (v > 0.9f ? 0.15f : 0.0f);
}

juce::Font font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::TextButton::buttonColourId, Colours::panelRaised);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ComboBox::backgroundColourId, Colours::panelRaised);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::outline);
    setColour (juce::ComboBox::arrowColourId, Colours::textDim);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panelRaised);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.8f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Label::textWhenEditingColourId, Colours::text);
    setColour (juce::Label::backgroundWhenEditingColourId, Colours::panel);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::thumbColourId, Colours::accent);
    setColour (juce::Slider::trackColourId, Colours::accent);
    setColour (juce::Slider::backgroundColourId, Colours::cellOff);
    setColour (juce::BubbleComponent::backgroundColourId, Colours::panelRaised);
    setColour (juce::BubbleComponent::outlineColourId, Colours::outline);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panelRaised);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::CaretComponent::caretColourId, Colours::accent);
    setColour (juce::TextEditor::backgroundColourId, Colours::panel);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::outlineColourId, Colours::outline);
    setColour (juce::AlertWindow::backgroundColourId, Colours::panelRaised);
    setColour (juce::AlertWindow::textColourId, Colours::text);
    setColour (juce::ToggleButton::textColourId, Colours::text);
    setColour (juce::ToggleButton::tickColourId, Colours::accent);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (2.5f, radius * 0.16f);
    const float arcR = radius - lineW * 0.5f;
    const auto colour = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float angle = start + pos * (end - start);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (Colours::cellOff);
    g.strokePath (track, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    // Bipolar ranges (pan, fine, nudge...) fill from the centre.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.isEnabled() ? colour : Colours::textDim);
    g.strokePath (value, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    g.setColour (Colours::panelRaised);
    g.fillEllipse (juce::Rectangle<float> (radius * 1.15f, radius * 1.15f).withCentre (centre));

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -radius * 0.55f, 3.0f, radius * 0.4f, 1.5f);
    g.setColour (Colours::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto c = on ? b.findColour (juce::TextButton::buttonOnColourId) : bg;

    if (down)             c = c.brighter (0.2f);
    else if (highlighted) c = c.brighter (0.08f);
    if (! b.isEnabled())  c = c.withMultipliedAlpha (0.4f);

    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? c.brighter (0.2f) : Colours::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    g.setColour ((on ? b.findColour (juce::TextButton::textColourOnId) : b.findColour (juce::TextButton::textColourOffId))
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 1), juce::Justification::centred, 1, 0.8f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? Colours::accent.withAlpha (0.6f) : Colours::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (Colours::textDim);
    g.fillPath (arrow);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const auto box = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.22f);

    g.setColour (b.getToggleState() ? Colours::accent : Colours::cellOff.brighter (highlighted ? 0.1f : 0.0f));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (box, 3.0f, 1.0f);

    g.setColour (Colours::text.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.setFont (font (13.0f));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (4), juce::Justification::centredLeft, 1);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (Colours::panelRaised);
    g.setColour (Colours::outline);
    g.drawRect (0, 0, w, h);
    g.setColour (Colours::text);
    g.setFont (font (13.0f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (6, 3), juce::Justification::centredLeft, 4);
}

// -----------------------------------------------------------------------------

Knob::Knob (const juce::String& n, juce::Colour colour) : name (n)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (14);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (Colours::textDim);
    g.setFont (font (11.5f));
    g.drawFittedText (name, getLocalBounds().removeFromBottom (14), juce::Justification::centred, 1, 0.7f);
}

} // namespace bf::ui
