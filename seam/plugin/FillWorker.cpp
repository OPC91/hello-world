#include "FillWorker.h"
#include "Parameters.h"

#include "engine/fill/Slicer.h"

namespace seam
{
    std::atomic<std::uint64_t> FillWorker::nextKitId { 1 };

    namespace
    {
        constexpr int peakColumns = 600;
        constexpr double maxBreakSeconds = 30.0;

        void fillPeaks (BreakView& view, const engine::BreakKit& kit)
        {
            view.peaks.assign (peakColumns, 0.0f);
            const auto n = kit.left.size();
            if (n == 0)
                return;
            for (size_t i = 0; i < n; ++i)
            {
                const auto column = std::min<size_t> (peakColumns - 1, i * peakColumns / n);
                view.peaks[column] = std::max (view.peaks[column], std::max (std::abs (kit.left[i]), std::abs (kit.right[i])));
            }
            view.markers.clear();
            for (const auto& s : kit.slices)
                view.markers.emplace_back (static_cast<float> (s.start) / static_cast<float> (n), s.type);
        }
    }

    FillWorker::FillWorker (engine::SeamEngine& e, juce::AudioProcessorValueTreeState& p)
        : juce::Thread ("SEAM fill worker"), engine (e), params (p)
    {
        formats.registerBasicFormats();
        useBuiltIn();
    }

    FillWorker::~FillWorker()
    {
        stopThread (2000);
    }

    void FillWorker::loadFromMemory (juce::MemoryBlock data, const juce::String& name, const juce::String& path)
    {
        juce::ignoreUnused (path);
        {
            const juce::ScopedLock sl (requestLock);
            pendingData = std::make_unique<juce::MemoryBlock> (std::move (data));
            pendingName = name;
            pendingBuiltIn = false;
        }
        {
            const juce::ScopedLock sl (viewLock);
            view.loading = true;
            view.error = {};
        }
        ++viewVersion;
        notify();
    }

    void FillWorker::useBuiltIn()
    {
        const juce::ScopedLock sl (requestLock);
        pendingData.reset();
        pendingBuiltIn = true;
        notify();
    }

    BreakView FillWorker::getView() const
    {
        const juce::ScopedLock sl (viewLock);
        return view;
    }

    std::vector<std::uint8_t> FillWorker::exportMidi (double bpm, int numerator, int denominator) const
    {
        std::shared_ptr<const engine::FillPattern> pattern;
        engine::KitSummary kit;
        {
            const juce::ScopedLock sl (viewLock);
            pattern = currentPattern;
            kit = currentSummary;
        }
        if (pattern == nullptr || pattern->events.empty())
            return {};
        return engine::exportFillMidi (*pattern, kit, { bpm, 960, numerator, denominator });
    }

    void FillWorker::run()
    {
        while (! threadShouldExit())
        {
            std::unique_ptr<juce::MemoryBlock> data;
            juce::String name;
            bool builtIn = false;
            {
                const juce::ScopedLock sl (requestLock);
                data = std::move (pendingData);
                name = pendingName;
                builtIn = pendingBuiltIn;
                pendingBuiltIn = false;
            }

            if (builtIn)
            {
                auto kit = std::make_unique<engine::BreakKit> (engine::makeBuiltInBreak (48000.0));
                const auto sliced = engine::sliceBreak (kit->left, kit->sampleRate);
                kit->slices = sliced.slices;
                kit->indexByType();
                installKit (std::move (kit), true);
            }
            else if (data != nullptr)
            {
                decodeAndSlice (*data, name);
            }

            regenerateIfNeeded();
            engine.collectGarbage();
            wait (15);
        }
    }

    void FillWorker::decodeAndSlice (const juce::MemoryBlock& data, const juce::String& name)
    {
        auto stream = std::make_unique<juce::MemoryInputStream> (data, false);
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (std::move (stream)));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        {
            const juce::ScopedLock sl (viewLock);
            view.loading = false;
            view.error = "Could not read " + name + " (WAV, AIFF or FLAC)";
            ++viewVersion;
            return;
        }

