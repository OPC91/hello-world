#include "Engine.h"

#include <cmath>

namespace bf
{

namespace
{
    constexpr int midiChannel = 10; // General MIDI drums

    bool isPatternNote (int note) noexcept { return note >= kPatternSelectBaseNote && note < kPatternSelectBaseNote + kNumPatterns; }
    bool isTrackNote (int note) noexcept   { return note >= kTrackTriggerBaseNote && note < kTrackTriggerBaseNote + kNumTracks; }
}

Engine::Engine (SampleSlots& s) : slots (s) {}

void Engine::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    maxBlock = juce::jmax (16, maxBlockSize);
    trackScratch.setSize (kNumTracks * 2, maxBlock, false, true, false);
    scratchMidi.ensureSize (8192);

    for (auto& g : trackGain)
        g.reset (sr, 0.02);

    reset();
}

void Engine::reset() noexcept
{
    scheduler.reset();

    for (auto& trackVoices : voices)
        for (auto& v : trackVoices)
            v.kill();

    for (auto& p : pendingOffs)
        p.note = -1;

    wasPlaying = false;
    playheadStep = -1.0f;
}

void Engine::audition (int track, float velocity) noexcept
{
    const auto scope = auditionFifo.write (1);

    if (scope.blockSize1 > 0)
        auditionQueue[(size_t) scope.startIndex1] = { track, velocity };
    else if (scope.blockSize2 > 0)
        auditionQueue[(size_t) scope.startIndex2] = { track, velocity };
}

GrooveContext Engine::makeGrooveContext (const EngineParams& p, const SequencerSnapshot& snap) noexcept
{
    GrooveContext ctx;
    ctx.swingEighths = p.swing8;
    ctx.grooveTemplate = p.groove < groove::numFactoryTemplates ? groove::factoryTemplate (p.groove) : snap.userGroove;
    ctx.humanizeTimeMs = p.humanizeTimeMs;
    ctx.humanizeVel = p.humanizeVel;
    ctx.seed = snap.humanizeSeed;
    ctx.accentAmount = p.accentAmount;

    bool anySolo = false;

    for (const auto& t : p.tracks)
        anySolo = anySolo || t.solo;

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tp = p.tracks[(size_t) t];
        ctx.swing[(size_t) t] = tp.swingOverride ? tp.swing : p.swing;
        ctx.trackEnabled[(size_t) t] = anySolo ? tp.solo : ! tp.mute;
    }

    return ctx;
}

void Engine::process (juce::AudioBuffer<float>& main, const TrackOutputs* trackOuts, juce::MidiBuffer& midi,
                      const TransportInfo& transport, const EngineParams& params, const SequencerSnapshot& snap) noexcept
{
    const int numSamples = main.getNumSamples();
    scratchMidi.clear();

    TransportInfo chunkTransport = transport;
    const double samplesPerPpq = transport.bpm > 0.0 ? sampleRate * 60.0 / transport.bpm : 1.0;

    // Hosts may send blocks larger than announced: split them into prepared-size chunks.
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int length = juce::jmin (maxBlock, numSamples - start);
        chunkTransport.ppqStart = transport.ppqStart + start / samplesPerPpq;
        processChunk (main, trackOuts, start, length, midi, scratchMidi, chunkTransport, params, snap);
    }

    // Swapping keeps both buffers' preallocated storage, so nothing allocates here.
    midi.swapWith (scratchMidi);
}

