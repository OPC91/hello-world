#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Core/BuiltInKits.h"
#include "Core/FactoryPatterns.h"
#include "Core/PatternTools.h"
#include "Core/KitLibrary.h"
#include "Core/StateIO.h"
#include "Core/UserLibrary.h"

namespace
{
    const juce::Identifier idState ("BreakForgeState"), idVersion ("version"), idTracks ("Tracks"), idTrack ("Track"),
        idIndex ("i"), idName ("name"), idGenLock ("genLock"), idSampleLock ("sampleLock"), idMidiNote ("midiNote"),
        idSeed ("seed"), idUserGroove ("UserGroove"), idSample ("sample"), idKit ("kit");
}

juce::AudioProcessor::BusesProperties BreakForgeProcessor::makeBuses()
{
    auto buses = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);

    for (const auto& t : bf::kTrackDefaults)
        buses = buses.withOutput (t.name, juce::AudioChannelSet::stereo(), false);

    return buses;
}

BreakForgeProcessor::BreakForgeProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "Params", bf::createParameterLayout()),
      paramCache (apvts),
      sampleManager (sampleSlots,
                     [] (const juce::String& id, double rate) { return bf::BuiltInKits::render (id, rate); },
                     [] (int track) { return bf::BuiltInKits::defaultFor (bf::kTrackDefaults[(size_t) track].role); })
{
    for (int t = 0; t < bf::kNumTracks; ++t)
    {
        trackSettings[(size_t) t].name = bf::kTrackDefaults[(size_t) t].name;
        sampleManager.load (t, bf::BuiltInKits::defaultFor (bf::kTrackDefaults[(size_t) t].role));
    }

    model.bank = bf::FactoryPatterns::makeBank();
    publishModel();
    kits = bf::KitLibrary::scan ({}); // built-in kits now, user kits once the background scan finishes
    rescanKits();
    startTimerHz (30);
}

BreakForgeProcessor::~BreakForgeProcessor()
{
    stopTimer();
    aliveFlag.reset();
    backgroundPool.removeAllJobs (true, 10000);
}

bool BreakForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (! layouts.inputBuses.isEmpty() && ! layouts.getMainInputChannelSet().isDisabled())
        return false;

    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);

        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void BreakForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    masterBus.prepare (sampleRate, samplesPerBlock);

    // Track buses skip the master section, so they are delayed to stay in sync with the main out.
    for (auto& d : auxDelays)
        d.prepare (masterBus.getLatency(), samplesPerBlock);

    setLatencySamples (masterBus.getLatency());

    // Samples are (re)built at the new rate in the background. Until they arrive the voices play
    // the old buffers with rate compensation, so playback stays in tune.
    if (juce::MessageManager::existsAndIsCurrentThread())
        sampleManager.setSampleRate (sampleRate);
    else
        pendingSampleRate.store (sampleRate);
}

void BreakForgeProcessor::loadSample (int track, const bf::SampleRef& ref)
{
    sampleManager.load (track, ref);
}

void BreakForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    bf::TransportInfo transport;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            transport.isPlaying = pos->getIsPlaying();
            transport.bpm = pos->getBpm().orFallback (174.0);

            if (auto ppq = pos->getPpqPosition())
            {
                transport.ppqStart = *ppq;
                transport.hasPosition = true;
            }
            else if (auto samples = pos->getTimeInSamples())
            {
                transport.ppqStart = (double) *samples / getSampleRate() * transport.bpm / 60.0;
                transport.hasPosition = true;
            }
        }
    }

    paramCache.read (engineParams);
    const auto& snap = snapshot.read();

    auto mainBus = getBusBuffer (buffer, false, 0);

    for (int t = 0; t < bf::kNumTracks; ++t)
    {
        auto& ch = trackOutputs.channels[(size_t) t];
        ch = {};

        if (t + 1 < getBusCount (false))
        {
            if (auto* bus = getBus (false, t + 1); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() >= 2)
            {
                auto busBuffer = getBusBuffer (buffer, false, t + 1);
                ch = { busBuffer.getWritePointer (0), busBuffer.getWritePointer (1) };
            }
        }
    }

    engine.process (mainBus, &trackOutputs, midi, transport, engineParams, snap);
    mainBus.applyGain (engineParams.masterGain);
    masterBus.process (mainBus, engineParams.drive, engineParams.driveOn, engineParams.limiterOn);

    for (int t = 0; t < bf::kNumTracks; ++t)
    {
        auto& ch = trackOutputs.channels[(size_t) t];

        if (ch[0] != nullptr)
        {
            juce::FloatVectorOperations::multiply (ch[0], engineParams.masterGain, buffer.getNumSamples());
            juce::FloatVectorOperations::multiply (ch[1], engineParams.masterGain, buffer.getNumSamples());
            auxDelays[(size_t) t].process (ch[0], ch[1], buffer.getNumSamples());
        }
    }
}

void BreakForgeProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // A bypassed instrument is silent, so there is no latency to match: silence is silence.
    // Ringing voices are cut so nothing jumps back in when bypass is released.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    midi.clear();
    engine.reset();
}

// -----------------------------------------------------------------------------

void BreakForgeProcessor::publishModel()
{
    const juce::ScopedLock sl (modelLock);
    snapshot.publish (model);
}

void BreakForgeProcessor::pushUndoState()
{
    undoStack.push_back (model.bank);

    if (undoStack.size() > maxUndo)
        undoStack.erase (undoStack.begin());

    redoStack.clear();
}

void BreakForgeProcessor::undo()
{
    if (undoStack.empty())
        return;

    editModel ([this] (bf::SequencerSnapshot& m) {
        redoStack.push_back (m.bank);
        m.bank = undoStack.back();
        undoStack.pop_back();
    }, false);
}

void BreakForgeProcessor::redo()
{
    if (redoStack.empty())
        return;

    editModel ([this] (bf::SequencerSnapshot& m) {
        undoStack.push_back (m.bank);
        m.bank = redoStack.back();
        redoStack.pop_back();
    }, false);
}

bf::PatternGenerator::Options BreakForgeProcessor::makeGeneratorOptions()
{
    bf::PatternGenerator::Options o;
    o.style = (bf::SubStyle) juce::jlimit (0, bf::kNumSubStyles - 1, (int) getParameterValue (bf::ParamID::subStyle));
    o.variation = getParameterValue (bf::ParamID::variation) * 0.01f;
    o.seed = (std::uint32_t) random.nextInt();

    const auto& current = model.bank.patterns[(size_t) getEditPattern()];
    o.length = current.length;
    o.resolution = current.resolution;

    for (int t = 0; t < bf::kNumTracks; ++t)
        o.locked[(size_t) t] = trackSettings[(size_t) t].generatorLocked;

    // If the FX track holds a riser / reverse cymbal, let the generator swell it into the loop.
    const auto fxTrack = bf::trackForRole (bf::Role::FX);

    if (auto fx = sampleSlots.get (fxTrack); fx != nullptr)
    {
        const auto name = fx->name.toLowerCase();

        if (name.contains ("reverse") || name.contains ("riser") || name.contains ("swell") || name.contains ("rev "))
            o.fxRiserLengthPpq = fx->buffer.getNumSamples() / fx->sampleRate * engine.getLastBpm() / 60.0;
    }

    return o;
}

void BreakForgeProcessor::generatePattern()
{
    const auto opts = makeGeneratorOptions();
    const int slot = getEditPattern();
    const auto base = model.bank.patterns[(size_t) slot];

    backgroundPool.addJob ([this, opts, slot, base, alive = std::weak_ptr<bool> (aliveFlag)] {
        auto result = bf::PatternGenerator::generate (opts, base);

        juce::MessageManager::callAsync ([this, result, slot, style = opts.style, alive] {
            if (alive.expired())
                return;

            editModel ([&] (bf::SequencerSnapshot& m) { m.bank.patterns[(size_t) slot] = result; });

            // Generated patterns are meant to be heard through a style-appropriate groove.
            const auto g = bf::PatternGenerator::suggestedGroove (style);
            setParameterValue (bf::ParamID::groove, (float) g.templateIndex);
            setParameterValue (bf::ParamID::swing, g.swing * 100.0f);
        });
    });
}

