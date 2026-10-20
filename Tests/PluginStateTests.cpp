#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Plugin/PluginEditor.h"
#include "Plugin/PluginProcessor.h"
#include "Core/StateIO.h"

namespace
{
    /** A host playhead that reports a running transport. */
    struct FakePlayHead final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (true);
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setTimeSignature (TimeSignature { 4, 4 });
            return info;
        }

        double bpm = 174.0, ppq = 0.0;
    };

    void pump (int ms)
    {
        // Let async loads, timers and callAsync messages run.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }
}

TEST_CASE ("Full plugin state survives save and restore", "[state][plugin]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tmp = juce::File::createTempFile ("bf_state");
    tmp.createDirectory();

    juce::MemoryBlock saved;
    bf::PatternBank bankBefore;

    {
        BreakForgeProcessor a;
        a.setRateAndBufferSizeDetails (48000.0, 256);
        a.prepareToPlay (48000.0, 256);

        // Parameters
        a.setParameterValue (bf::ParamID::swing, 61.5f);
        a.setParameterValue (bf::ParamID::humanizeTime, 7.0f);
        a.setParameterValue (bf::ParamID::subStyle, 3.0f);
        a.setParameterValue (bf::ParamID::track (4, bf::ParamID::pitch), -5.0f);
        a.setParameterValue (bf::ParamID::track (2, bf::ParamID::mute), 1.0f);

        // Patterns, groove, MIDI notes, seed
        a.editModel ([] (bf::SequencerSnapshot& m) {
            m.bank.patterns[9].tracks[3].steps[7].on = true;
            m.bank.patterns[9].tracks[3].steps[7].ratchet = 3;
            m.bank.patterns[9].length = 32;
            m.userGroove.timing[3] = 0.2f;
            m.midiNotes[6] = 77;
            m.humanizeSeed = 4242;
        });

        // Track settings and a user sample (plus a missing one, which must keep its path)
        a.getTrackSettings()[1].name = "Amen Snare";
        a.getTrackSettings()[0].generatorLocked = true;
        a.getTrackSettings()[5].sampleLocked = true;

        juce::AudioBuffer<float> click (1, 100);
        click.clear();
        click.setSample (0, 0, 1.0f);
        const auto wavFile = tmp.getChildFile ("my kick.wav");
        REQUIRE (bf::Exporter::writeWav (wavFile, click, 44100.0));
        a.loadSample (0, bf::SampleRef::fromFile (wavFile));
        a.loadSample (7, bf::SampleRef::fromFile (tmp.getChildFile ("missing.wav")));
        REQUIRE (a.getSampleManager().waitAndApply (10000));
        REQUIRE (a.getSampleManager().hasWarning (7));

        a.getStateInformation (saved);
        bankBefore = a.getModel().bank;
    }

    BreakForgeProcessor b;
    b.setRateAndBufferSizeDetails (48000.0, 256);
    b.prepareToPlay (48000.0, 256);
    b.setStateInformation (saved.getData(), (int) saved.getSize());
    pump (50);
    REQUIRE (b.getSampleManager().waitAndApply (10000));

    REQUIRE_THAT (b.getParameterValue (bf::ParamID::swing), Catch::Matchers::WithinAbs (61.5, 0.05));
    REQUIRE_THAT (b.getParameterValue (bf::ParamID::humanizeTime), Catch::Matchers::WithinAbs (7.0, 0.05));
    REQUIRE ((int) b.getParameterValue (bf::ParamID::subStyle) == 3);
    REQUIRE_THAT (b.getParameterValue (bf::ParamID::track (4, bf::ParamID::pitch)), Catch::Matchers::WithinAbs (-5.0, 0.01));
    REQUIRE (b.getParameterValue (bf::ParamID::track (2, bf::ParamID::mute)) > 0.5f);

    REQUIRE (b.getModel().bank == bankBefore);
    REQUIRE_THAT (b.getModel().userGroove.timing[3], Catch::Matchers::WithinAbs (0.2, 1e-4));
    REQUIRE (b.getModel().midiNotes[6] == 77);
    REQUIRE (b.getModel().humanizeSeed == 4242u);

    REQUIRE (b.getTrackSettings()[1].name == "Amen Snare");
    REQUIRE (b.getTrackSettings()[0].generatorLocked);
    REQUIRE (b.getTrackSettings()[5].sampleLocked);

    REQUIRE (b.getSampleManager().getRef (0).file.getFileName() == "my kick.wav");
    REQUIRE (b.getSampleSlots().get (0) != nullptr);
    REQUIRE (b.getSampleSlots().get (0)->name == "my kick");
    // Missing file: path kept, warning shown, track still plays the built-in fallback.
    REQUIRE (b.getSampleManager().getRef (7).file.getFileName() == "missing.wav");
    REQUIRE (b.getSampleManager().hasWarning (7));
    REQUIRE (b.getSampleSlots().get (7) != nullptr);

    // Garbage state must be ignored, not crash.
    const char junk[] = "definitely not a plugin state";
    b.setStateInformation (junk, (int) sizeof (junk));
    REQUIRE (b.getModel().bank == bankBefore);

    tmp.deleteRecursively();
}

