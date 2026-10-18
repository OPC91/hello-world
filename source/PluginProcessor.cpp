#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "dsp/GainMatcher.h"

namespace benchmark
{
    namespace StateIDs
    {
        static const juce::Identifier root { "BenchmarkState" };
        static const juce::Identifier version { "stateVersion" };
        static const juce::Identifier editorWidth { "editorWidth" };
        static const juce::Identifier editorHeight { "editorHeight" };
    }

    BenchmarkProcessor::BenchmarkProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          parameters (*this, nullptr, StateIDs::root, createParameterLayout()),
          library (referenceHandoff, &transport)
    {
        bypassValue = parameters.getRawParameterValue (ParamIDs::bypass);
        listenReferenceValue = parameters.getRawParameterValue (ParamIDs::listenRef);
        loudnessMatchValue = parameters.getRawParameterValue (ParamIDs::loudnessMatch);
        syncModeValue = parameters.getRawParameterValue (ParamIDs::syncMode);

        // Create the weak-reference master now, on the constructing thread, so that
        // setStateInformation() can safely take weak references from any thread.
        [[maybe_unused]] const juce::WeakReference<BenchmarkProcessor> init (this);

        analysisEngine.startThread();
    }

    BenchmarkProcessor::~BenchmarkProcessor()
    {
        cancelPendingUpdate();
        analysisEngine.stopThread();
    }

    //==============================================================================
    void BenchmarkProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
    {
        currentSampleRate = sampleRate;
        const auto numChannels = juce::jlimit (1, 2, getTotalNumOutputChannels());

        scratchCapacity = juce::jmax (maximumExpectedSamplesPerBlock, 256);
        referenceScratch.setSize (2, scratchCapacity, false, true, false);

        mixMeter.prepare (sampleRate, numChannels);
        referenceMeter.prepare (sampleRate, numChannels);
        crossfader.setTarget (false);
        crossfader.prepare (sampleRate);
        player.prepare (sampleRate);

        for (auto* g : { &mixGain, &referenceGain })
        {
            g->reset (sampleRate, gainSmoothingSeconds);
            g->setCurrentAndTargetValue (1.0f);
        }

        lastReference = nullptr;
        lastMeterUpdate = 0;

        analysisEngine.setSampleRate (sampleRate);

        // The library resamples references to this rate on its own threads.
        hostSampleRate.store (sampleRate);

        if (juce::MessageManager::existsAndIsCurrentThread())
            library.setTargetSampleRate (sampleRate);
        else
            triggerAsyncUpdate();
    }

    void BenchmarkProcessor::handleAsyncUpdate()
    {
        library.setTargetSampleRate (hostSampleRate.load());
    }

    void BenchmarkProcessor::releaseResources()
    {
    }

    bool BenchmarkProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto& out = layouts.getMainOutputChannelSet();

        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
            return false;

