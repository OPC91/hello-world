#pragma once

namespace exnilo
{
    /** The Exnilo base palette (same values as exnilo.co.uk and Nil2Max). Everything is drawn from these and the
        current genre theme's two accents; there are no bitmap assets. */
    namespace palette
    {
        inline const juce::Colour bg        { 0xff0e0a1c };
        inline const juce::Colour bg2       { 0xff140f27 };
        inline const juce::Colour surface   { 0xff1c1534 };
        inline const juce::Colour surface2  { 0xff261d45 };
        inline const juce::Colour line      { 0xff352a5c };
        inline const juce::Colour ink       { 0xfff4eefe };
        inline const juce::Colour dim       { 0xffa99fcb };
        inline const juce::Colour onAccent  { 0xff1b0f20 };  ///< text on an accent-filled control
        inline const juce::Colour meterLow  { 0xff5fd9a0 };
        inline const juce::Colour meterMid  { 0xffffd84a };
        inline const juce::Colour meterHigh { 0xffff5a5a };
    }

    /** Shape the energy display draws its rings in. */
    enum class RingShape { circle, square, triangle, hexagon, streaks, sparkles };

    /** A genre colour scheme. Picking a genre tints the whole plugin. */
    struct GenreTheme
    {
        const char* id;
        const char* name;
        juce::Colour accentA;
        juce::Colour accentB;
        const char* tagline;
        RingShape rings;
        bool rain = false;        ///< falling streaks behind the core (drum & bass)

        /** Background tint: the base palette nudged towards this theme's accent. */
        juce::Colour tint (juce::Colour base, float amount = 0.06f) const
        {
            // Desaturate the purple base towards neutral first, so themes far from violet (teal, orange) still read as theirs.
            return base.interpolatedWith (juce::Colour::greyLevel (base.getPerceivedBrightness()), 0.35f)
                       .interpolatedWith (accentA, juce::jmin (1.0f, amount * 1.7f));
        }
        /** Accent colour at position t (0 = A, 1 = B) along the theme gradient. */
        juce::Colour at (float t) const { return accentA.interpolatedWith (accentB, juce::jlimit (0.0f, 1.0f, t)); }
    };

    namespace themes
    {
        const std::vector<GenreTheme>& all();
        /** Theme with this id, or the first theme (Classic) if there is none. */
        const GenreTheme& byId (const juce::String& id);
        int indexOf (const juce::String& id);
        inline constexpr const char* drumAndBass = "drum-and-bass";
    }

    /** Fonts. Segoe UI on Windows (like Nil2Max); elsewhere the platform's sans-serif. */
    namespace fonts
    {
        juce::Font regular (float height);
        juce::Font bold (float height);
        juce::Font heavy (float height);   ///< black weight, for numbers and the wordmark
    }
}
