#include "Params.h"
#include "Tracks.h"

namespace bf
{

juce::String ParamID::track (int trackIndex, const char* suffix)
{
    return "t" + juce::String (trackIndex + 1) + "_" + suffix;
}

juce::StringArray subStyleNames()
{
    return { "Liquid", "Jump-Up", "Neuro", "Jungle", "Halftime", "Rollers", "Minimal" };
}

juce::StringArray grooveNames()
{
    return { "Straight", "Light Shuffle", "Heavy Swing", "Amen Feel", "Rolling", "User" };
}

namespace
{
    using Float  = juce::AudioParameterFloat;
    using Bool   = juce::AudioParameterBool;
    using Choice = juce::AudioParameterChoice;
    using Int    = juce::AudioParameterInt;
    using Attr   = juce::AudioParameterFloatAttributes;

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    Attr withSuffix (const juce::String& suffix, int decimals = 1)
    {
        return Attr().withLabel (suffix).withStringFromValueFunction ([decimals] (float v, int) {
            return juce::String (v, decimals);
        });
    }

    Attr percent()
    {
        return Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); });
    }

    Attr milliseconds()
    {
        return Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0);
        });
    }

    Attr hertz()
    {
        return Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (v));
        });
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto pid = [] (const juce::String& id) { return juce::ParameterID { id, 1 }; };

    layout.add (std::make_unique<Int> (pid (ParamID::pattern), "Pattern", 1, kNumPatterns, 1));
    layout.add (std::make_unique<Float> (pid (ParamID::swing), "Swing", juce::NormalisableRange<float> (50.0f, 75.0f, 0.1f), 50.0f, withSuffix ("%")));
    layout.add (std::make_unique<Bool> (pid (ParamID::swing8), "Swing 8ths", false));
    layout.add (std::make_unique<Choice> (pid (ParamID::groove), "Groove Template", grooveNames(), 0));
    layout.add (std::make_unique<Float> (pid (ParamID::humanizeTime), "Humanize Time", juce::NormalisableRange<float> (0.0f, 20.0f, 0.1f), 0.0f, withSuffix ("ms")));
    layout.add (std::make_unique<Float> (pid (ParamID::humanizeVel), "Humanize Velocity", juce::NormalisableRange<float> (0.0f, 30.0f, 0.1f), 0.0f, percent()));
    layout.add (std::make_unique<Float> (pid (ParamID::velocityDepth), "Velocity Depth", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 80.0f, percent()));
    layout.add (std::make_unique<Float> (pid (ParamID::accentAmount), "Accent Amount", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, percent()));
    layout.add (std::make_unique<Choice> (pid (ParamID::subStyle), "Sub-style", subStyleNames(), 0));
    layout.add (std::make_unique<Float> (pid (ParamID::variation), "Variation", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f, percent()));
    layout.add (std::make_unique<Float> (pid (ParamID::drive), "Drive", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f, percent()));
    layout.add (std::make_unique<Bool> (pid (ParamID::driveOn), "Drive On", true));
    layout.add (std::make_unique<Bool> (pid (ParamID::limiterOn), "Limiter On", true));
    layout.add (std::make_unique<Float> (pid (ParamID::masterVolume), "Master Volume", juce::NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f, withSuffix ("dB")));
    layout.add (std::make_unique<Bool> (pid (ParamID::multiOut), "Multi-Out", false));
    layout.add (std::make_unique<Bool> (pid (ParamID::midiOut), "MIDI Out", false));
    layout.add (std::make_unique<Choice> (pid (ParamID::midiPatternMode), "Pattern MIDI Mode", juce::StringArray { "Latch", "Hold", "Off" }, 0));

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& def = kTrackDefaults[(size_t) t];
        const juce::String prefix = juce::String (def.name) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("track" + juce::String (t + 1), def.name, " | ");
        const auto id = [t, &pid] (const char* s) { return pid (ParamID::track (t, s)); };

        group->addChild (std::make_unique<Float> (id (ParamID::volume), prefix + "Volume", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.5f), 0.0f, withSuffix ("dB")));
        group->addChild (std::make_unique<Float> (id (ParamID::pan), prefix + "Pan", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f, withSuffix ("", 2)));
        group->addChild (std::make_unique<Float> (id (ParamID::pitch), prefix + "Pitch", juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f, withSuffix ("st", 0)));
        group->addChild (std::make_unique<Float> (id (ParamID::fine), prefix + "Fine Tune", juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f, withSuffix ("ct", 0)));
        group->addChild (std::make_unique<Float> (id (ParamID::start), prefix + "Start", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percent()));
        group->addChild (std::make_unique<Choice> (id (ParamID::envMode), prefix + "Envelope Mode", juce::StringArray { "One-Shot", "ADSR" }, 0));
        group->addChild (std::make_unique<Float> (id (ParamID::attack), prefix + "Attack", skewed (0.0f, 500.0f, 30.0f), 0.0f, milliseconds()));
        group->addChild (std::make_unique<Float> (id (ParamID::decay), prefix + "Decay", skewed (5.0f, 5000.0f, 400.0f), 5000.0f, milliseconds()));
        group->addChild (std::make_unique<Float> (id (ParamID::sustain), prefix + "Sustain", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent()));
        group->addChild (std::make_unique<Float> (id (ParamID::release), prefix + "Release", skewed (5.0f, 5000.0f, 300.0f), 60.0f, milliseconds()));
        group->addChild (std::make_unique<Float> (id (ParamID::lowPass), prefix + "Low-Pass", skewed (20.0f, 20000.0f, 1000.0f), 20000.0f, hertz()));
        group->addChild (std::make_unique<Float> (id (ParamID::highPass), prefix + "High-Pass", skewed (20.0f, 20000.0f, 1000.0f), 20.0f, hertz()));
        group->addChild (std::make_unique<Float> (id (ParamID::resonance), prefix + "Resonance", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percent()));
        group->addChild (std::make_unique<Choice> (id (ParamID::choke), prefix + "Choke Group", juce::StringArray { "Off", "1", "2", "3", "4" }, def.chokeGroup));
        group->addChild (std::make_unique<Bool> (id (ParamID::mute), prefix + "Mute", false));
        group->addChild (std::make_unique<Bool> (id (ParamID::solo), prefix + "Solo", false));
        group->addChild (std::make_unique<Float> (id (ParamID::velFilter), prefix + "Velocity > Filter", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, percent()));
        group->addChild (std::make_unique<Float> (id (ParamID::velPitch), prefix + "Velocity > Pitch", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, withSuffix ("st", 2)));
        group->addChild (std::make_unique<Bool> (id (ParamID::swingOn), prefix + "Swing Override", false));
        group->addChild (std::make_unique<Float> (id (ParamID::swingAmt), prefix + "Swing", juce::NormalisableRange<float> (50.0f, 75.0f, 0.1f), 50.0f, withSuffix ("%")));

        layout.add (std::move (group));
    }

    return layout;
}

