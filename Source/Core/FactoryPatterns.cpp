#include "FactoryPatterns.h"
#include "Tracks.h"

namespace bf::FactoryPatterns
{

void writeTrack (Pattern& p, int track, const char* steps, float nudge)
{
    int i = 0;

    for (const char* c = steps; *c != 0 && i < kMaxSteps; ++c)
    {
        if (*c == ' ')
            continue;

        auto& s = p.tracks[(size_t) track].steps[(size_t) i++];
        s = Step {};

        int level = 0, ratchet = 1;

        if (*c >= '1' && *c <= '9')      level = *c - '0';
        else if (*c >= 'a' && *c <= 'i') { level = *c - 'a' + 1; ratchet = 2; }
        else if (*c >= 'A' && *c <= 'I') { level = *c - 'A' + 1; ratchet = 3; }

        if (level > 0)
        {
            s.on = true;
            s.velocity = (std::uint8_t) juce::jlimit (1, 127, level * 14 + 1);
            s.ratchet = (std::uint8_t) ratchet;
            s.nudge = nudge;
        }
    }
}

namespace
{
    enum { K = 0, S = 1, G = 2, CH = 3, OH = 4, RD = 5, PC = 6, FX = 7 };

    std::vector<Entry> build()
    {
        std::vector<Entry> list;

        {   // Classic two-step: kick on 1 and the "and" of 3, snare on 2 and 4.
            Pattern p;
            writeTrack (p, K,  "9... .... ..8. ....");
            writeTrack (p, S,  ".... 9... .... 9...");
            writeTrack (p, CH, "5.7. 4.7. 5.7. 4...");
            writeTrack (p, OH, ".... .... .... ..6.");
            list.push_back ({ "Two-Step", p });
        }

        {   // Rolling two-step with ghost snares and a 16th hat roll.
            Pattern p;
            writeTrack (p, K,  "9... .... ..8. ....");
            writeTrack (p, S,  ".... 9... .... 9...");
            writeTrack (p, G,  ".... ...3 .2.. ..3.", 0.04f);
            writeTrack (p, CH, "6364 6364 6364 63b4");
            writeTrack (p, PC, "..2. ..2. ..2. ..2.");
            list.push_back ({ "Rolling Two-Step", p });
        }

        {   // Amen-style break over two bars, with the bar-2 snare edit.
            Pattern p;
            p.length = 32;
            writeTrack (p, K,  "9.8. .... ..87 ....  9.8. .... ..8. ....");
            writeTrack (p, S,  ".... 9..6 .6.. 9..6  .... 9..6 .6.. ..8.");
            writeTrack (p, G,  ".... .... ...3 ....  .... .... ...3 .3..", 0.03f);
            writeTrack (p, RD, "6.5. 6.5. 6.5. 6.5.  6.5. 6.5. 6.5. 6.5.");
            writeTrack (p, OH, ".... .... .... ....  .... .... .... ...5");
            list.push_back ({ "Amen Break", p });
        }

        {   // Halftime: snare on 3, sparse syncopated kicks, swung hats.
            Pattern p;
            writeTrack (p, K,  "9... ..6. ...7 ....");
            writeTrack (p, S,  ".... .... 9... ....");
            writeTrack (p, G,  ".... .... .... ..3.", 0.05f);
            writeTrack (p, CH, "6.4. 6.4. 6.4. 6.C.");
            writeTrack (p, OH, ".... .... .... ..5.");
            list.push_back ({ "Halftime", p });
        }

        {   // Jungle breakbeat: busy edited break, ride on top, roll at the end.
            Pattern p;
            p.length = 32;
            writeTrack (p, K,  "9..7 .... ..8. ....  ..8. .... ..87 ....");
            writeTrack (p, S,  ".... 9..6 .... 9..7  .... 9..6 .7.. 9.8d");
            writeTrack (p, G,  "..3. ..3. .3.. ..3.  ...3 ..3. ...3 ....", 0.04f);
            writeTrack (p, RD, "6.5. 6.5. 6.5. 6.5.  6.5. 6.5. 6.5. 6...");
            writeTrack (p, CH, ".4.4 .4.4 .4.4 .4.4  .4.4 .4.4 .4.4 .4..");
            list.push_back ({ "Jungle Break", p });
        }

        {   // Minimal liquid: space, rim clicks and a soft shaker.
            Pattern p;
            writeTrack (p, K,  "8... .... ..7. ....");
            writeTrack (p, S,  ".... 8... .... 8...");
            writeTrack (p, CH, "..5. ..5. ..5. ..5.");
            writeTrack (p, RD, ".... ..4. .... ..4.");
            writeTrack (p, PC, "...3 ...2 ...3 .2.2");
            list.push_back ({ "Minimal Liquid", p });
        }

        return list;
    }
}

const std::vector<Entry>& all()
{
    static const auto list = build();
    return list;
}

PatternBank makeBank()
{
    PatternBank bank;

    for (size_t i = 0; i < all().size() && i < (size_t) kNumPatterns; ++i)
        bank.patterns[i] = all()[i].pattern;

    return bank;
}

} // namespace bf::FactoryPatterns