void Engine::processChunk (juce::AudioBuffer<float>& main, const TrackOutputs* trackOuts, int start, int length,
                           const juce::MidiBuffer& midiIn, juce::MidiBuffer& midiOut, const TransportInfo& transport,
                           const EngineParams& params, const SequencerSnapshot& snap) noexcept
{
    const bool running = transport.isPlaying && transport.hasPosition && transport.bpm > 0.0;
    const double samplesPerPpq = transport.bpm > 0.0 ? sampleRate * 60.0 / transport.bpm : 1.0;

    // Turning MIDI out off, or stopping the transport, must not leave notes hanging.
    if ((wasPlaying && ! running) || (midiOutWasOn && ! params.midiOut))
    {
        allNotesOff (midiOut, start);
        scheduler.reset();
    }

    wasPlaying = running;
    midiOutWasOn = params.midiOut;

    // Pattern selection: host automation / UI first, MIDI notes are applied in time order below.
    if (params.pattern != lastParamPattern)
    {
        lastParamPattern = params.pattern;
        activePattern = params.pattern;
        patternSilenced = false;
    }

    if (const int req = requestedPattern.exchange (-1); req >= 0 && req < kNumPatterns)
    {
        activePattern = req;
        patternSilenced = false;
    }

    const GrooveContext ctx = makeGrooveContext (params, snap);
    int numHits = 0;
    int segmentStart = 0;

    const auto scheduleUpTo = [&] (int segmentEnd) {
        if (running && ! patternSilenced && segmentEnd > segmentStart)
            numHits = scheduler.collect (snap.bank.patterns[(size_t) activePattern], ctx, transport.ppqStart, transport.bpm,
                                         sampleRate, segmentStart, segmentEnd, hits, numHits);
        segmentStart = segmentEnd;
    };

    for (const auto metadata : midiIn)
    {
        const int pos = metadata.samplePosition - start;

        if (pos < 0 || pos >= length)
            continue;

        const auto msg = metadata.getMessage();

        if (msg.isNoteOnOrOff() && isPatternNote (msg.getNoteNumber()) && params.midiPatternMode != MidiPatternMode::Off)
        {
            scheduleUpTo (pos);
            handlePatternNote (msg, params.midiPatternMode);
        }
        else if (msg.isNoteOn() && isTrackNote (msg.getNoteNumber()) && numHits < Scheduler::maxHitsPerBlock)
        {
            HitEvent e;
            e.track = msg.getNoteNumber() - kTrackTriggerBaseNote;
            e.velocity = msg.getFloatVelocity();
            e.ppq = transport.ppqStart + pos / samplesPerPpq;
            hits[(size_t) numHits++] = { e, pos };
        }
    }

    scheduleUpTo (length);

    // UI auditions play at the start of the chunk.
    {
        const auto scope = auditionFifo.read (auditionFifo.getNumReady());
        const auto addAudition = [&] (int index) {
            if (numHits >= Scheduler::maxHitsPerBlock)
                return;
            HitEvent e;
            e.track = juce::jlimit (0, kNumTracks - 1, auditionQueue[(size_t) index].track);
            e.velocity = auditionQueue[(size_t) index].velocity;
            hits[(size_t) numHits++] = { e, 0 };
        };

        for (int i = 0; i < scope.blockSize1; ++i) addAudition (scope.startIndex1 + i);
        for (int i = 0; i < scope.blockSize2; ++i) addAudition (scope.startIndex2 + i);
    }

    // Segments are sorted individually; MIDI-triggered hits may interleave, so sort the lot.
    // (Stable insertion sort: std::stable_sort may allocate.)
    for (int i = 1; i < numHits; ++i)
    {
        const auto h = hits[(size_t) i];
        int j = i - 1;

        for (; j >= 0 && hits[(size_t) j].sampleOffset > h.sampleOffset; --j)
            hits[(size_t) j + 1] = hits[(size_t) j];

        hits[(size_t) j + 1] = h;
    }

    for (int i = 0; i < numHits; ++i)
        triggerHit (hits[(size_t) i].event, hits[(size_t) i].sampleOffset, params, snap, start, samplesPerPpq, midiOut);

    flushNoteOffs (midiOut, start, length);

    // Render voices per track, apply the smoothed track gain, then route to main or the track bus.
    trackScratch.clear (0, length);

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tp = params.tracks[(size_t) t];
        float* l = trackScratch.getWritePointer (t * 2);
        float* r = trackScratch.getWritePointer (t * 2 + 1);

        for (auto& v : voices[(size_t) t])
            v.render (l, r, length, tp, sampleRate);

        auto& g = trackGain[(size_t) t];
        g.setTargetValue (ctx.trackEnabled[(size_t) t] ? tp.gain : 0.0f);

        float* destL = main.getWritePointer (0, start);
        float* destR = main.getWritePointer (main.getNumChannels() > 1 ? 1 : 0, start);

        if (params.multiOut && trackOuts != nullptr && trackOuts->channels[(size_t) t][0] != nullptr)
        {
            destL = trackOuts->channels[(size_t) t][0] + start;
            destR = trackOuts->channels[(size_t) t][1] + start;
        }

        if (g.isSmoothing())
        {
            for (int i = 0; i < length; ++i)
            {
                const float gain = g.getNextValue();
                destL[i] += l[i] * gain;
                destR[i] += r[i] * gain;
            }
        }
        else
        {
            juce::FloatVectorOperations::addWithMultiply (destL, l, g.getTargetValue(), length);
            juce::FloatVectorOperations::addWithMultiply (destR, r, g.getTargetValue(), length);
        }
    }

    // UI feedback
    activePatternForUi.store (activePattern, std::memory_order_relaxed);

    if (running && ! patternSilenced)
    {
        const auto& pat = snap.bank.patterns[(size_t) activePattern];
        const double ppq = transport.ppqStart + length / samplesPerPpq;
        const double inPattern = ppq - std::floor (ppq / pat.lengthPpq()) * pat.lengthPpq();
        playheadStep.store ((float) (inPattern / pat.stepLengthPpq()), std::memory_order_relaxed);
    }
    else
    {
        playheadStep.store (-1.0f, std::memory_order_relaxed);
    }
}