        const auto length = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples, static_cast<juce::int64> (maxBreakSeconds * reader->sampleRate)));
        juce::AudioBuffer<float> buffer (static_cast<int> (std::min (2u, reader->numChannels)), length);
        reader->read (&buffer, 0, length, 0, true, buffer.getNumChannels() > 1);

        std::vector<float> left (buffer.getReadPointer (0), buffer.getReadPointer (0) + length);
        std::vector<float> right (buffer.getReadPointer (buffer.getNumChannels() - 1), buffer.getReadPointer (buffer.getNumChannels() - 1) + length);
        auto kit = std::make_unique<engine::BreakKit> (engine::buildKit (std::move (left), std::move (right), reader->sampleRate,
                                                                         name.toStdString()));
        if (kit->empty())
        {
            const juce::ScopedLock sl (viewLock);
            view.loading = false;
            view.error = name + ": no hits found";
            ++viewVersion;
            return;
        }
        installKit (std::move (kit), false);
    }

    void FillWorker::installKit (std::unique_ptr<engine::BreakKit> kit, bool builtIn, const juce::String& error)
    {
        kit->id = nextKitId.fetch_add (1);
        kitId = kit->id;
        kitSampleRate = kit->sampleRate;
        summary = engine::KitSummary::of (*kit);

        {
            const juce::ScopedLock sl (viewLock);
            view.name = builtIn ? juce::String ("Built-in break") : juce::String (kit->name);
            view.builtIn = builtIn;
            view.loading = false;
            view.error = error;
            std::array<int, engine::numSliceTypes> counts {};
            for (const auto& s : kit->slices)
                ++counts[static_cast<size_t> (s.type)];
            view.summary = juce::String (kit->slices.size()) + " slices: " + juce::String (counts[0]) + " kick, "
                           + juce::String (counts[1]) + " snare, " + juce::String (counts[2]) + " ghost, " + juce::String (counts[3]) + " hat";
            fillPeaks (view, *kit);
        }
        ++viewVersion;

        engine.publishKit (std::move (kit));
        lastRequest = {};   // force a new pattern for the new kit
    }

    void FillWorker::regenerateIfNeeded()
    {
        if (kitId == 0)
            return;

        Request r;
        r.style = juce::roundToInt (params.getRawParameterValue (ids::style)->load());
        r.complexity = juce::roundToInt (params.getRawParameterValue (ids::complexity)->load() * 10.0f);   // 0.1% steps
        r.bars = choices::fillLengthBars[static_cast<size_t> (juce::jlimit (0, 2, juce::roundToInt (params.getRawParameterValue (ids::fillLength)->load())))];
        r.qpb = quartersPerBar.load();
        r.seed = seed.load();
        r.kit = kitId;

        if (r.style == lastRequest.style && r.complexity == lastRequest.complexity && r.bars == lastRequest.bars
            && std::equal_to<double>() (r.qpb, lastRequest.qpb) && r.seed == lastRequest.seed && r.kit == lastRequest.kit)
            return;
        lastRequest = r;

        engine::FillRequest request;
        request.style = static_cast<engine::FillStyle> (juce::jlimit (0, 3, r.style));
        request.complexity = static_cast<float> (r.complexity) / 1000.0f;
        request.bars = r.bars;
        request.quartersPerBar = r.qpb;
        request.seed = r.seed;
        auto pattern = std::make_unique<engine::FillPattern> (engine::generateFill (summary, request));
        pattern->kitId = kitId;

        {
            const juce::ScopedLock sl (viewLock);
            currentPattern = std::make_shared<const engine::FillPattern> (*pattern);
            currentSummary = summary;
            view.fillDescription = pattern->description;
        }
        ++viewVersion;
        engine.publishPattern (std::move (pattern));
    }
}
