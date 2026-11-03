namespace exnilo
{
    namespace themes
    {
        const std::vector<GenreTheme>& all()
        {
            static const std::vector<GenreTheme> list {
                { "classic",       "Classic",       juce::Colour (0xffff5fa2), juce::Colour (0xffffb547), "from nothing to too much.",             RingShape::circle,   false },
                { "house",         "House",         juce::Colour (0xffffb000), juce::Colour (0xffff5e3a), "four on the floor, hands in the air.",  RingShape::circle,   false },
                { "techno",        "Techno",        juce::Colour (0xffd8d8de), juce::Colour (0xffff2d2d), "warehouse pressure. no mercy.",         RingShape::square,   false },
                { "trance",        "Trance",        juce::Colour (0xff39d0ff), juce::Colour (0xffb46bff), "hands up. eyes closed. here it comes.", RingShape::streaks,  false },
                { "dubstep",       "Dubstep",       juce::Colour (0xff9dff3a), juce::Colour (0xffc13bff), "brace for the wub.",                    RingShape::triangle, false },
                { "drum-and-bass", "Drum & Bass",   juce::Colour (0xff00e0c6), juce::Colour (0xffffd400), "174 and climbing.",                     RingShape::hexagon,  true  },
                { "future-bass",   "Future Bass",   juce::Colour (0xffff8ad8), juce::Colour (0xff8ae0ff), "big chords, bigger feelings.",          RingShape::sparkles, false },
            };
            return list;
        }

        int indexOf (const juce::String& id)
        {
            const auto& list = all();
            for (size_t i = 0; i < list.size(); ++i)
                if (id == list[i].id)
                    return static_cast<int> (i);
            return -1;
        }

        const GenreTheme& byId (const juce::String& id)
        {
            const int index = indexOf (id);
            return all()[static_cast<size_t> (index < 0 ? 0 : index)];
        }
    }

    namespace fonts
    {
        static juce::Font make (float height, const char* style, bool bold)
        {
           #if JUCE_WINDOWS
            juce::FontOptions options ("Segoe UI", style, height);
           #else
            juce::ignoreUnused (style);
            juce::FontOptions options (juce::Font::getDefaultSansSerifFontName(), height, juce::Font::plain);
            options = options.withStyle (bold ? "Bold" : "Regular");
           #endif
            juce::Font font (options);
            if (bold)
                font.setBold (true);
            return font;
        }

        juce::Font regular (float height) { return make (height, "Regular", false); }
        juce::Font bold (float height)    { return make (height, "Bold", true); }
        juce::Font heavy (float height)   { return make (height, "Black", true); }
    }
}
