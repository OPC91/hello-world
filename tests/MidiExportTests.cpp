#include "TestHelpers.h"
#include "engine/fill/MidiExport.h"

using namespace seam::test;

namespace
{
    struct Note { std::uint32_t tick; int note; int velocity; bool on; };

    std::vector<Note> parseNotes (const std::vector<std::uint8_t>& file, std::uint32_t& endTick)
    {
        std::vector<Note> notes;
        size_t i = 22; // after MThd (14) + MTrk + length (8)
        std::uint32_t tick = 0;
        auto readVar = [&]
        {
            std::uint32_t v = 0;
            while (true) { const auto b = file[i++]; v = (v << 7) | (b & 0x7f); if (! (b & 0x80)) return v; }
        };
        while (i < file.size())
        {
            tick += readVar();
            const auto status = file[i];
            if (status == 0xff)
            {
                const auto type = file[i + 1];
                i += 2;
                const auto len = readVar();
                i += len;
                if (type == 0x2f) { endTick = tick; break; }
            }
            else
            {
                notes.push_back ({ tick, file[i + 1], file[i + 2], (status & 0xf0) == 0x90 });
                i += 3;
            }
        }
        return notes;
    }
}

TEST_CASE ("Fill MIDI clip: C1 is slice 1, notes on the fill's grid, whole-bar length", "[midi]")
{
    KitSummary kit;
    kit.numSlices = 4;
    kit.lengthSeconds = { 0.3, 0.3, 0.3, 0.3 };
    FillPattern p;
    p.bars = 2;
    p.length = 8.0;
    p.events = { { 0.0, 0, 1.0f }, { 1.0, 1, 0.5f }, { 7.5 + 1.0 / 12.0, 3, 0.8f }, { 8.0, 1, 0.85f, true, FillRole::reverse } };

    const auto file = exportFillMidi (p, kit, { 174.0, 960, 4, 4 });
    REQUIRE (file.size() > 22);
    CHECK (std::string (file.begin(), file.begin() + 4) == "MThd");
    CHECK (file[9] == 0);                    // format 0
    CHECK ((file[12] << 8 | file[13]) == 960);
    CHECK (std::string (file.begin() + 14, file.begin() + 18) == "MTrk");

    std::uint32_t endTick = 0;
    const auto notes = parseNotes (file, endTick);
    std::vector<Note> ons;
    for (const auto& n : notes)
        if (n.on) ons.push_back (n);

    REQUIRE (ons.size() == 4);
    CHECK (ons[0].note == 36);               // slice 1 = C1
    CHECK (ons[0].tick == 0);
    CHECK (ons[0].velocity == 127);
    CHECK (ons[1].note == 37);
    CHECK (ons[1].tick == 960);
    CHECK (ons[1].velocity == 64);
    // The reversed hit starts one slice-length (0.3 s = 0.87 quarters at 174) before the seam.
    CHECK (ons[2].note == 37);
    CHECK (ons[2].tick == static_cast<std::uint32_t> (std::llround ((8.0 - 0.3 * 174.0 / 60.0) * 960.0)));
    CHECK (ons[3].note == 39);
    CHECK (ons[3].tick == static_cast<std::uint32_t> (std::llround ((7.5 + 1.0 / 12.0) * 960.0)));
    CHECK (endTick == 8 * 960);

    // Every note ends, at or before the clip end.
    int offs = 0;
    for (const auto& n : notes)
        if (! n.on) { ++offs; CHECK (n.tick <= endTick); }
    CHECK (offs == 4);
}
