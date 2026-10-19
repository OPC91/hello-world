#include "BuiltInKits.h"
#include "DrumSynth.h"

namespace bf::BuiltInKits
{

namespace
{
    /** What makes a kit sound like itself. */
    struct Flavour
    {
        const char* name;
        double kickStart, kickEnd, kickDecay, kickClick, kickDrive;
        double snareTone, snareDecay, snareHp, snareDrive;
        double hatTune, hatDecay, hatHp;
        DrumSynth::Colour colour;
    };

    // Clean: tight and modern, for liquid/rollers.
    // Neuro: hard, bright and saturated.
    // Jungle: lower, looser, ringier, through a 12-bit / 26 kHz "vintage sampler".
    constexpr Flavour flavours[] = {
        { "Clean",  185.0, 50.0, 0.30, 0.45, 1.5,   190.0, 0.15, 1500.0, 1.6,   1.00, 0.040, 7500.0, { 1.1, 0.0, 0 } },
        { "Neuro",  240.0, 46.0, 0.26, 0.85, 3.2,   205.0, 0.12, 1900.0, 3.4,   1.12, 0.032, 8500.0, { 1.9, 0.0, 0 } },
        { "Jungle", 150.0, 54.0, 0.40, 0.30, 1.8,   230.0, 0.19, 1100.0, 2.2,   0.92, 0.050, 6000.0, { 1.4, 26040.0, 12 } },
    };

    const Flavour* findFlavour (const juce::String& kit)
    {
        for (const auto& f : flavours)
            if (kit == f.name)
                return &f;

        return nullptr;
    }

    struct Sound
    {
        const char* name;
        std::vector<Role> roles; // roles it is offered for, first = primary
    };

    const std::vector<Sound>& sounds()
    {
        static const std::vector<Sound> s {
            { "Kick A",        { Role::Kick } },
            { "Kick B",        { Role::Kick } },
            { "Snare A",       { Role::Snare } },
            { "Snare B",       { Role::Snare } },
            { "Snare Ghost",   { Role::GhostSnare } },
            { "Rimshot",       { Role::Perc, Role::GhostSnare } },
            { "Closed Hat A",  { Role::ClosedHat } },
            { "Closed Hat B",  { Role::ClosedHat } },
            { "Shaker",        { Role::Perc, Role::ClosedHat } },
            { "Open Hat",      { Role::OpenHat } },
            { "Ride",          { Role::Ride } },
            { "Ride Bell",     { Role::Ride } },
            { "Clap",          { Role::Perc, Role::Snare } },
            { "Reverse Crash", { Role::FX } },
            { "Crash",         { Role::FX, Role::Ride } },
        };
        return s;
    }

