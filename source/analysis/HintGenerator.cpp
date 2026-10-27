#include "HintGenerator.h"

#include <algorithm>
#include <cmath>

namespace benchmark::analysis
{
    namespace
    {
        const SpectralView& view (const SourceAnalysis& s, bool live) { return live ? s.live : s.average; }

        bool hasData (const SourceAnalysis& s, bool live)
        {
            return view (s, live).valid && (live || s.secondsAnalysed >= HintGenerator::minSecondsForHints);
        }

        juce::String percent (float fraction) { return juce::String (juce::roundToInt (fraction * 100.0f)) + "%"; }
    }

    juce::String HintGenerator::formatDb (float db, bool withSign)
    {
        const auto text = juce::String (std::abs (db), 1);
        if (! withSign)
            return text;
        return (db >= 0.0f ? "+" : "-") + text;
    }

    juce::String HintGenerator::getWaitingMessage (const AnalysisSnapshot& s, bool live)
    {
        const auto mixReady = hasData (s.mix, live);
        const auto refReady = hasData (s.reference, live);

        if (! mixReady && ! refReady) return "Play your mix with a reference loaded to compare them.";
        if (! mixReady)               return "Play your mix to analyse it.";
        if (! refReady)               return "Play the reference to analyse it (it plays in Free mode, or with the host in Sync).";
        return {};
    }

    juce::StringArray HintGenerator::tonal (const AnalysisSnapshot& s, bool live, int maxDetails)
    {
        juce::StringArray hints;

        if (const auto waiting = getWaitingMessage (s, live); waiting.isNotEmpty())
            return { waiting };

        const auto& mix = view (s.mix, live);
        const auto& ref = view (s.reference, live);

        std::array<float, numTonalRanges> diff {};
        for (size_t i = 0; i < (size_t) numTonalRanges; ++i)
            diff[i] = std::isfinite (mix.ranges[i]) && std::isfinite (ref.ranges[i]) ? mix.ranges[i] - ref.ranges[i] : 0.0f;

        // Summary: brightness from the top two ranges, weight from the bottom two.
        const auto high = 0.5f * (diff[5] + diff[6]);
        const auto low = 0.5f * (diff[0] + diff[1]);
        juce::StringArray character;

        if (high >= tonalThresholdDb)       character.add ("brighter");
        else if (high <= -tonalThresholdDb) character.add ("darker");
        if (low >= tonalThresholdDb)        character.add ("heavier in the low end");
        else if (low <= -tonalThresholdDb)  character.add ("lighter in the low end");

        // Details: largest differences first.
        std::array<int, numTonalRanges> order {};
        for (int i = 0; i < numTonalRanges; ++i)
            order[(size_t) i] = i;
        std::sort (order.begin(), order.end(), [&] (int a, int b) { return std::abs (diff[(size_t) a]) > std::abs (diff[(size_t) b]); });

        juce::StringArray details;
        for (const auto i : order)
        {
            const auto d = diff[(size_t) i];
            if (std::abs (d) < tonalThresholdDb || details.size() >= maxDetails)
                break;

            const auto& range = tonalRanges[(size_t) i];
            // "+2.5 dB more" / "3.7 dB less": the word carries the direction.
            details.add ("Mix has " + formatDb (d, d > 0) + " dB " + (d > 0 ? "more " : "less ")
                         + range.label + " (" + range.name + ") than the reference");
        }

        if (! character.isEmpty())
            hints.add ("Overall the mix sounds " + character.joinIntoString (" and ") + " than the reference.");
        else if (details.isEmpty())
            hints.add ("Tonal balance is close to the reference: within "
                       + juce::String (tonalThresholdDb, 1) + " dB in every range.");

        hints.addArray (details);
        return hints;
    }

    juce::StringArray HintGenerator::stereo (const AnalysisSnapshot& s, bool live)
    {
        if (const auto waiting = getWaitingMessage (s, live); waiting.isNotEmpty())
            return { waiting };

        const auto& mix = view (s.mix, live).stereo;
        const auto& ref = view (s.reference, live).stereo;
        juce::StringArray hints;

        for (size_t b = 0; b < (size_t) numStereoBands; ++b)
        {
            const auto name = juce::String (stereoBandNames[b]).toLowerCase();
            const auto d = mix[b].width - ref[b].width;

            if (mix[b].correlation < 0.0f)
                hints.add ("Mix has negative correlation in the " + name + " band (" + juce::String (mix[b].correlation, 2)
                           + "): parts of it may cancel in mono");

            if (b == 0 && mix[b].width > 0.15f && d > 0.05f)
                hints.add ("Mix has more side content in the low end than the reference (" + percent (mix[b].width) + " vs "
                           + percent (ref[b].width) + "): check mono compatibility");
            else if (d >= widthThreshold)
                hints.add ("Mix is wider in the " + name + " band (" + percent (mix[b].width) + " side vs " + percent (ref[b].width) + ")");
            else if (d <= -widthThreshold)
                hints.add ("Mix is narrower in the " + name + " band (" + percent (mix[b].width) + " side vs " + percent (ref[b].width) + ")");
        }

        if (hints.isEmpty())
            hints.add ("Stereo width is close to the reference in every band.");

        return hints;
    }