void BreakForgeProcessor::generateTrack (int track)
{
    if (track < 0 || track >= bf::kNumTracks)
        return;

    const auto opts = makeGeneratorOptions();
    const int slot = getEditPattern();

    editModel ([&] (bf::SequencerSnapshot& m) {
        bf::PatternGenerator::generateTrack (m.bank.patterns[(size_t) slot], track, opts);
    });
}

int BreakForgeProcessor::generateFill()
{
    const auto opts = makeGeneratorOptions();
    const int source = getEditPattern();
    const int target = (source + 1) % bf::kNumPatterns;

    editModel ([&] (bf::SequencerSnapshot& m) {
        m.bank.patterns[(size_t) target] = bf::PatternGenerator::generateFill (m.bank.patterns[(size_t) source], opts);
    });

    return target;
}

void BreakForgeProcessor::selectPattern (int index)
{
    index = juce::jlimit (0, bf::kNumPatterns - 1, index);
    editPattern = index;
    engine.requestPattern (index);

    if (auto* p = apvts.getParameter (bf::ParamID::pattern))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (index + 1)));
}

const bf::KitInfo* BreakForgeProcessor::findKit (const juce::String& name) const
{
    for (const auto& k : kits)
        if (k.name == name)
            return &k;

    return nullptr;
}

void BreakForgeProcessor::selectKit (const juce::String& name)
{
    const auto* kit = findKit (name);

    if (kit == nullptr)
        return;

    currentKit = name;

    for (int t = 0; t < bf::kNumTracks; ++t)
    {
        if (trackSettings[(size_t) t].sampleLocked)
            continue;

        const auto list = kit->samplesFor (bf::kTrackDefaults[(size_t) t].role);

        if (! list.isEmpty())
            sampleManager.load (t, list[0]);
    }

    kitsChanged.sendChangeMessage();
}

void BreakForgeProcessor::randomizeSample (int track)
{
    const auto* kit = findKit (currentKit);

    if (kit == nullptr || track < 0 || track >= bf::kNumTracks)
        return;

    const auto list = kit->samplesFor (bf::kTrackDefaults[(size_t) track].role);

    if (list.isEmpty())
        return;

    // Choose a different sample than the current one when there is a choice.
    const auto current = sampleManager.getRef (track);
    juce::Array<bf::SampleRef> choices;

    for (const auto& r : list)
        if (r != current)
            choices.add (r);

    if (choices.isEmpty())
        choices = list;

    sampleManager.load (track, choices[random.nextInt (choices.size())]);
}

void BreakForgeProcessor::randomizeAllSamples()
{
    for (int t = 0; t < bf::kNumTracks; ++t)
        if (! trackSettings[(size_t) t].sampleLocked)
            randomizeSample (t);
}

void BreakForgeProcessor::exportPattern (ExportKind kind, int bars, std::function<void (juce::Array<juce::File>, juce::String)> onDone)
{
    // Snapshot everything on the message thread; the render itself runs in the background.
    bf::Exporter::RenderRequest req;
    {
        const juce::ScopedLock sl (modelLock);
        req.snapshot = model;
    }

    paramCache.read (req.params);
    req.pattern = getEditPattern();
    req.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    req.bpm = engine.getLastBpm();
    req.bars = bars;
    req.renderStems = kind == ExportKind::AudioStems;

    for (int t = 0; t < bf::kNumTracks; ++t)
        req.samples[(size_t) t] = sampleSlots.get (t);

    juce::StringArray trackNames;
    for (const auto& ts : trackSettings)
        trackNames.add (ts.name);

    const auto baseName = "BreakForge P" + juce::String (req.pattern + 1) + " " + juce::String (juce::roundToInt (req.bpm)) + "bpm";

    backgroundPool.addJob ([kind, req, baseName, trackNames, onDone, alive = std::weak_ptr<bool> (aliveFlag)] {
        juce::Array<juce::File> files;
        juce::String error;
        const auto folder = bf::Exporter::getExportFolder();

        if (kind == ExportKind::Midi)
        {
            const auto& pattern = req.snapshot.bank.patterns[(size_t) req.pattern];
            const auto ctx = bf::Engine::makeGrooveContext (req.params, req.snapshot);
            const auto midi = bf::Exporter::makeMidiFile (pattern, ctx, req.snapshot.midiNotes, req.bpm, req.bars, baseName);
            const auto file = folder.getChildFile (baseName + ".mid");

            if (bf::Exporter::writeMidi (file, midi))
                files.add (file);
            else
                error = "Could not write " + file.getFullPathName();
        }
        else
        {
            const auto result = bf::Exporter::render (req);

            if (kind == ExportKind::AudioMix)
            {
                const auto file = folder.getChildFile (baseName + ".wav");
                if (bf::Exporter::writeWav (file, result.mix, req.sampleRate)) files.add (file);
                else error = "Could not write " + file.getFullPathName();
            }
            else
            {
                for (int t = 0; t < bf::kNumTracks; ++t)
                {
                    if (! result.trackHasAudio[(size_t) t])
                        continue;

                    const auto file = folder.getChildFile (juce::File::createLegalFileName (baseName + " - " + trackNames[t]) + ".wav");
                    if (bf::Exporter::writeWav (file, result.stems[(size_t) t], req.sampleRate)) files.add (file);
                    else error = "Could not write " + file.getFullPathName();
                }
            }
        }

        juce::MessageManager::callAsync ([files, error, onDone, alive] {
            if (! alive.expired() && onDone != nullptr)
                onDone (files, error);
        });
    });
}

