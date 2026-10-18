// Renders the editor offscreen to PNG files for visual review.
// Hidden from normal runs; use:  BenchmarkTests "[.snapshot]"
// Output goes to $BENCHMARK_SNAPSHOT_DIR (default: the system temp folder).

#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    juce::File snapshotDir()
    {
        const auto env = juce::SystemStats::getEnvironmentVariable ("BENCHMARK_SNAPSHOT_DIR", {});
        auto dir = env.isNotEmpty() ? juce::File (env) : juce::File::getSpecialLocation (juce::File::tempDirectory);
        dir.createDirectory();
        return dir;
    }

    void saveSnapshot (juce::Component& c, const juce::String& name)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        const auto file = snapshotDir().getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        juce::Logger::writeToLog ("Snapshot: " + file.getFullPathName());
    }
}

TEST_CASE ("Editor snapshots", "[.snapshot]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    testing::TempDir temp;
    constexpr double sr = 48000.0;

    benchmark::BenchmarkProcessor processor;
    processor.prepareToPlay (sr, 512);

    {
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor.createEditor());
        saveSnapshot (*editor, "benchmark-empty");
    }

    // Some references, one missing.
    juce::Array<juce::File> files;
    for (const auto& [name, level] : { std::pair { "Radio Single (Master)", -9.0 }, { "Club Mix 2024", -7.5 },
                                       { "Acoustic Ballad", -14.0 }, { "Lo-fi Beat", -12.0 } })
    {
        const auto f = temp / (juce::String (name) + ".wav");
        testing::writeWav (f, testing::makeSine (sr, 4.0, 220.0, level + 3.0), sr);
        files.add (f);
    }

    auto& library = processor.getReferenceLibrary();
    library.addFiles (files);
    REQUIRE (library.waitUntilLoaded (20000));

    auto state = library.getStateSnapshot();
    juce::ValueTree missing ("Reference");
    missing.setProperty ("path", (temp / "Moved Track.wav").getFullPathName(), nullptr);
    missing.setProperty ("name", "Moved Track", nullptr);
    state.appendChild (missing, nullptr);
    state.setProperty ("active", 1, nullptr);
    library.restoreState (state);
    REQUIRE (library.waitUntilLoaded (20000));

    // Feed some mix so the meters have values.
    processor.getValueTreeState().getParameter (benchmark::ParamIDs::listenRef)->setValueNotifyingHost (1.0f);
    auto mix = testing::makeSine (sr, 6.0, 330.0, -11.0);
    juce::MidiBuffer midi;
    for (int start = 0; start < mix.getNumSamples(); start += 512)
    {
        juce::AudioBuffer<float> view (mix.getArrayOfWritePointers(), 2, start, std::min (512, mix.getNumSamples() - start));
        processor.processBlock (view, midi);
    }

    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor.createEditor());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (600); // let timers and animations settle
    saveSnapshot (*editor, "benchmark-loaded");

    editor->setSize (800, 512);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    saveSnapshot (*editor, "benchmark-small");
}
