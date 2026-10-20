#include "StateIO.h"

namespace bf::StateIO
{

namespace
{
    const juce::Identifier idPattern ("Pattern"), idBank ("Patterns"), idTrack ("Track"), idStep ("S"),
        idGroove ("Groove"), idIndex ("i"), idLength ("length"), idResolution ("res"), idAccents ("accents"),
        idOn ("on"), idVel ("v"), idProb ("p"), idRatchet ("r"), idNudge ("n"), idPitch ("pt"), idGate ("g"), idSample ("smp"),
        idSwing ("swing"), idTiming ("timing"), idVelocity ("velocity");

    juce::String floatsToString (const float* values, int num)
    {
        juce::StringArray s;

        for (int i = 0; i < num; ++i)
            s.add (juce::String (values[i], 4));

        return s.joinIntoString (" ");
    }

    void floatsFromString (const juce::String& text, float* values, int num)
    {
        const auto tokens = juce::StringArray::fromTokens (text, " ", {});

        for (int i = 0; i < juce::jmin (num, tokens.size()); ++i)
            values[i] = tokens[i].getFloatValue();
    }
}

juce::ValueTree patternToTree (const Pattern& p)
{
    juce::ValueTree tree (idPattern);
    tree.setProperty (idLength, p.length, nullptr);
    tree.setProperty (idResolution, (int) p.resolution, nullptr);

    juce::String accents;

    for (int i = 0; i < kMaxSteps; ++i)
        accents << (p.accents[(size_t) i] ? '1' : '0');

    tree.setProperty (idAccents, accents, nullptr);

    for (int t = 0; t < kNumTracks; ++t)
    {
        juce::ValueTree track (idTrack);
        track.setProperty (idIndex, t, nullptr);

        for (int s = 0; s < kMaxSteps; ++s)
        {
            const auto& step = p.tracks[(size_t) t].steps[(size_t) s];

            if (step == Step {})
                continue;

            juce::ValueTree st (idStep);
            st.setProperty (idIndex, s, nullptr);
            st.setProperty (idOn, step.on, nullptr);
            st.setProperty (idVel, (int) step.velocity, nullptr);
            if (step.probability != 100) st.setProperty (idProb, (int) step.probability, nullptr);
            if (step.ratchet != 1)       st.setProperty (idRatchet, (int) step.ratchet, nullptr);
            if (! juce::exactlyEqual (step.nudge, 0.0f))    st.setProperty (idNudge, step.nudge, nullptr);
            if (step.pitch != 0)         st.setProperty (idPitch, (int) step.pitch, nullptr);
            if (! juce::exactlyEqual (step.length, 1.0f))     st.setProperty (idGate, step.length, nullptr);
            if (step.sample != 0)                             st.setProperty (idSample, (int) step.sample, nullptr);
            track.appendChild (st, nullptr);
        }

        if (track.getNumChildren() > 0)
            tree.appendChild (track, nullptr);
    }

    return tree;
}

Pattern patternFromTree (const juce::ValueTree& tree)
{
    Pattern p;

    if (! tree.hasType (idPattern))
        return p;

    p.length = (int) tree.getProperty (idLength, 16) == 32 ? 32 : 16;
    p.resolution = (int) tree.getProperty (idResolution, 0) == 1 ? Resolution::ThirtySecond : Resolution::Sixteenth;

    const auto accents = tree.getProperty (idAccents).toString();

    for (int i = 0; i < juce::jmin (kMaxSteps, accents.length()); ++i)
        p.accents[(size_t) i] = accents[i] == '1';

    for (const auto& track : tree)
    {
        const int t = track.getProperty (idIndex, -1);

        if (! track.hasType (idTrack) || t < 0 || t >= kNumTracks)
            continue;

        for (const auto& st : track)
        {
            const int s = st.getProperty (idIndex, -1);

            if (! st.hasType (idStep) || s < 0 || s >= kMaxSteps)
                continue;

            auto& step = p.tracks[(size_t) t].steps[(size_t) s];
            step.on = st.getProperty (idOn, false);
            step.velocity = (std::uint8_t) juce::jlimit (1, 127, (int) st.getProperty (idVel, 100));
            step.probability = (std::uint8_t) juce::jlimit (0, 100, (int) st.getProperty (idProb, 100));
            step.ratchet = (std::uint8_t) juce::jlimit (1, 4, (int) st.getProperty (idRatchet, 1));
            step.nudge = juce::jlimit (-0.5f, 0.5f, (float) st.getProperty (idNudge, 0.0f));
            step.pitch = (std::int8_t) juce::jlimit (-12, 12, (int) st.getProperty (idPitch, 0));
            step.length = juce::jlimit (0.1f, 16.0f, (float) st.getProperty (idGate, 1.0f));
            step.sample = (std::uint8_t) juce::jlimit (0, kSampleVariants - 1, (int) st.getProperty (idSample, 0));
        }
    }

    return p;
}

juce::ValueTree bankToTree (const PatternBank& bank)
{
    juce::ValueTree tree (idBank);

    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto pt = patternToTree (bank.patterns[(size_t) i]);
        pt.setProperty (idIndex, i, nullptr);
        tree.appendChild (pt, nullptr);
    }

    return tree;
}

PatternBank bankFromTree (const juce::ValueTree& tree)
{
    PatternBank bank;

    for (const auto& pt : tree)
    {
        const int i = pt.getProperty (idIndex, -1);

        if (i >= 0 && i < kNumPatterns)
            bank.patterns[(size_t) i] = patternFromTree (pt);
    }

    return bank;
}

juce::ValueTree grooveToTree (const GrooveTemplate& g)
{
    juce::ValueTree tree (idGroove);
    tree.setProperty (idSwing, g.swing, nullptr);
    tree.setProperty (idTiming, floatsToString (g.timing.data(), 16), nullptr);
    tree.setProperty (idVelocity, floatsToString (g.velocity.data(), 16), nullptr);
    return tree;
}

GrooveTemplate grooveFromTree (const juce::ValueTree& tree)
{
    GrooveTemplate g;

    if (! tree.hasType (idGroove))
        return g;

    g.swing = juce::jlimit (0.5f, 0.75f, (float) tree.getProperty (idSwing, 0.5f));
    floatsFromString (tree.getProperty (idTiming).toString(), g.timing.data(), 16);
    floatsFromString (tree.getProperty (idVelocity).toString(), g.velocity.data(), 16);

    for (auto& t : g.timing)   t = juce::jlimit (-0.5f, 0.5f, t);
    for (auto& v : g.velocity) v = juce::jlimit (0.0f, 2.0f, v);

    return g;
}

juce::String patternToString (const Pattern& p)
{
    return patternToTree (p).toXmlString();
}

bool patternFromString (const juce::String& text, Pattern& out)
{
    const auto tree = juce::ValueTree::fromXml (text);

    if (! tree.hasType (idPattern))
        return false;

    out = patternFromTree (tree);
    return true;
}

} // namespace bf::StateIO
