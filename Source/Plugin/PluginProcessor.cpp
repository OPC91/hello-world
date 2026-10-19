#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Core/DrumSynth.h"
#include "Core/PatternTools.h"
#include "Core/StateIO.h"
#include "Core/UserLibrary.h"

namespace
{
    const juce::Identifier idState ("BreakForgeState"), idVersion ("version"), idTracks ("Tracks"), idTrack ("Track"),
        idIndex ("i"), idName ("name"), idGenLock ("genLock"), idSampleLock ("sampleLock"), idMidiNote ("midiNote"),
        idSeed ("seed"), idUserGroove ("UserGroove");
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
      paramCache (apvts)
{
    for (int t = 0; t < bf::kNumTracks; ++t)
        trackSettings[(size_t) t].name = bf::kTrackDefaults[(size_t) t].name;

    // Temporary default: a plain two-step so the sequencer is audible until the factory content lands.
    auto& p = model.bank.patterns[0];
    for (int s : { 0, 10 }) p.tracks[0].steps[(size_t) s].on = true;
    for (int s : { 4, 12 }) p.tracks[1].steps[(size_t) s].on = true;

    publishModel();
    startTimerHz (2);
}

BreakForgeProcessor::~BreakForgeProcessor()
{
    stopTimer();
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

    if (! juce::approximatelyEqual (sampleRate, preparedSampleRate))
    {
        preparedSampleRate = sampleRate;
        installTemporarySamples (sampleRate);
    }
}

void BreakForgeProcessor::installTemporarySamples (double sampleRate)
{
    auto kick = bf::DrumSynth::renderKick (sampleRate, {});
    sampleSlots.set (0, new bf::SampleData (std::move (kick), sampleRate, "Synth Kick"));
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
}

// -----------------------------------------------------------------------------

void BreakForgeProcessor::publishModel()
{
    const juce::ScopedLock sl (modelLock);
    snapshot.publish (model);
}

void BreakForgeProcessor::selectPattern (int index)
{
    index = juce::jlimit (0, bf::kNumPatterns - 1, index);
    engine.requestPattern (index);

    if (auto* p = apvts.getParameter (bf::ParamID::pattern))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (index + 1)));
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
    });
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

    editModel ([&t] (bf::SequencerSnapshot& m) { m.userGroove = t; });
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
    sampleSlots.collectGarbage();
}

// -----------------------------------------------------------------------------

void BreakForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock sl (modelLock);
    juce::ValueTree state (idState);
    state.setProperty (idVersion, 1, nullptr);
    state.setProperty (idSeed, (juce::int64) model.humanizeSeed, nullptr);
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

    if (auto params = state.getChildWithName (apvts.state.getType()); params.isValid())
        apvts.replaceState (params);

    model.bank = bf::StateIO::bankFromTree (state.getChildWithName ("Patterns"));
    model.humanizeSeed = (std::uint32_t) (juce::int64) state.getProperty (idSeed, 1);

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
    }

    publishModel();
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