void BreakForgeProcessor::rescanKits()
{
    backgroundPool.addJob ([folder = bf::UserLibrary::getKitsFolder(), alive = std::weak_ptr<bool> (aliveFlag), this] {
        auto scanned = bf::KitLibrary::scan (folder);

        juce::MessageManager::callAsync ([scanned = std::move (scanned), alive, this]() mutable {
            if (alive.expired())
                return;

            kits = std::move (scanned);
            kitsChanged.sendChangeMessage();
        });
    });
}

void BreakForgeProcessor::setParameterValue (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

float BreakForgeProcessor::getParameterValue (const juce::String& id) const
{
    if (auto* v = apvts.getRawParameterValue (id))
        return v->load();

    return 0.0f;
}

void BreakForgeProcessor::applyGrooveTemplate (int index)
{
    index = juce::jlimit (0, bf::kNumGrooves - 1, index);
    const auto& t = index < bf::groove::numFactoryTemplates ? bf::groove::factoryTemplate (index) : model.userGroove;
    setParameterValue (bf::ParamID::groove, (float) index);
    setParameterValue (bf::ParamID::swing, t.swing * 100.0f);
}

void BreakForgeProcessor::rerollHumanize()
{
    editModel ([] (bf::SequencerSnapshot& m) {
        m.humanizeSeed = (std::uint32_t) juce::Random::getSystemRandom().nextInt() | 1u;
    }, false);
}

bool BreakForgeProcessor::saveUserGroove (const juce::String& name)
{
    const int index = (int) getParameterValue (bf::ParamID::groove);
    auto t = index < bf::groove::numFactoryTemplates ? bf::groove::factoryTemplate (index) : model.userGroove;
    t.swing = getParameterValue (bf::ParamID::swing) * 0.01f;
    return bf::UserLibrary::saveGroove (name, t);
}

bool BreakForgeProcessor::loadUserGroove (const juce::File& file)
{
    bf::GrooveTemplate t;

    if (! bf::UserLibrary::loadGroove (file, t))
        return false;

    editModel ([&t] (bf::SequencerSnapshot& m) { m.userGroove = t; }, false);
    applyGrooveTemplate (bf::kNumGrooves - 1);
    return true;
}

void BreakForgeProcessor::addGhostNotes()
{
    const int pattern = getEditPattern();
    editModel ([pattern] (bf::SequencerSnapshot& m) {
        bf::PatternTools::addGhostNotes (m.bank.patterns[(size_t) pattern],
                                         bf::trackForRole (bf::Role::GhostSnare), bf::trackForRole (bf::Role::Snare),
                                         (std::uint32_t) juce::Random::getSystemRandom().nextInt());
    });
}

void BreakForgeProcessor::timerCallback()
{
    // Follow pattern changes made on the audio thread (MIDI pattern notes, automation).
    if (const int playing = engine.getActivePattern(); playing != lastEnginePattern)
    {
        lastEnginePattern = playing;
        editPattern = playing;
    }

    if (const auto rate = pendingSampleRate.exchange (0.0); rate > 0.0)
        sampleManager.setSampleRate (rate);

    sampleManager.handleCompletedJobs();
    sampleSlots.collectGarbage();
}

// -----------------------------------------------------------------------------

void BreakForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock sl (modelLock);
    juce::ValueTree state (idState);
    state.setProperty (idVersion, 1, nullptr);
    state.setProperty (idSeed, (juce::int64) model.humanizeSeed, nullptr);
    state.setProperty (idKit, currentKit, nullptr);
    state.appendChild (apvts.copyState(), nullptr);
    state.appendChild (bf::StateIO::bankToTree (model.bank), nullptr);

    juce::ValueTree userGroove (idUserGroove);
    userGroove.appendChild (bf::StateIO::grooveToTree (model.userGroove), nullptr);
    state.appendChild (userGroove, nullptr);

    juce::ValueTree tracks (idTracks);

    for (int t = 0; t < bf::kNumTracks; ++t)
    {
        const auto& ts = trackSettings[(size_t) t];
        juce::ValueTree tr (idTrack);
        tr.setProperty (idIndex, t, nullptr);
        tr.setProperty (idName, ts.name, nullptr);
        tr.setProperty (idGenLock, ts.generatorLocked, nullptr);
        tr.setProperty (idSampleLock, ts.sampleLocked, nullptr);
        tr.setProperty (idMidiNote, model.midiNotes[(size_t) t], nullptr);
        tr.setProperty (idSample, sampleManager.getRef (t).toString(), nullptr);
        tracks.appendChild (tr, nullptr);
    }

    state.appendChild (tracks, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BreakForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    const auto state = juce::ValueTree::fromXml (*xml);

    if (! state.hasType (idState))
        return;

    const juce::ScopedLock sl (modelLock);
    std::array<bf::SampleRef, bf::kNumTracks> sampleRefsToLoad;

    if (auto params = state.getChildWithName (apvts.state.getType()); params.isValid())
        apvts.replaceState (params);

    model.bank = bf::StateIO::bankFromTree (state.getChildWithName ("Patterns"));
    model.humanizeSeed = (std::uint32_t) (juce::int64) state.getProperty (idSeed, 1);
    currentKit = state.getProperty (idKit, "Clean").toString();

    if (auto ug = state.getChildWithName (idUserGroove); ug.isValid() && ug.getNumChildren() > 0)
        model.userGroove = bf::StateIO::grooveFromTree (ug.getChild (0));

    for (const auto& tr : state.getChildWithName (idTracks))
    {
        const int t = tr.getProperty (idIndex, -1);

        if (t < 0 || t >= bf::kNumTracks)
            continue;

        auto& ts = trackSettings[(size_t) t];
        ts.name = tr.getProperty (idName, bf::kTrackDefaults[(size_t) t].name).toString();
        ts.generatorLocked = tr.getProperty (idGenLock, false);
        ts.sampleLocked = tr.getProperty (idSampleLock, false);
        model.midiNotes[(size_t) t] = juce::jlimit (0, 127, (int) tr.getProperty (idMidiNote, bf::kTrackDefaults[(size_t) t].midiNote));
        sampleRefsToLoad[(size_t) t] = bf::SampleRef::fromString (tr.getProperty (idSample).toString());
    }

    publishModel();

    // Sample loading belongs to the message thread; hosts may restore state from elsewhere.
    const auto applySamples = [this, refs = sampleRefsToLoad] {
        for (int t = 0; t < bf::kNumTracks; ++t)
            if (! refs[(size_t) t].isEmpty() && refs[(size_t) t] != sampleManager.getRef (t))
                sampleManager.load (t, refs[(size_t) t]);
    };

    if (juce::MessageManager::existsAndIsCurrentThread())
        applySamples();
    else
        juce::MessageManager::callAsync ([applySamples, alive = std::weak_ptr<bool> (aliveFlag)] {
            if (! alive.expired())
                applySamples();
        });

    modelChanged.sendChangeMessage();
}

juce::AudioProcessorEditor* BreakForgeProcessor::createEditor()
{
    return new BreakForgeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BreakForgeProcessor();
}