TEST_CASE ("Dropped samples go to individual hits and survive save and restore", "[plugin][samples]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tmp = juce::File::createTempFile ("bf_hits");
    tmp.createDirectory();

    const auto makeFile = [&] (const juce::String& name, float level) {
        juce::AudioBuffer<float> b (1, 64);
        b.clear();
        b.setSample (0, 0, level);
        const auto f = tmp.getChildFile (name);
        REQUIRE (bf::Exporter::writeWav (f, b, 48000.0));
        return bf::SampleRef::fromFile (f);
    };

    const auto snareA = makeFile ("snare a.wav", 0.5f);
    const auto snareB = makeFile ("snare b.wav", 0.25f);
    juce::MemoryBlock saved;

    {
        BreakForgeProcessor p;
        p.prepareToPlay (48000.0, 256);
        p.selectPattern (6); // an empty slot

        // First file gets slot 1, the same file is reused, a different file gets slot 2.
        REQUIRE (p.assignHitSample (1, 3, snareA) == 1);
        REQUIRE (p.assignHitSample (1, 9, snareA) == 1);
        REQUIRE (p.assignHitSample (1, 11, snareB) == 2);
        REQUIRE (p.getSampleManager().waitAndApply (10000));

        const auto& steps = p.getModel().bank.patterns[6].tracks[1].steps;
        REQUIRE (steps[3].on);                 // dropping on an empty step turns it on
        REQUIRE (steps[3].sample == 1);
        REQUIRE (steps[9].sample == 1);
        REQUIRE (steps[11].sample == 2);
        REQUIRE (p.getSampleSlots().get (1, 1)->name == "snare a");
        REQUIRE (p.getSampleSlots().get (1, 2)->name == "snare b");
        REQUIRE (p.isHitSampleUsed (1, 2));

        // Dropping the track's own sample just points the hit back at it.
        REQUIRE (p.assignHitSample (1, 5, p.getSampleManager().getRef (1)) == 0);

        // Back to the track sound.
        p.clearHitSample (1, 9);
        REQUIRE (p.getModel().bank.patterns[6].tracks[1].steps[9].sample == 0);

        // 15 distinct samples fill every per-hit slot; a 16th distinct one is refused.
        for (int i = 0; i < 13; ++i)
            REQUIRE (p.assignHitSample (2, i, makeFile ("ghost " + juce::String (i) + ".wav", 0.1f)) > 0);
        REQUIRE (p.assignHitSample (2, 13, makeFile ("ghost 13.wav", 0.1f)) > 0);
        REQUIRE (p.assignHitSample (2, 14, makeFile ("ghost 14.wav", 0.1f)) > 0);
        REQUIRE (p.assignHitSample (2, 15, makeFile ("ghost 15.wav", 0.1f)) == -1);
        // ...until a hit is reset, which frees its slot for reuse.
        p.clearHitSample (2, 0);
        REQUIRE (p.assignHitSample (2, 15, makeFile ("ghost 15.wav", 0.1f)) > 0);

        REQUIRE (p.getSampleManager().waitAndApply (10000));
        p.getStateInformation (saved);
    }

    BreakForgeProcessor q;
    q.prepareToPlay (48000.0, 256);
    q.setStateInformation (saved.getData(), (int) saved.getSize());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    REQUIRE (q.getSampleManager().waitAndApply (10000));

    REQUIRE (q.getModel().bank.patterns[6].tracks[1].steps[11].sample == 2);
    REQUIRE (q.getSampleManager().getRef (1, 1) == snareA);
    REQUIRE (q.getSampleManager().getRef (1, 2) == snareB);
    REQUIRE (q.getSampleSlots().get (1, 2) != nullptr);
    REQUIRE (q.getSampleSlots().get (1, 2)->name == "snare b");

    tmp.deleteRecursively();
}