void Engine::triggerHit (const HitEvent& e, int offset, const EngineParams& params, const SequencerSnapshot& snap,
                         int chunkStart, double samplesPerPpq, juce::MidiBuffer& midiOut) noexcept
{
    const int track = e.track;
    const auto& tp = params.tracks[(size_t) track];
    const double gateSamples = e.lengthPpq * samplesPerPpq;

    triggeredTracks.fetch_or (1u << track, std::memory_order_relaxed);

    // Choke: any other track in the same group is cut at this sample.
    if (tp.chokeGroup > 0)
        for (int other = 0; other < kNumTracks; ++other)
            if (other != track && params.tracks[(size_t) other].chokeGroup == tp.chokeGroup)
                for (auto& v : voices[(size_t) other])
                    v.choke (offset);

    if (auto* sample = slots.acquire (track))
    {
        auto& pool = voices[(size_t) track];
        Voice* target = &pool[0];

        for (auto& v : pool)
        {
            if (! v.isActive()) { target = &v; break; }
            if (v.getAge() < target->getAge()) target = &v;
        }

        Voice::Trigger trig;
        trig.sample = sample;
        trig.track = track;
        trig.delaySamples = offset;
        trig.velocity = e.velocity;
        trig.pitchSemis = e.pitch;
        trig.gateSamples = gateSamples;
        trig.age = ++voiceAge;
        target->start (trig, tp, params.velocityDepth, sampleRate);
    }

    if (params.midiOut)
    {
        const int note = juce::jlimit (0, 127, snap.midiNotes[(size_t) track]);
        auto& pending = pendingOffs[(size_t) note];
        const int at = chunkStart + offset;

        if (pending.note >= 0)
            midiOut.addEvent (juce::MidiMessage::noteOff (midiChannel, note), at);

        midiOut.addEvent (juce::MidiMessage::noteOn (midiChannel, note, (juce::uint8) juce::jlimit (1, 127, (int) std::lround (e.velocity * 127.0f))), at);
        pending.note = note;
        pending.samplesLeft = offset + juce::jmax (1.0, gateSamples);
    }
}

void Engine::flushNoteOffs (juce::MidiBuffer& midiOut, int chunkStart, int length) noexcept
{
    for (auto& p : pendingOffs)
    {
        if (p.note < 0)
            continue;

        if (p.samplesLeft < length)
        {
            midiOut.addEvent (juce::MidiMessage::noteOff (midiChannel, p.note), chunkStart + juce::jmax (0, (int) p.samplesLeft));
            p.note = -1;
        }
        else
        {
            p.samplesLeft -= length;
        }
    }
}

void Engine::allNotesOff (juce::MidiBuffer& midiOut, int offset) noexcept
{
    for (auto& p : pendingOffs)
    {
        if (p.note >= 0)
            midiOut.addEvent (juce::MidiMessage::noteOff (midiChannel, p.note), offset);

        p.note = -1;
    }
}

void Engine::handlePatternNote (const juce::MidiMessage& msg, MidiPatternMode mode) noexcept
{
    const int index = msg.getNoteNumber() - kPatternSelectBaseNote;

    // Remove from the held list (if present) in both cases, re-add on note-on.
    for (int i = 0; i < numHeldPatterns; ++i)
    {
        if (heldPatternOrder[(size_t) i] == index)
        {
            for (int j = i; j < numHeldPatterns - 1; ++j)
                heldPatternOrder[(size_t) j] = heldPatternOrder[(size_t) j + 1];

            --numHeldPatterns;
            break;
        }
    }

    if (msg.isNoteOn())
    {
        if (numHeldPatterns < kNumPatterns)
            heldPatternOrder[(size_t) numHeldPatterns++] = index;

        activePattern = index;
        patternSilenced = false;
    }
    else if (mode == MidiPatternMode::Hold)
    {
        if (numHeldPatterns > 0)
            activePattern = heldPatternOrder[(size_t) numHeldPatterns - 1];
        else
            patternSilenced = true;
    }
}

} // namespace bf
