#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace seam
{
    namespace
    {
        const juce::Identifier editorNode { "editor" };
        const juce::Identifier themeProp { "theme" };
        const juce::Identifier scaleProp { "scale" };
        const juce::Identifier breakNode { "break" };
        const juce::Identifier nameProp { "name" };
        const juce::Identifier pathProp { "path" };
        const juce::Identifier dataProp { "data" };
        const juce::Identifier seedProp { "seed" };
        const juce::Identifier presetProp { "preset" };

        template <typename Array>
        int pick (const Array& values, std::atomic<float>* index)
        {
            const auto count = static_cast<int> (std::size (values));
            return values[static_cast<size_t> (juce::jlimit (0, count - 1, juce::roundToInt (index->load())))];
        }

        void atomicMax (std::atomic<float>& target, float value) noexcept
        {
            float current = target.load (std::memory_order_relaxed);
            while (value > current && ! target.compare_exchange_weak (current, value, std::memory_order_relaxed)) {}
        }
    }

    SeamProcessor::SeamProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "SEAM", createParameterLayout())
    {
        auto raw = [this] (const char* id) { return state.getRawParameterValue (id); };
        p.transition = raw (ids::transition); p.style = raw (ids::style); p.complexity = raw (ids::complexity);
        p.fillLength = raw (ids::fillLength); p.fillMode = raw (ids::fillMode); p.vacuum = raw (ids::vacuum);
        p.vacuumSize = raw (ids::vacuumSize); p.tapeStop = raw (ids::tapeStop); p.autoMode = raw (ids::autoMode);
        p.autoBars = raw (ids::autoBars); p.autoCurve = raw (ids::autoCurve); p.phrase = raw (ids::phrase);
        for (size_t i = 0; i < p.amounts.size(); ++i)
            p.amounts[i] = raw (advancedAmounts[i].id);

        state.state.getOrCreateChildWithName (editorNode, nullptr).setProperty (themeProp, "drum-and-bass", nullptr);
        worker.startWorking();
    }

    SeamProcessor::~SeamProcessor() = default;

    bool SeamProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        // Stereo out, from stereo or mono in.
        const auto out = layouts.getMainOutputChannelSet();
        const auto in = layouts.getMainInputChannelSet();
        return out == juce::AudioChannelSet::stereo()
               && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
    }

    void SeamProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        engine.prepare (sampleRate, samplesPerBlock);
    }

    void SeamProcessor::releaseResources() { engine.reset(); }

    engine::Settings SeamProcessor::readSettings() const noexcept
    {
        engine::Settings s;
        s.transition = juce::jlimit (0.0f, 1.0f, p.transition->load() * 0.01f);
        s.style = juce::jlimit (0, 3, juce::roundToInt (p.style->load()));
        s.complexity = juce::jlimit (0.0f, 1.0f, p.complexity->load() * 0.01f);
        s.fillBars = pick (choices::fillLengthBars, p.fillLength);
        s.replace = p.fillMode->load() > 0.5f;
        s.vacuum = p.vacuum->load() > 0.5f;
        s.vacuumQuarters = choices::vacuumQuarters[static_cast<size_t> (juce::jlimit (0, 2, juce::roundToInt (p.vacuumSize->load())))];
        s.tapeStop = p.tapeStop->load() > 0.5f;
        s.autoMode = p.autoMode->load() > 0.5f;
        s.autoBars = pick (choices::autoBarsValues, p.autoBars);
        s.autoCurve = juce::roundToInt (p.autoCurve->load());
        s.phraseBars = pick (choices::phraseBars, p.phrase);
        s.amtFilter = p.amounts[0]->load() * 0.01f;
        s.amtReverb = p.amounts[1]->load() * 0.01f;
        s.amtStutter = p.amounts[2]->load() * 0.01f;
        s.amtDuck = p.amounts[3]->load() * 0.01f;
        s.amtFill = p.amounts[4]->load() * 0.01f;
        return s;
    }

    void SeamProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        midi.clear();

        const int numSamples = buffer.getNumSamples();
        const int inChannels = getTotalNumInputChannels();
        const int outChannels = juce::jmin (2, getTotalNumOutputChannels(), buffer.getNumChannels());

        // Mono in, stereo out: both outputs start as the input (an exact copy, so bypass stays bit-for-bit).
        if (inChannels == 1 && outChannels == 2)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
        for (int ch = juce::jmax (inChannels, outChannels); ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);

        engine::TransportSnapshot transport;
        transport.sampleRate = getSampleRate();
        transport.numSamples = numSamples;
        if (auto* head = getPlayHead())
        {
            if (const auto pos = head->getPosition())
            {
                transport.playing = pos->getIsPlaying();
                if (const auto ppq = pos->getPpqPosition()) { transport.ppq = *ppq; transport.hasPosition = true; }
                if (const auto bpm = pos->getBpm()) transport.bpm = *bpm;
                if (const auto sig = pos->getTimeSignature())
                {
                    transport.timeSigNumerator = sig->numerator;
                    transport.timeSigDenominator = sig->denominator;
                }
                lastBpm.store (transport.bpm);
                lastNumerator.store (transport.timeSigNumerator);
                lastDenominator.store (transport.timeSigDenominator);
                worker.setQuartersPerBar (engine::PhraseTracker::quartersPerBar (transport.timeSigNumerator, transport.timeSigDenominator));
            }
        }

        engine.process (buffer.getArrayOfWritePointers(), outChannels, numSamples, transport, readSettings());

        const auto& tel = engine.telemetry();
        ui.playing.store (tel.playing);
        ui.barsToSeam.store (tel.barsToSeam);
        ui.barInPhrase.store (tel.barInPhrase);
        ui.phraseBars.store (tel.phraseBars);
        ui.beatPhase.store (tel.beatPhase);
        ui.effective.store (tel.effective);
        ui.fillActive.store (tel.fillActive);
        ui.barsPerBeat.store (tel.barsPerBeat);
        atomicMax (ui.peakLeft, buffer.getMagnitude (0, 0, numSamples));
        atomicMax (ui.peakRight, buffer.getMagnitude (outChannels > 1 ? 1 : 0, 0, numSamples));
    }

    //==============================================================================
    juce::String SeamProcessor::getThemeId() const
    {
        return state.state.getChildWithName (editorNode).getProperty (themeProp, "drum-and-bass").toString();
    }

    void SeamProcessor::setThemeId (const juce::String& id)
    {
        state.state.getOrCreateChildWithName (editorNode, nullptr).setProperty (themeProp, id, nullptr);
    }

    float SeamProcessor::getEditorScale() const
    {
        return static_cast<float> (state.state.getChildWithName (editorNode).getProperty (scaleProp, 1.0));
    }

    void SeamProcessor::setEditorScale (float scale)
    {
        state.state.getOrCreateChildWithName (editorNode, nullptr).setProperty (scaleProp, scale, nullptr);
    }

    juce::String SeamProcessor::getPresetName() const
    {
        return state.state.getChildWithName (editorNode).getProperty (presetProp, factoryPresets().front().name).toString();
    }

    void SeamProcessor::setPresetName (const juce::String& name)
    {
        state.state.getOrCreateChildWithName (editorNode, nullptr).setProperty (presetProp, name, nullptr);
    }

    void SeamProcessor::loadPreset (int index)
    {
        const auto& all = presets.getAll();
        if (index < 0 || index >= static_cast<int> (all.size()))
            return;
        const auto& preset = all[static_cast<size_t> (index)];
        presets.apply (preset, state);
        setPresetName (preset.name);
    }

    void SeamProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        if (auto xml = state.copyState().createXml())
            copyXmlToBinary (*xml, destData);
    }

    void SeamProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
            if (xml->hasTagName (state.state.getType()))
            {
                state.replaceState (juce::ValueTree::fromXml (*xml));
                restoreBreakFromState();
            }
    }

    //==============================================================================
    bool SeamProcessor::loadBreakFile (const juce::File& file)
    {
        juce::MemoryBlock bytes;
        if (! file.existsAsFile() || file.getSize() > 256 * 1024 * 1024 || ! file.loadFileAsData (bytes))
            return false;

        auto node = state.state.getOrCreateChildWithName (breakNode, nullptr);
        node.setProperty (nameProp, file.getFileNameWithoutExtension(), nullptr);
        node.setProperty (pathProp, file.getFullPathName(), nullptr);
        if (bytes.getSize() <= static_cast<size_t> (maxEmbeddedBytes))
            node.setProperty (dataProp, bytes, nullptr);
        else
            node.removeProperty (dataProp, nullptr);
        worker.loadFromMemory (std::move (bytes), file.getFileNameWithoutExtension(), file.getFullPathName());
        return true;
    }

    void SeamProcessor::useBuiltInBreak()
    {
        state.state.removeChild (state.state.getChildWithName (breakNode), nullptr);
        worker.useBuiltIn();
    }

    void SeamProcessor::newFill()
    {
        worker.newFill();
        state.state.getOrCreateChildWithName (breakNode, nullptr).setProperty (seedProp, static_cast<juce::int64> (worker.getSeed()), nullptr);
    }

    void SeamProcessor::restoreBreakFromState()
    {
        const auto node = state.state.getChildWithName (breakNode);
        worker.setSeed (static_cast<std::uint64_t> (static_cast<juce::int64> (node.getProperty (seedProp, 1))));
        if (! node.isValid() || ! (node.hasProperty (dataProp) || node.hasProperty (pathProp)))
        {
            worker.useBuiltIn();
            return;
        }

        const auto name = node.getProperty (nameProp).toString();
        const auto path = node.getProperty (pathProp).toString();
        if (const auto* embedded = node.getProperty (dataProp).getBinaryData(); embedded != nullptr && embedded->getSize() > 0)
        {
            worker.loadFromMemory (*embedded, name, path);   // embedded audio wins: the session works on any computer
            return;
        }
        const juce::File file (path);
        juce::MemoryBlock bytes;
        if (juce::File::isAbsolutePath (path) && file.existsAsFile() && file.loadFileAsData (bytes))
            worker.loadFromMemory (std::move (bytes), name, path);
        else
            worker.useBuiltIn();
    }

    juce::AudioProcessorEditor* SeamProcessor::createEditor() { return new SeamEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new seam::SeamProcessor();
}