    juce::StringArray HintGenerator::dynamics (const AnalysisSnapshot& s)
    {
        const auto& mix = s.mix.dynamics;
        const auto& ref = s.reference.dynamics;

        if (! std::isfinite (mix.plr) || ! std::isfinite (ref.plr))
            return { getWaitingMessage (s, false).isNotEmpty() ? getWaitingMessage (s, false)
                                                               : juce::String ("Keep playing: dynamics need a few seconds of audio.") };

        juce::StringArray hints;
        const auto plrDiff = mix.plr - ref.plr;

        if (plrDiff <= -plrThresholdDb)
            hints.add ("Mix is more compressed or limited: PLR " + formatDb (mix.plr, false) + " dB vs "
                       + formatDb (ref.plr, false) + " dB in the reference");
        else if (plrDiff >= plrThresholdDb)
            hints.add ("Mix has more peak headroom (less limiting): PLR " + formatDb (mix.plr, false) + " dB vs "
                       + formatDb (ref.plr, false) + " dB in the reference");

        const auto crestDiff = mix.crestDb - ref.crestDb;
        if (std::isfinite (crestDiff) && std::abs (crestDiff) >= crestThresholdDb)
            hints.add (juce::String (crestDiff > 0 ? "Mix is punchier (higher crest factor): " : "Mix has softer transients (lower crest factor): ")
                       + formatDb (mix.crestDb, false) + " dB vs " + formatDb (ref.crestDb, false) + " dB");

        const auto lraDiff = mix.loudnessRange - ref.loudnessRange;
        if (s.mix.secondsAnalysed > 10.0 && s.reference.secondsAnalysed > 10.0 && std::abs (lraDiff) >= lraThresholdLU)
            hints.add (juce::String (lraDiff > 0 ? "Mix varies more in loudness: " : "Mix is more even in loudness: ")
                       + "LRA " + juce::String (mix.loudnessRange, 1) + " LU vs " + juce::String (ref.loudnessRange, 1) + " LU");

        if (hints.isEmpty())
            hints.add ("Dynamics are close to the reference (PLR within " + juce::String (plrThresholdDb, 1) + " dB).");

        return hints;
    }

    juce::String HintGenerator::sectionTitle (Section section)
    {
        switch (section)
        {
            case Section::tonal:    return "Tonal balance";
            case Section::stereo:   return "Stereo width";
            case Section::dynamics: return "Dynamics";
        }
        return {};
    }

    juce::StringArray HintGenerator::notesFor (const AnalysisSnapshot& s, Section section, bool live)
    {
        switch (section)
        {
            case Section::tonal:    return tonal (s, live);
            case Section::stereo:   return stereo (s, live);
            case Section::dynamics: return dynamics (s);
        }
        return {};
    }

    juce::String HintGenerator::formatReport (const AnalysisSnapshot& s, bool live, const juce::String& referenceName,
                                              std::optional<Section> only)
    {
        const auto duration = [] (double seconds)
        {
            const auto total = juce::roundToInt (seconds);
            return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
        };

        juce::StringArray lines;
        lines.add ("Benchmark comparison" + (referenceName.isNotEmpty() ? " vs \"" + referenceName + "\"" : juce::String()));
        lines.add (juce::String (live ? "Live view" : "Average since reset") + ": mix " + duration (s.mix.secondsAnalysed)
                   + ", reference " + duration (s.reference.secondsAnalysed) + " analysed");

        for (int i = 0; i < numSections; ++i)
        {
            const auto section = (Section) i;
            if (only.has_value() && *only != section)
                continue;

            lines.add ({});
            lines.add (sectionTitle (section));

            // Dynamics are always measured since reset.
            for (const auto& note : notesFor (s, section, section == Section::dynamics ? false : live))
                lines.add ("- " + note);
        }

        return lines.joinIntoString ("\n") + "\n";
    }
}
