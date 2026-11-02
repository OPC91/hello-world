#include "Presets.h"
#include "Parameters.h"

namespace seam
{
    juce::StringArray presetParameterIds()
    {
        return { ids::style, ids::complexity, ids::fillLength, ids::fillMode, ids::vacuum, ids::vacuumSize, ids::tapeStop,
                 ids::autoMode, ids::autoBars, ids::autoCurve, ids::amtFilter, ids::amtReverb, ids::amtStutter, ids::amtDuck,
                 ids::amtFill };
    }

    namespace
    {
        struct Spec
        {
            const char* name; const char* tagline;
            int style; float complexity; int fillLength; int fillMode;
            bool vacuum; int vacuumSize; bool tapeStop;
            bool autoMode; int autoBars; int autoCurve;
            float filter, reverb, stutter, duck, fill;
        };

        // fillLength: 0 = 1 bar, 1 = 2 bars, 2 = 4 bars. vacuumSize: 0 = 1/16, 1 = 1/8, 2 = 1/4.
        // autoBars: 0 = 2, 1 = 4, 2 = 8, 3 = 16 bars. autoCurve: 0 = linear, 1 = late surge. fillMode: 0 mix, 1 replace.
        const Spec specs[] {
            // Liquid
            { "Liquid Glide",       "smooth seams, rolling in.",          0, 50.0f, 0, 0, false, 1, false, false, 1, 0, 100, 100, 100, 100, 100 },
            { "Rolling Silk",       "soft roll, long throw.",             0, 40.0f, 1, 0, true,  0, false, true,  1, 0,  80, 135,  60,  70,  90 },
            { "Sunset Lift",        "four bars of lift, then air.",       0, 60.0f, 2, 0, true,  1, true,  true,  2, 1,  90, 120,  80,  80, 100 },
            // Neuro
            { "Neuro Charge",       "tear it up at the turn.",            1, 70.0f, 1, 1, true,  1, false, true,  1, 1, 120, 100, 130, 110, 110 },
            { "Reese Tear",         "chop, stall, slam.",                 1, 85.0f, 0, 1, true,  0, true,  false, 1, 0, 110,  90, 140, 120, 115 },
            { "Halftime Pressure",  "drop into half-time, hold, release.",1, 55.0f, 2, 0, true,  2, false, true,  2, 0, 100,  80, 100, 100, 100 },
            // Jungle
            { "Amen Rinse",         "chop it. flip it. drop it.",         2, 90.0f, 1, 1, false, 1, false, true,  1, 0, 100, 110, 100,  90, 110 },
            { "Rewind Selecta",     "pull up and wheel it.",              2, 75.0f, 0, 0, true,  1, true,  false, 1, 0,  90, 125,  90,  80, 100 },
            { "Junglist Roll",      "sixteen bars of rolling pressure.",  2, 100.0f,2, 1, true,  0, false, true,  3, 1, 110, 100, 110, 100, 120 },
            // Jump-Up
            { "Jump-Up Wind",       "wind it up. let it go.",             3, 60.0f, 1, 1, true,  1, false, true,  1, 1, 110, 100, 110, 130, 110 },
            { "Bounce Spring",      "boing, gap, bounce.",                3, 50.0f, 0, 0, true,  2, true,  false, 1, 0, 100,  90, 100, 120, 100 },
            { "Dancefloor Detonator","everything, then nothing, then everything.", 3, 80.0f, 2, 1, true, 2, true, true, 2, 1, 130, 110, 120, 120, 125 },
        };
    }

    std::vector<exnilo::Preset> factoryPresets()
    {
        std::vector<exnilo::Preset> list;
        for (const auto& s : specs)
        {
            exnilo::Preset p;
            p.name = s.name;
            p.category = choices::style[s.style];
            p.values = {
                { ids::style, static_cast<float> (s.style) },        { ids::complexity, s.complexity },
                { ids::fillLength, static_cast<float> (s.fillLength) }, { ids::fillMode, static_cast<float> (s.fillMode) },
                { ids::vacuum, s.vacuum ? 1.0f : 0.0f },               { ids::vacuumSize, static_cast<float> (s.vacuumSize) },
                { ids::tapeStop, s.tapeStop ? 1.0f : 0.0f },           { ids::autoMode, s.autoMode ? 1.0f : 0.0f },
                { ids::autoBars, static_cast<float> (s.autoBars) },     { ids::autoCurve, static_cast<float> (s.autoCurve) },
                { ids::amtFilter, s.filter }, { ids::amtReverb, s.reverb }, { ids::amtStutter, s.stutter },
                { ids::amtDuck, s.duck },     { ids::amtFill, s.fill },
            };
            list.push_back (std::move (p));
        }
        return list;
    }

    juce::String presetTagline (const juce::String& presetName)
    {
        for (const auto& s : specs)
            if (presetName == s.name)
                return s.tagline;
        return {};
    }
}
