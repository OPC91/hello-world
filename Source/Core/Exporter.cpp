#include "Exporter.h"
#include "MasterBus.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace bf::Exporter
{

double exportLengthPpq (const Pattern& p, int bars) noexcept
{
    const double loop = p.lengthPpq();
    const double wanted = juce::jmax (1, bars) * 4.0;
    return std::ceil (wanted / loop - 1.0e-9) * loop;
}

juce::MidiFile makeMidiFile (const Pattern& pattern, const GrooveContext& ctx, const std::array<int, kNumTracks>& notes,
                             double bpm, int bars, const juce::String& name)
{
    const double total = exportLengthPpq (pattern, bars);
    const auto toTicks = [] (double ppq) { return (double) std::llround (ppq * midiTicksPerQuarter); };

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0.0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

    struct Note { double on, off; int number, velocity; };
    std::vector<Note> list;

    scheduling::forEachHit (pattern, ctx, 0.0, total, bpm, [&] (const HitEvent& e) {
        const int number = juce::jlimit (0, 127, notes[(size_t) e.track]);
        const int vel = juce::jlimit (1, 127, (int) std::lround (e.velocity * 127.0f));
        list.push_back ({ toTicks (e.ppq), toTicks (juce::jmin (total, e.ppq + e.lengthPpq)), number, vel });
    });

    std::sort (list.begin(), list.end(), [] (const Note& a, const Note& b) { return a.on < b.on; });

    // A note can't outlast the next hit on the same note (overlapping identical notes confuse DAWs).
    for (size_t i = 0; i < list.size(); ++i)
        for (size_t j = i + 1; j < list.size(); ++j)
            if (list[j].number == list[i].number)
            {
                list[i].off = juce::jmin (list[i].off, list[j].on);
                break;
            }

    for (const auto& n : list)
    {
        seq.addEvent (juce::MidiMessage::noteOn (10, n.number, (juce::uint8) n.velocity), n.on);
        seq.addEvent (juce::MidiMessage::noteOff (10, n.number), juce::jmax (n.on + 1.0, n.off));
    }

    seq.addEvent (juce::MidiMessage::endOfTrack(), toTicks (total));
    seq.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (midiTicksPerQuarter);
    file.addTrack (seq);
    return file;
}

RenderResult render (const RenderRequest& req)
{
    RenderResult result;
    const auto& pattern = req.snapshot.bank.patterns[(size_t) juce::jlimit (0, kNumPatterns - 1, req.pattern)];
    const double samplesPerPpq = req.sampleRate * 60.0 / req.bpm;
    const double totalPpq = exportLengthPpq (pattern, req.bars);
    const auto totalSamples = (int) std::lround (totalPpq * samplesPerPpq);

    // Pre-roll whole loops covering at least 3 s, so long tails wrap into the start.
    const double loopPpq = pattern.lengthPpq();
    const double prerollPpq = std::ceil (3.0 * req.bpm / 60.0 / loopPpq) * loopPpq;
    const auto prerollSamples = (int) std::lround (prerollPpq * samplesPerPpq);

    constexpr int block = 512;
    MasterBus master;
    master.prepare (req.sampleRate, block);
    const int latency = req.applyMaster ? master.getLatency() : 0;
    const int renderLength = prerollSamples + totalSamples + latency;

    SampleSlots slots;
    for (int t = 0; t < kNumTracks; ++t)
        for (int v = 0; v < kSampleVariants; ++v)
            slots.set (t, req.samples[(size_t) (t * kSampleVariants + v)], v);

    Engine engine (slots);
    engine.prepare (req.sampleRate, block);

    auto params = req.params;
    params.pattern = req.pattern;
    params.midiOut = false;
    params.multiOut = true; // every track to its own buffer; the mix is their sum

    std::array<juce::AudioBuffer<float>, kNumTracks> tracks;
    for (auto& b : tracks)
        b.setSize (2, renderLength);

    juce::AudioBuffer<float> main (2, block);
    juce::MidiBuffer midi;

    for (int pos = 0; pos < renderLength; pos += block)
    {
        const int n = juce::jmin (block, renderLength - pos);
        main.setSize (2, n, false, false, true);
        main.clear();
        midi.clear();

        TrackOutputs outs;
        for (int t = 0; t < kNumTracks; ++t)
        {
            tracks[(size_t) t].clear (pos, n);
            outs.channels[(size_t) t] = { tracks[(size_t) t].getWritePointer (0, pos), tracks[(size_t) t].getWritePointer (1, pos) };
        }

        // Engine writes are relative to the start of `main`, so hand it pointers at `pos`.
        TransportInfo transport;
        transport.isPlaying = pos < prerollSamples + totalSamples; // then let the tails ring out
        transport.hasPosition = true;
        transport.bpm = req.bpm;
        transport.ppqStart = (double) (pos - prerollSamples) / samplesPerPpq;

        engine.process (main, &outs, midi, transport, params, req.snapshot);
    }

    // Mix = sum of tracks, then the master section with its latency removed.
    juce::AudioBuffer<float> mix (2, renderLength);
    mix.clear();

    for (int t = 0; t < kNumTracks; ++t)
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, tracks[(size_t) t], ch, 0, renderLength);

    mix.applyGain (params.masterGain);

    if (req.applyMaster)
        master.process (mix, params.drive, params.driveOn, params.limiterOn);

    result.mix.setSize (2, totalSamples);
    for (int ch = 0; ch < 2; ++ch)
        result.mix.copyFrom (ch, 0, mix, ch, prerollSamples + latency, totalSamples);

    if (req.renderStems)
    {
        for (int t = 0; t < kNumTracks; ++t)
        {
            auto& stem = result.stems[(size_t) t];
            stem.setSize (2, totalSamples);

            for (int ch = 0; ch < 2; ++ch)
                stem.copyFrom (ch, 0, tracks[(size_t) t], ch, prerollSamples, totalSamples);

            stem.applyGain (params.masterGain);
            result.trackHasAudio[(size_t) t] = stem.getMagnitude (0, totalSamples) > 1.0e-5f;
        }
    }

    return result;
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());

    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (audio.getNumChannels())
                                                   .withBitsPerSample (24));

    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

bool writeMidi (const juce::File& file, const juce::MidiFile& midi)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream out (file);
    return out.openedOk() && midi.writeTo (out, 1);
}

juce::File getExportFolder()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BreakForge Export");
}

} // namespace bf::Exporter
