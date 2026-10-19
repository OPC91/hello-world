#include "ExportHandle.h"
#include "Theme.h"

namespace bf::ui
{

ExportHandle::ExportHandle (EditorState& s) : state (s)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag the pattern into your DAW. Use the arrow to pick MIDI, audio mix or stems.");
}

ExportHandle::~ExportHandle()
{
    alive.reset();
}

juce::String ExportHandle::kindName() const
{
    switch (kind)
    {
        case Kind::Midi:       return "MIDI";
        case Kind::AudioMix:   return "Audio";
        case Kind::AudioStems: return "Stems";
    }

    return {};
}

void ExportHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool ready = ! files.isEmpty() && ! rendering;

    g.setColour (mouseIsDown ? Colours::accent.withAlpha (0.25f) : Colours::panelRaised);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (ready ? Colours::accent : Colours::outline);
    g.drawRoundedRectangle (r, 5.0f, 1.2f);

    auto content = getLocalBounds().reduced (6, 2);
    content.removeFromRight (14);

    // Grip dots: "this can be dragged".
    auto grip = content.removeFromLeft (10).toFloat();
    g.setColour (Colours::textDim);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 2; ++col)
            g.fillEllipse (grip.getX() + col * 5.0f, grip.getCentreY() - 6.0f + row * 5.0f, 2.5f, 2.5f);

    g.setColour (Colours::text);
    g.setFont (font (12.5f, true));
    g.drawFittedText (rendering ? "Rendering..." : "Drag " + kindName(), content.withTrimmedLeft (4), juce::Justification::centred, 1);

    juce::Path arrow;
    const auto m = menuArea().toFloat();
    arrow.addTriangle (m.getCentreX() - 4.0f, m.getCentreY() - 2.0f, m.getCentreX() + 4.0f, m.getCentreY() - 2.0f, m.getCentreX(), m.getCentreY() + 3.0f);
    g.setColour (Colours::textDim);
    g.fillPath (arrow);
}

void ExportHandle::showMenu()
{
    juce::PopupMenu menu, barsMenu;
    menu.addItem (1, "MIDI pattern (.mid)", true, kind == Kind::Midi);
    menu.addItem (2, "Audio - full mix (.wav)", true, kind == Kind::AudioMix);
    menu.addItem (3, "Audio - stems per track (.wav)", true, kind == Kind::AudioStems);

    for (int b : { 1, 2, 4, 8, 16 })
        barsMenu.addItem (10 + b, juce::String (b) + (b == 1 ? " bar" : " bars"), true, bars == b);

    menu.addSubMenu ("Length", barsMenu);
    menu.addSeparator();
    menu.addItem (4, "Reveal export folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r) {
        if (r == 0) return;
        if (r == 1) kind = Kind::Midi;
        if (r == 2) kind = Kind::AudioMix;
        if (r == 3) kind = Kind::AudioStems;
        if (r > 10) bars = r - 10;
        if (r == 4) Exporter::getExportFolder().getChildFile ("x").revealToUser();
        files.clear();
        repaint();
    });
}

void ExportHandle::startRender()
{
    rendering = true;
    files.clear();
    repaint();

    state.processor.exportPattern (kind, bars, [this, weak = std::weak_ptr<bool> (alive)] (juce::Array<juce::File> result, juce::String error) {
        if (weak.expired())
            return;

        rendering = false;
        files = result;
        repaint();

        if (error.isNotEmpty() && state.showStatus)
            state.showStatus (error);
        else if (! dragStarted && ! mouseIsDown && state.showStatus && ! files.isEmpty())
            state.showStatus ("Export ready: drag the handle into your DAW (" + files[0].getParentDirectory().getFullPathName() + ")");
    });
}

void ExportHandle::mouseDown (const juce::MouseEvent& e)
{
    if (menuArea().contains (e.getPosition()))
    {
        showMenu();
        return;
    }

    mouseIsDown = true;
    dragStarted = false;
    // Always render fresh, so the export matches what is playing now.
    startRender();
}

void ExportHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (! mouseIsDown || dragStarted || e.getDistanceFromDragStart() < 6 || rendering || files.isEmpty())
        return;

    dragStarted = true;
    juce::StringArray paths;

    for (const auto& f : files)
        paths.add (f.getFullPathName());

    juce::DragAndDropContainer::performExternalDragDropOfFiles (paths, false, this, [this] {
        mouseIsDown = false;
        repaint();
    });
}

void ExportHandle::mouseUp (const juce::MouseEvent&)
{
    mouseIsDown = false;
    repaint();
}

} // namespace bf::ui