        // The plugin is an insert on the master bus: input and output must match.
        return layouts.getMainInputChannelSet() == out;
    }

    //==============================================================================
    void BenchmarkProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        midi.clear();

        const auto numSamples = buffer.getNumSamples();
        const auto numInputs = getTotalNumInputChannels();
        const auto numOutputs = getTotalNumOutputChannels();

        for (auto ch = numInputs; ch < numOutputs; ++ch)
            buffer.clear (ch, 0, numSamples);

        const auto numChannels = juce::jmin (numOutputs, buffer.getNumChannels(), 2);

        if (numSamples == 0 || numChannels == 0 || scratchCapacity == 0)
            return;

        const ScopedHandoffAccess<ReferenceAudio> access (referenceHandoff);
        const auto* reference = access.get();

        // A reference prepared for another sample rate is being reloaded; treat it as absent.
        if (reference != nullptr && std::abs (reference->sampleRate - currentSampleRate) > 1e-6)
            reference = nullptr;

        if (reference != lastReference)
        {
            referenceMeter.reset();
            lastReference = reference;
            analysisEngine.requestReset (false, true);
        }

        player.setReference (reference);

        if (resetMixRequested.exchange (false))
            mixMeter.reset();

        updateTransport();

        const auto bypassed = bypassValue->load (std::memory_order_relaxed) >= 0.5f;
        const auto listenToReference = listenReferenceValue->load (std::memory_order_relaxed) >= 0.5f;
        const auto matchLoudness = loudnessMatchValue->load (std::memory_order_relaxed) >= 0.5f;

        const auto referenceLufs = reference != nullptr ? reference->info.integratedFor (numChannels) : LiveReadings::noValue;

        dsp::MatchGains gains;
        if (matchLoudness && reference != nullptr && ! bypassed)
            gains = dsp::computeMatchGains (mixMeter.getIntegrated(), referenceLufs);

        mixGain.setTargetValue (juce::Decibels::decibelsToGain (gains.mixDb));
        referenceGain.setTargetValue (juce::Decibels::decibelsToGain (gains.referenceDb));
        crossfader.setTarget (! bypassed && listenToReference && reference != nullptr);

        // Process in chunks no larger than the scratch buffer, so hosts that send
        // bigger blocks than announced never cause an allocation.
        float* channels[2] {};
        for (int start = 0; start < numSamples; start += scratchCapacity)
        {
            const auto n = juce::jmin (scratchCapacity, numSamples - start);

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch] = buffer.getWritePointer (ch, start);

            processChunk (channels, numChannels, n, reference);
        }

        // Publish readings for the UI.
        readings.mixMomentary.store (mixMeter.getMomentary(), std::memory_order_relaxed);
        readings.mixShortTerm.store (mixMeter.getShortTerm(), std::memory_order_relaxed);
        readings.mixIntegrated.store (mixMeter.getIntegrated(), std::memory_order_relaxed);
        readings.referenceShortTerm.store (reference != nullptr ? referenceMeter.getShortTerm() : LiveReadings::noValue, std::memory_order_relaxed);
        readings.referenceIntegrated.store (referenceLufs, std::memory_order_relaxed);
        readings.mixGainDb.store (juce::Decibels::gainToDecibels (mixGain.getCurrentValue()), std::memory_order_relaxed);
        readings.referenceGainDb.store (juce::Decibels::gainToDecibels (referenceGain.getCurrentValue()), std::memory_order_relaxed);
        readings.referenceAvailable.store (reference != nullptr, std::memory_order_relaxed);
        readings.crossfadePosition.store (crossfader.getPosition(), std::memory_order_relaxed);

        transport.playhead.store (player.getPosition(), std::memory_order_relaxed);
        transport.referencePlaying.store (reference != nullptr && player.isPlaying(), std::memory_order_relaxed);
    }

    void BenchmarkProcessor::updateTransport() noexcept
    {
        bool hostPlaying = false;
        std::optional<int64_t> hostTime;

        if (auto* hostPlayHead = getPlayHead())
        {
            if (const auto position = hostPlayHead->getPosition())
            {
                hostPlaying = position->getIsPlaying();

                if (const auto samples = position->getTimeInSamples())
                    hostTime = *samples;
                else if (const auto seconds = position->getTimeInSeconds())
                    hostTime = (int64_t) std::llround (*seconds * currentSampleRate);
            }
        }

        transport.hostPlaying.store (hostPlaying, std::memory_order_relaxed);
        transport.hostPositionAvailable.store (hostTime.has_value(), std::memory_order_relaxed);

        const auto seekRequest = transport.seekRequest.exchange (TransportState::noSeek);
        const auto sync = syncModeValue->load (std::memory_order_relaxed) >= 0.5f;

        if (sync && hostTime.has_value())
        {
            // Follow the host: play/stop and position, no looping, silence outside the file.
            player.setWrapAtEnd (false);
            player.clearLoop();
            player.setPlaying (hostPlaying);

            if (hostPlaying)
                player.seek (*hostTime + transport.syncOffset.load (std::memory_order_relaxed));
        }
        else
        {
            // Free: own play state, loop region and seeking. Also the fallback when
            // the host provides no position.
            player.setWrapAtEnd (true);

            if (const auto loop = LoopPoints::unpack (transport.loop.load (std::memory_order_relaxed)); loop.isValid())
                player.setLoop (loop.start, loop.end);
            else
                player.clearLoop();

            player.setPlaying (transport.freePlaying.load (std::memory_order_relaxed));

            if (seekRequest != TransportState::noSeek)
                player.seek (seekRequest);
        }
    }

    void BenchmarkProcessor::processChunk (float* const* channels, int numChannels, int numSamples, const ReferenceAudio* reference) noexcept
    {
        // The mix is always measured at the input, whichever source is audible.
        mixMeter.process (channels, numChannels, numSamples);
        analysisEngine.pushMix (channels[0], channels[numChannels > 1 ? 1 : 0], numSamples);

        float* refChannels[2] { referenceScratch.getWritePointer (0), referenceScratch.getWritePointer (1) };

        if (reference != nullptr)
        {
            player.render (refChannels, numChannels, numSamples);
            referenceMeter.process (refChannels, numChannels, numSamples);
            analysisEngine.pushReference (refChannels[0], refChannels[numChannels > 1 ? 1 : 0], numSamples);
        }

        // Fast path: only the untouched mix is audible.
        if (crossfader.isFullyA() && ! mixGain.isSmoothing() && juce::exactlyEqual (mixGain.getCurrentValue(), 1.0f))
        {
            referenceGain.skip (numSamples);
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            float fadeMix, fadeRef;
            crossfader.next (fadeMix, fadeRef);

            const auto gMix = mixGain.getNextValue() * fadeMix;
            const auto gRef = referenceGain.getNextValue() * fadeRef;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto ref = reference != nullptr ? refChannels[ch][i] : 0.0f;
                channels[ch][i] = channels[ch][i] * gMix + ref * gRef;
            }
        }
    }

    juce::AudioProcessorParameter* BenchmarkProcessor::getBypassParameter() const
    {
        return parameters.getParameter (ParamIDs::bypass);
    }

    //==============================================================================
    juce::AudioProcessorEditor* BenchmarkProcessor::createEditor()
    {
        return new BenchmarkEditor (*this);
    }

    //==============================================================================
    void BenchmarkProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto state = parameters.copyState();
        state.setProperty (StateIDs::version, stateVersion, nullptr);

        state.removeChild (state.getChildWithName (ReferenceLibrary::stateType), nullptr);
        state.appendChild (library.getStateSnapshot(), nullptr);

        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void BenchmarkProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto xml = getXmlFromBinary (data, sizeInBytes);

        if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
            return; // Unknown or corrupt data: keep current state.

        auto state = juce::ValueTree::fromXml (*xml);
        auto references = state.getChildWithName (ReferenceLibrary::stateType);
        state.removeChild (references, nullptr);

        if (! references.isValid())
            references = juce::ValueTree (ReferenceLibrary::stateType);

        // Parameters are restored immediately, whichever thread the host uses:
        // hosts (and validators) read them back straight after this call.
        parameters.replaceState (state);

        if (juce::MessageManager::existsAndIsCurrentThread())
        {
            library.restoreState (references);
            return;
        }

        // The reference library lives on the message thread.
        juce::MessageManager::callAsync ([safeThis = juce::WeakReference<BenchmarkProcessor> (this), references]
        {
            if (auto* self = safeThis.get())
                self->library.restoreState (references);
        });
    }

    juce::Point<int> BenchmarkProcessor::getSavedEditorSize() const
    {
        return { (int) parameters.state.getProperty (StateIDs::editorWidth, 0),
                 (int) parameters.state.getProperty (StateIDs::editorHeight, 0) };
    }

    void BenchmarkProcessor::setSavedEditorSize (juce::Point<int> size)
    {
        parameters.state.setProperty (StateIDs::editorWidth, size.x, nullptr);
        parameters.state.setProperty (StateIDs::editorHeight, size.y, nullptr);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new benchmark::BenchmarkProcessor();
}
