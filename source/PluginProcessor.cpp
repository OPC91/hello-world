#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "dsp/GainMatcher.h"
#include "reference/Aligner.h"

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
        bandSoloValue = parameters.getRawParameterValue (ParamIDs::bandSolo);
        crossoverModeValue = parameters.getRawParameterValue (ParamIDs::crossoverMode);

        // Create the weak-reference master now, on the constructing thread, so that
        // setStateInformation() can safely take weak references from any thread.
        [[maybe_unused]] const juce::WeakReference<BenchmarkProcessor> init (this);

        analysisEngine.startThread();
        startTimerHz (20);
    }

    BenchmarkProcessor::~BenchmarkProcessor()
    {
        stopTimer();
        cancelPendingUpdate();
        cancelAlignment();
        alignmentPool.removeAllJobs (true, 10000);
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

        bandSolo.setMode ((dsp::BandSolo::Mode) juce::roundToInt (crossoverModeValue->load()));
        bandSolo.setBand (juce::roundToInt (bandSoloValue->load()) - 1);
        bandSolo.prepare (sampleRate, scratchCapacity, numChannels);
        setLatencySamples (bandSolo.getLatencyFor (bandSolo.getActiveMode()));

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
        midi.clear();
        processInternal (buffer, false);
    }

    void BenchmarkProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        // Host bypass: play the mix, but through the same path so any latency stays constant.
        midi.clear();
        processInternal (buffer, true);
    }

    void BenchmarkProcessor::processInternal (juce::AudioBuffer<float>& buffer, bool hostBypassed) noexcept
    {
        juce::ScopedNoDenormals noDenormals;

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
        captureForAlignment (buffer, numChannels);

        // Offline renders (Export / bounce) always get the plain mix: no matching gain,
        // no reference and no band solo, so a file is never made quieter by accident.
        const auto renderingOffline = isNonRealtime();
        const auto bypassed = hostBypassed || renderingOffline || bypassValue->load (std::memory_order_relaxed) >= 0.5f;
        const auto listenToReference = listenReferenceValue->load (std::memory_order_relaxed) >= 0.5f;
        const auto matchLoudness = loudnessMatchValue->load (std::memory_order_relaxed) >= 0.5f;

        auto referenceLufs = reference != nullptr ? reference->info.integratedFor (numChannels) : LiveReadings::noValue;

        // While a loop plays in Free mode, match against the loop's loudness (a chorus is
        // typically louder than the whole track). In Sync the whole track is the fair basis.
        bool loudnessFromLoop = false;
        if (reference != nullptr && ! syncFollowingHost && LoopPoints::unpack (transport.loop.load (std::memory_order_relaxed)).isValid())
        {
            const auto loopLufs = (numChannels == 1 ? transport.loopLufsMono : transport.loopLufsStereo).load (std::memory_order_relaxed);
            if (std::isfinite (loopLufs) && loopLufs > -70.0f)
            {
                referenceLufs = loopLufs;
                loudnessFromLoop = true;
            }
        }

        dsp::MatchGains gains;
        if (matchLoudness && reference != nullptr && ! bypassed)
            gains = dsp::computeMatchGains (mixMeter.getIntegrated(), referenceLufs);

        mixGain.setTargetValue (juce::Decibels::decibelsToGain (gains.mixDb));
        referenceGain.setTargetValue (juce::Decibels::decibelsToGain (gains.referenceDb));
        crossfader.setTarget (! bypassed && listenToReference && reference != nullptr);

        if (renderingOffline)
        {
            // No ramps in a render: start on the untouched mix immediately.
            crossfader.jumpToTarget();
            mixGain.setCurrentAndTargetValue (1.0f);
            referenceGain.setCurrentAndTargetValue (1.0f);
        }

        bandSolo.setMode ((dsp::BandSolo::Mode) juce::roundToInt (crossoverModeValue->load (std::memory_order_relaxed)));
        bandSolo.setBand (bypassed ? dsp::BandSolo::off : juce::roundToInt (bandSoloValue->load (std::memory_order_relaxed)) - 1);

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
        readings.referenceLoudnessFromLoop.store (loudnessFromLoop, std::memory_order_relaxed);
        readings.rendering.store (renderingOffline, std::memory_order_relaxed);

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

        lastHostPlaying = hostPlaying;
        syncFollowingHost = syncModeValue->load (std::memory_order_relaxed) >= 0.5f && hostTime.has_value();
        lastHostTimeValid = hostTime.has_value();
        lastHostTime = hostTime.value_or (0);

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
        }
        else
        {
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

        // Band solo applies to whatever is audible, Mix or Reference alike. It is skipped
        // in offline renders, except that linear-phase mode keeps its (compensated) delay.
        if (isNonRealtime() && bandSolo.getActiveMode() == dsp::BandSolo::Mode::zeroLatency)
            return;

        if (! bandSolo.isTransparent())
            bandSolo.process (channels, numChannels, numSamples);
    }

    //==============================================================================
    void BenchmarkProcessor::captureForAlignment (const juce::AudioBuffer<float>& buffer, int numChannels) noexcept
    {
        const auto state = captureState.load (std::memory_order_acquire);

        if (state != captureArmed && state != captureRunning)
            return;

        const auto sync = syncModeValue->load (std::memory_order_relaxed) >= 0.5f;
        if (! sync || ! lastHostPlaying || ! lastHostTimeValid)
            return; // waits until the session plays

        const auto numSamples = buffer.getNumSamples();
        auto written = captureWritten.load (std::memory_order_relaxed);

        // (Re)start when arming, or when the host jumped: the capture must be continuous.
        if (state == captureArmed || lastHostTime != captureExpectedTime)
        {
            captureStartTime = lastHostTime;
            written = 0;
            captureState.store (captureRunning, std::memory_order_release);
        }

        const auto capacity = (int) captureBuffer.size();
        const auto count = juce::jmin (numSamples, capacity - written);
        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (numChannels > 1 ? 1 : 0);

        for (int i = 0; i < count; ++i)
            captureBuffer[(size_t) (written + i)] = 0.5f * (l[i] + r[i]);

        written += count;
        captureExpectedTime = lastHostTime + numSamples;
        captureWritten.store (written, std::memory_order_relaxed);

        if (written >= capacity)
        {
            captureStartPublished.store (captureStartTime, std::memory_order_relaxed);
            captureState.store (captureComplete, std::memory_order_release);
        }
    }

    bool BenchmarkProcessor::startAlignment()
    {
        cancelAlignment();

        const auto* active = library.getActive();
        const auto rate = hostSampleRate.load();

        if (active == nullptr || active->audio == nullptr || rate <= 0.0)
        {
            alignmentStatus = { AlignmentStatus::State::failed, 0.0f, "Load a reference first" };
            return false;
        }

        if (syncModeValue->load() < 0.5f)
        {
            alignmentStatus = { AlignmentStatus::State::failed, 0.0f, "Switch to Sync mode to align" };
            return false;
        }

        // The audio thread doesn't touch the buffer while the capture is idle.
        captureBuffer.assign ((size_t) (alignmentCaptureSeconds * rate), 0.0f);
        captureWritten = 0;
        alignmentReferenceId = active->id;
        captureState.store (captureArmed, std::memory_order_release);

        alignmentStatus = { AlignmentStatus::State::listening, 0.0f, "Play your session where the song is playing" };
        return true;
    }

    void BenchmarkProcessor::cancelAlignment()
    {
        captureState.store (captureIdle, std::memory_order_release);

        if (alignmentCancel != nullptr)
            alignmentCancel->store (true);

        alignmentCancel = nullptr;
        alignmentStatus = {};
    }

    void BenchmarkProcessor::launchAlignmentAnalysis()
    {
        const auto index = library.findIndex (alignmentReferenceId);
        const auto rate = hostSampleRate.load();

        if (index < 0 || library[index].audio == nullptr)
        {
            captureState.store (captureIdle);
            alignmentStatus = { AlignmentStatus::State::failed, 0.0f, "The reference changed while listening" };
            return;
        }

        auto reference = library[index].audio;
        auto excerpt = captureBuffer; // the job owns a copy
        const auto startTime = captureStartPublished.load();
        captureState.store (captureIdle, std::memory_order_release);

        alignmentCancel = std::make_shared<std::atomic<bool>> (false);
        alignmentStatus = { AlignmentStatus::State::analysing, 1.0f, "Finding the match..." };

        alignmentPool.addJob ([reference, excerpt = std::move (excerpt), startTime, rate, cancel = alignmentCancel,
                               id = alignmentReferenceId, safeThis = juce::WeakReference<BenchmarkProcessor> (this)]
        {
            auto result = Aligner::findExcerpt (reference->samples, excerpt.data(), (int) excerpt.size(), rate, cancel.get());

            if (cancel->load())
                return;

            juce::MessageManager::callAsync ([result, startTime, rate, cancel, id, safeThis]
            {
                auto* self = safeThis.get();
                if (self == nullptr || cancel->load())
                    return;

                if (! result.found)
                {
                    self->alignmentStatus = { AlignmentStatus::State::failed, 0.0f, result.message };
                    return;
                }

                const auto offsetSeconds = (double) (result.referencePosition - startTime) / rate;
                self->library.setSyncOffset (self->library.findIndex (id), offsetSeconds, result.confidence);

                self->alignmentStatus = { AlignmentStatus::State::done, 1.0f,
                                          "Aligned: offset " + juce::String (offsetSeconds >= 0.0 ? "+" : "") + juce::String (offsetSeconds, 3)
                                              + " s (match " + juce::String (juce::roundToInt (result.confidence * 100.0f)) + "%)" };
            });
        });
    }

    //==============================================================================
    void BenchmarkProcessor::updateLatency()
    {
        const auto mode = (dsp::BandSolo::Mode) juce::roundToInt (crossoverModeValue->load());
        const auto latency = bandSolo.getLatencyFor (mode);

        if (latency != getLatencySamples())
            setLatencySamples (latency);
    }

    void BenchmarkProcessor::timerCallback()
    {
        updateLatency();

        // Keep the linear-phase filter matching the selected band (loaded off the audio thread).
        const auto bypassed = bypassValue->load() >= 0.5f || isNonRealtime();
        bandSolo.updateLinearPhaseBand (bypassed ? dsp::BandSolo::off : juce::roundToInt (bandSoloValue->load()) - 1);

        // Alignment progress.
        if (alignmentStatus.state == AlignmentStatus::State::listening)
        {
            if (captureState.load() == captureComplete)
                launchAlignmentAnalysis();
            else if (! captureBuffer.empty())
                alignmentStatus.progress = (float) captureWritten.load() / (float) captureBuffer.size();
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