    juce::AudioBuffer<float> renderSound (const Flavour& f, const juce::String& sound, double sr)
    {
        using namespace DrumSynth;
        juce::AudioBuffer<float> b;
        float peak = 0.9f;
        bool colour = true;

        if (sound.startsWith ("Kick"))
        {
            KickParams k;
            k.startHz = f.kickStart;
            k.endHz = f.kickEnd;
            k.ampDecay = f.kickDecay;
            k.click = f.kickClick;
            k.drive = f.kickDrive;

            if (sound == "Kick B") // shorter, punchier alternative with a faster sweep
            {
                k.startHz *= 1.25;
                k.endHz *= 1.08;
                k.pitchDecay = 0.022;
                k.ampDecay *= 0.62;
                k.length = 0.5;
                k.click = juce::jmin (1.0, k.click * 1.4);
            }

            b = renderKick (sr, k, 11);
            peak = 0.97f;
        }
        else if (sound.startsWith ("Snare"))
        {
            SnareParams s;
            s.toneHz = f.snareTone;
            s.noiseDecay = f.snareDecay;
            s.noiseHp = f.snareHp;
            s.drive = f.snareDrive;

            if (sound == "Snare B") // higher and drier, more crack
            {
                s.toneHz *= 1.16;
                s.toneDecay = 0.06;
                s.noiseDecay *= 0.75;
                s.click = 0.9;
                s.noiseLevel = 0.9;
            }
            else if (sound == "Snare Ghost") // soft brush of the wires, little body or stick
            {
                s.toneLevel = 0.35;
                s.toneDecay = 0.04;
                s.noiseDecay *= 0.6;
                s.noiseHp *= 1.3;
                s.click = 0.15;
                s.length = 0.25;
                s.drive = 1.2;
                peak = 0.7f;
            }

            b = renderSnare (sr, s, sound == "Snare B" ? 23 : 21);
        }
        else if (sound.startsWith ("Closed Hat") || sound == "Open Hat" || sound.startsWith ("Ride") || sound == "Crash")
        {
            MetalParams m;
            m.tune = f.hatTune;
            m.decay = f.hatDecay;
            m.hp = f.hatHp;

            if (sound == "Closed Hat B") { m.tune *= 1.09; m.decay *= 0.6; m.noise = 0.45; m.length = 0.15; }
            else if (sound == "Open Hat") { m.decay = 0.34; m.length = 1.1; m.noise = 0.35; }
            else if (sound == "Ride")     { m.tune *= 0.78; m.decay = 0.9; m.length = 2.6; m.hp = 4200.0; m.bandHz = 8000.0; m.noise = 0.12; m.bell = 0.25; }
            else if (sound == "Ride Bell"){ m.tune *= 0.82; m.decay = 0.7; m.length = 2.2; m.hp = 2600.0; m.bandHz = 5000.0; m.noise = 0.05; m.bell = 1.0; }
            else if (sound == "Crash")    { m.tune *= 0.62; m.decay = 0.9; m.length = 2.8; m.hp = 2800.0; m.bandHz = 7000.0; m.noise = 0.9; m.width = 1.0; }

            b = renderMetal (sr, m, 31);
            peak = sound.startsWith ("Closed") ? 0.8f : 0.75f;
        }
        else if (sound == "Rimshot")       b = renderRimshot (sr, f.snareTone / 190.0, 41);
        else if (sound == "Clap")          b = renderClap (sr, f.snareTone / 190.0, 43);
        else if (sound == "Shaker")        { b = renderShaker (sr, f.hatTune, 47); peak = 0.7f; }
        else if (sound == "Reverse Crash") { b = renderReverseCrash (sr, 1.6, f.hatTune, 53); colour = false; peak = 0.8f; }

        if (b.getNumSamples() > 0 && colour)
            applyColour (b, sr, f.colour, peak);

        return b;
    }
}

juce::StringArray kitNames()
{
    juce::StringArray names;

    for (const auto& f : flavours)
        names.add (f.name);

    return names;
}

juce::StringArray soundsFor (const juce::String& kit, Role role)
{
    juce::StringArray primary, secondary;

    if (findFlavour (kit) == nullptr)
        return {};

    for (const auto& s : sounds())
    {
        auto it = s.roles.begin();

        if (*it == role)
            primary.add (kit + "/" + s.name);
        else if (std::find (s.roles.begin(), s.roles.end(), role) != s.roles.end())
            secondary.add (kit + "/" + s.name);
    }

    primary.addArray (secondary);
    return primary;
}

juce::AudioBuffer<float> render (const juce::String& id, double sampleRate)
{
    const auto kit = id.upToFirstOccurrenceOf ("/", false, false);
    const auto sound = id.fromFirstOccurrenceOf ("/", false, false);

    if (const auto* f = findFlavour (kit); f != nullptr && sampleRate > 0.0)
        return renderSound (*f, sound, sampleRate);

    return {};
}

SampleRef defaultFor (Role role, const juce::String& kit)
{
    const auto list = soundsFor (findFlavour (kit) != nullptr ? kit : juce::String ("Clean"), role);
    return list.isEmpty() ? SampleRef() : SampleRef::builtIn (list[0]);
}

} // namespace bf::BuiltInKits
