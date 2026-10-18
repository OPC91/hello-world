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

    /** Noise with a song-like loudness envelope, so waveforms look like music. */
    juce::AudioBuffer<float> makeMusicLike (double sr, double seconds, double levelDb, int seed,
                                            float brightness = 0.2f, float width = 0.1f)
    {
        juce::Random rng (seed);
        const auto n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        const auto gain = (float) std::pow (10.0, levelDb / 20.0);
        float lp = 0.0f, lpSide = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const auto t = i / sr;
            const auto section = 0.55 + 0.45 * std::sin (t * 0.35 + seed);         // verses / choruses
            const auto beat = 0.6 + 0.4 * std::pow (std::abs (std::sin (t * 3.9)), 6.0); // pulses
            lp += brightness * ((rng.nextFloat() * 2.0f - 1.0f) - lp);
            lpSide += brightness * ((rng.nextFloat() * 2.0f - 1.0f) - lpSide);
            const auto env = gain * (float) (section * beat) * 3.0f;
            b.setSample (0, i, env * (lp + width * lpSide));
            b.setSample (1, i, env * (lp - width * lpSide));
        }

        return b;
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
        testing::writeWav (f, makeMusicLike (sr, 30.0, level + 6.0, files.size() + 1, 0.15f, 0.45f), sr);
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

    // A loop and two markers on the active reference.
    library.setLoop (1, benchmark::LoopRegion { 8.0, 14.5 });
    library.setMarker (1, 0, benchmark::LoopRegion { 8.0, 14.5 });
    library.setMarker (1, 2, benchmark::LoopRegion { 20.0, 26.0 });
    processor.getTransport().requestSeek ((int64_t) (10.2 * sr));

    // Feed some mix so the meters have values.
    processor.getValueTreeState().getParameter (benchmark::ParamIDs::listenRef)->setValueNotifyingHost (1.0f);
    auto mix = makeMusicLike (sr, 12.0, -8.0, 99, 0.3f, 0.2f); // brighter and narrower than the references
    juce::MidiBuffer midi;
    for (int start = 0; start < mix.getNumSamples(); start += 512)
    {
        juce::AudioBuffer<float> view (mix.getArrayOfWritePointers(), 2, start, std::min (512, mix.getNumSamples() - start));
        processor.processBlock (view, midi);
    }

    processor.getAnalysis().processPending();

    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor.createEditor());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (600); // let timers and animations settle
    saveSnapshot (*editor, "benchmark-loaded");

    for (auto* child : editor->getChildren())
    {
        if (auto* panel = dynamic_cast<benchmark::ui::AnalysisPanel*> (child))
        {
            panel->setTab (benchmark::ui::AnalysisPanel::Tab::stereo);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            saveSnapshot (*editor, "benchmark-stereo");
            panel->setTab (benchmark::ui::AnalysisPanel::Tab::dynamics);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            saveSnapshot (*editor, "benchmark-dynamics");
            panel->setTab (benchmark::ui::AnalysisPanel::Tab::tonal);
        }
    }

    editor->setSize (800, 512);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    saveSnapshot (*editor, "benchmark-small");
}