// -----------------------------------------------------------------------------

namespace
{
    constexpr const char* globalIds[] = {
        ParamID::pattern, ParamID::swing, ParamID::swing8, ParamID::groove, ParamID::humanizeTime,
        ParamID::humanizeVel, ParamID::velocityDepth, ParamID::accentAmount, ParamID::subStyle,
        ParamID::variation, ParamID::drive, ParamID::driveOn, ParamID::limiterOn, ParamID::masterVolume,
        ParamID::multiOut, ParamID::midiOut, ParamID::midiPatternMode
    };

    constexpr const char* trackIds[] = {
        ParamID::volume, ParamID::pan, ParamID::pitch, ParamID::fine, ParamID::start, ParamID::envMode,
        ParamID::attack, ParamID::decay, ParamID::sustain, ParamID::release, ParamID::lowPass,
        ParamID::highPass, ParamID::resonance, ParamID::choke, ParamID::mute, ParamID::solo,
        ParamID::velFilter, ParamID::velPitch, ParamID::swingOn, ParamID::swingAmt
    };

    static_assert (std::size (globalIds) == 17);
    static_assert (std::size (trackIds) == 20);
}

ParamCache::ParamCache (juce::AudioProcessorValueTreeState& apvts)
{
    for (size_t i = 0; i < std::size (globalIds); ++i)
    {
        global[i] = apvts.getRawParameterValue (globalIds[i]);
        jassert (global[i] != nullptr);
    }

    for (int t = 0; t < kNumTracks; ++t)
    {
        for (size_t i = 0; i < std::size (trackIds); ++i)
        {
            tracks[(size_t) t].p[i] = apvts.getRawParameterValue (ParamID::track (t, trackIds[i]));
            jassert (tracks[(size_t) t].p[i] != nullptr);
        }
    }
}

void ParamCache::read (EngineParams& e) const noexcept
{
    const auto g = [this] (int i) { return global[i]->load (std::memory_order_relaxed); };

    e.pattern         = juce::jlimit (0, kNumPatterns - 1, (int) g (0) - 1);
    e.swing           = g (1) * 0.01f;
    e.swing8          = g (2) > 0.5f;
    e.groove          = juce::jlimit (0, kNumGrooves - 1, (int) g (3));
    e.humanizeTimeMs  = g (4);
    e.humanizeVel     = g (5) * 0.01f;
    e.velocityDepth   = g (6) * 0.01f;
    e.accentAmount    = g (7) * 0.01f;
    e.drive           = g (10) * 0.01f;
    e.driveOn         = g (11) > 0.5f;
    e.limiterOn       = g (12) > 0.5f;
    e.masterGain      = juce::Decibels::decibelsToGain (g (13), -48.0f);
    e.multiOut        = g (14) > 0.5f;
    e.midiOut         = g (15) > 0.5f;
    e.midiPatternMode = (MidiPatternMode) juce::jlimit (0, 2, (int) g (16));

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& p = tracks[(size_t) t].p;
        const auto v = [&p] (int i) { return p[i]->load (std::memory_order_relaxed); };
        auto& tp = e.tracks[(size_t) t];

        tp.gain          = juce::Decibels::decibelsToGain (v (0), -59.9f);
        tp.pan           = v (1);
        tp.pitch         = v (2) + v (3) * 0.01f;
        tp.start         = v (4) * 0.01f;
        tp.envMode       = v (5) > 0.5f ? EnvMode::ADSR : EnvMode::OneShot;
        tp.attackMs      = v (6);
        tp.decayMs       = v (7);
        tp.sustain       = v (8) * 0.01f;
        tp.releaseMs     = v (9);
        tp.lowPassHz     = v (10);
        tp.highPassHz    = v (11);
        tp.resonance     = v (12) * 0.01f;
        tp.chokeGroup    = juce::jlimit (0, kNumChokeGroups, (int) v (13));
        tp.mute          = v (14) > 0.5f;
        tp.solo          = v (15) > 0.5f;
        tp.velToFilter   = v (16) * 0.01f;
        tp.velToPitch    = v (17);
        tp.swingOverride = v (18) > 0.5f;
        tp.swing         = v (19) * 0.01f;
    }
}

} // namespace bf
