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
    };
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
        seamEditor->advanceForSnapshot (1.5f);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = folder.getChildFile (juce::String (shot.name) + ".png");
        const bool ok = writePng (image, file) && image.getWidth() == juce::roundToInt (960.0f * shot.scale);
        std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";
        failures += ok ? 0 : 1;
    }
    return failures == 0 ? 0 : 1;
}
