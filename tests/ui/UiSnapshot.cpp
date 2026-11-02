// Renders the SEAM editor offscreen to PNG files, in several themes, scales and states.
//
//   SeamUiSnapshot <output folder>
//
// A UI smoke test (the editor builds, lays out and paints at every size without asserting) and a way to review
// the UI without opening a window.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <iostream>

namespace
{
    bool writePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        return out.openedOk() && png.writeImageToStream (image, out);
    }

    void setParam (seam::SeamProcessor& p, const char* id, float plainValue)
    {
        auto* param = p.getState().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    struct Shot
    {
        const char* name;
        const char* theme;
        float scale;
        float transition;
        bool playing;
        double barsToSeam;
        bool advanced = false;
    };

    int fail (const juce::String& message)
    {
        std::cout << "FAILED: " << message << "\n";
        return 1;
    }

    /** Every factory preset loads, is reported unmodified, and a change marks it modified; state round-trips. */
    int presetChecks()
    {
        int failures = 0;
        seam::SeamProcessor processor;
        auto& store = processor.getPresets();
        int factoryCount = 0;
        for (size_t i = 0; i < store.getAll().size(); ++i)
        {
            const auto& preset = store.getAll()[i];
            if (! preset.isFactory)
                continue;
            ++factoryCount;
            processor.loadPreset (static_cast<int> (i));
            if (processor.getPresetName() != preset.name) failures += fail ("preset name not set: " + preset.name);
            if (! store.matches (preset, processor.getState())) failures += fail ("preset not matching after load: " + preset.name);
            const auto style = seam::choices::style[juce::roundToInt (processor.getState().getRawParameterValue (seam::ids::style)->load())];
            if (style != preset.category) failures += fail ("preset style mismatch: " + preset.name);
        }
        if (factoryCount != 12) failures += fail ("expected 12 factory presets, got " + juce::String (factoryCount));

        // First preset = the defaults.
        seam::SeamProcessor fresh;
        if (! fresh.getPresets().matches (fresh.getPresets().getAll().front(), fresh.getState()))
            failures += fail ("defaults differ from the first factory preset");

        // Modified detection.
        processor.loadPreset (3);
        auto* complexity = processor.getState().getParameter (seam::ids::complexity);
        complexity->setValueNotifyingHost (complexity->getValue() > 0.5f ? 0.1f : 0.9f);
        if (store.matches (store.getAll()[3], processor.getState())) failures += fail ("modified preset still matches");

        // State round trip keeps parameters, preset name, theme and the seed.
        processor.setThemeId ("techno");
        processor.newFill();
        processor.newFill();
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);
        seam::SeamProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        if (restored.getPresetName() != processor.getPresetName()) failures += fail ("preset name not restored");
        if (restored.getThemeId() != "techno") failures += fail ("theme not restored");
        if (restored.getFillWorker().getSeed() != processor.getFillWorker().getSeed()) failures += fail ("seed not restored");
        if (std::abs (restored.getState().getParameter (seam::ids::complexity)->getValue() - complexity->getValue()) > 1.0e-4f)
            failures += fail ("parameters not restored");
        std::cout << (failures == 0 ? "presets and state: OK\n" : "presets and state: FAILED\n");
        return failures;
    }
}

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: SeamUiSnapshot <output folder>\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    folder.createDirectory();

    const Shot shots[] {
        { "seam-dnb-100",   "drum-and-bass", 1.0f,  0.0f,   false, 0.0 },
        { "seam-dnb-live",  "drum-and-bass", 1.0f,  55.0f,  true,  2.75 },
        { "seam-dnb-075",   "drum-and-bass", 0.75f, 85.0f,  true,  0.6 },
        { "seam-dnb-200",   "drum-and-bass", 2.0f,  85.0f,  true,  0.6 },
        { "seam-classic",   "classic",       1.0f,  55.0f,  true,  6.2 },
        { "seam-techno",    "techno",        1.0f,  70.0f,  true,  1.5 },
        { "seam-trance",    "trance",        1.0f,  95.0f,  true,  0.3 },
        { "seam-dubstep",   "dubstep",       1.0f,  75.0f,  true,  3.1 },
        { "seam-house",     "house",         1.0f,  80.0f,  true,  9.0 },
        { "seam-futurebass","future-bass",   1.0f, 100.0f,  true,  0.9 },
        { "seam-advanced",  "drum-and-bass", 1.0f,  60.0f,  true,  5.0, true },
    };

    int failures = 0;
    for (const auto& shot : shots)
    {
        seam::SeamProcessor processor;
        processor.setThemeId (shot.theme);
        processor.setEditorScale (shot.scale);
        setParam (processor, seam::ids::transition, shot.transition);
        setParam (processor, seam::ids::fillLength, 1.0f);
        auto& ui = processor.getUiState();
        ui.playing = shot.playing;
        ui.barsToSeam = shot.barsToSeam;
        ui.phraseBars = 16;
        ui.barInPhrase = 16 - static_cast<int> (std::ceil (shot.barsToSeam));
        ui.effective = shot.transition / 100.0f;
        ui.beatPhase = 0.2f;
        ui.peakLeft = shot.playing ? 0.5f + shot.transition / 300.0f : 0.0f;
        ui.peakRight = ui.peakLeft.load() * 0.9f;

        // Wait for the fill worker to slice the built-in break.
        for (int wait = 0; wait < 200 && (processor.getFillWorker().getView().peaks.empty()
                                          || processor.getFillWorker().getView().fillDescription.isEmpty()); ++wait)
            juce::Thread::sleep (10);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        auto* seamEditor = dynamic_cast<seam::SeamEditor*> (editor.get());
        if (shot.advanced)
            seamEditor->setAdvancedVisible (true);
        seamEditor->advanceForSnapshot (1.5f);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = folder.getChildFile (juce::String (shot.name) + ".png");
        const bool ok = writePng (image, file) && image.getWidth() == juce::roundToInt (960.0f * shot.scale);
        std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";
        failures += ok ? 0 : 1;
    }
    failures += presetChecks();
    return failures == 0 ? 0 : 1;
}