TEST_CASE ("Processor plays the factory pattern in sync with the host and reports latency", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    BreakForgeProcessor p;
    FakePlayHead playhead;
    p.setPlayHead (&playhead);
    p.setRateAndBufferSizeDetails (44100.0, 512);
    p.prepareToPlay (44100.0, 512);
    REQUIRE (p.getSampleManager().waitAndApply (20000));
    REQUIRE (p.getLatencySamples() > 0);

    juce::AudioBuffer<float> buffer (2 + 2 * bf::kNumTracks, 512);
    juce::MidiBuffer midi;
    double peak = 0.0;

    for (int block = 0; block < 200; ++block)
    {
        playhead.ppq = block * 512.0 / (44100.0 * 60.0 / 174.0);
        midi.clear();
        p.processBlock (buffer, midi);
        peak = std::max (peak, (double) buffer.getMagnitude (0, 0, 512));
    }

    REQUIRE (peak > 0.1);  // the two-step is audible
    REQUIRE (peak <= juce::Decibels::decibelsToGain (-0.3) + 1e-3); // limiter ceiling
    p.releaseResources();
}

TEST_CASE ("Editor opens, resizes and closes", "[plugin][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    BreakForgeProcessor p;
    p.prepareToPlay (48000.0, 256);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    REQUIRE (editor != nullptr);

    for (auto [w, h] : { std::pair { 1240, 660 }, std::pair { 1600, 1000 }, std::pair { 2400, 1500 } })
    {
        editor->setSize (w, h);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        REQUIRE (image.getWidth() == w);
    }

    pump (100);
    editor.reset();
}

// Hidden: `BreakForgeTests "[.uidrop]"` with BF_SHOT_DIR set drives a file drop through the real
// editor and saves before/after screenshots.
TEST_CASE ("Drop samples onto steps through the editor", "[.uidrop]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File dir (juce::SystemStats::getEnvironmentVariable ("BF_SHOT_DIR", {}));
    REQUIRE (dir.isDirectory());

    juce::StringArray files;
    for (int i = 1; i <= 3; ++i)
    {
        juce::AudioBuffer<float> b (1, 64);
        b.clear();
        b.setSample (0, 0, 0.5f);
        const auto f = dir.getChildFile ("amen slice " + juce::String (i) + ".wav");
        REQUIRE (bf::Exporter::writeWav (f, b, 48000.0));
        files.add (f.getFullPathName());
    }

    BreakForgeProcessor p;
    p.prepareToPlay (48000.0, 256);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize (1400, 820);

    // Find the step grid and drop on the snare row, step 5.
    bf::ui::StepGrid* grid = nullptr;
    for (auto* c : editor->getChildren())
        if (auto* g = dynamic_cast<bf::ui::StepGrid*> (c))
            grid = g;
    REQUIRE (grid != nullptr);

    const auto layout = bf::ui::RowLayout::fit (grid->getHeight(), 0);
    const int y = layout.row (1, grid->getWidth()).getCentreY();
    const int x = (int) (grid->getWidth() * 4.5f / 16.0f);

    REQUIRE (grid->isInterestedInFileDrag (files));
    grid->fileDragEnter (files, x, y);
    editor->createComponentSnapshot (editor->getLocalBounds()).convertedToFormat (juce::Image::RGB);
    {
        juce::FileOutputStream out (dir.getChildFile ("drop_hover.png"));
        out.setPosition (0); out.truncate();
        juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), out);
    }

    grid->filesDropped (files, x, y);
    REQUIRE (p.getSampleManager().waitAndApply (10000));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

    const auto& snare = p.getModel().bank.patterns[0].tracks[1].steps;
    REQUIRE (snare[4].sample > 0);
    REQUIRE (snare[5].sample > 0);
    REQUIRE (snare[6].sample > 0);
    REQUIRE (snare[4].sample != snare[5].sample);

    juce::FileOutputStream out (dir.getChildFile ("drop_after.png"));
    out.setPosition (0); out.truncate();
    juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), out);
    editor.reset();
}
