#include "MidiExport.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace seam::engine
{
    namespace
    {
        void appendVariableLength (std::vector<std::uint8_t>& out, std::uint32_t value)
        {
            std::uint8_t bytes[5];
            int count = 0;
            bytes[count++] = static_cast<std::uint8_t> (value & 0x7f);
            while ((value >>= 7) != 0)
                bytes[count++] = static_cast<std::uint8_t> ((value & 0x7f) | 0x80);
            while (count > 0)
                out.push_back (bytes[--count]);
        }

        void append32 (std::vector<std::uint8_t>& out, std::uint32_t v)
        {
            for (int shift = 24; shift >= 0; shift -= 8)
                out.push_back (static_cast<std::uint8_t> ((v >> shift) & 0xff));
        }

        struct MidiEvent
        {
            std::uint32_t tick;
            int order;                       ///< at equal ticks: note-offs (0) before note-ons (1)
            std::vector<std::uint8_t> bytes;
        };
    }

    std::vector<std::uint8_t> exportFillMidi (const FillPattern& pattern, const KitSummary& kit, const MidiClipOptions& options)
    {
        const double tpq = options.ticksPerQuarter;
        const auto toTick = [&] (double quarters) { return static_cast<std::uint32_t> (std::llround (std::max (0.0, quarters) * tpq)); };
        const auto endTick = toTick (pattern.length);

        // Main-lane hit times, to end each note at the next hit.
        std::vector<double> mainTimes;
        for (const auto& e : pattern.events)
            if (! e.reverse)
                mainTimes.push_back (e.ppq);

        std::vector<MidiEvent> events;
        for (const auto& e : pattern.events)
        {
            if (e.slice < 0 || e.slice >= kit.numSlices)
                continue;
            const double sliceQuarters = kit.lengthSeconds[static_cast<size_t> (e.slice)] * options.bpm / 60.0;
            double start = e.ppq, end;
            if (e.reverse)
            {
                start = std::max (0.0, e.ppq - sliceQuarters);
                end = e.ppq;
            }
            else
            {
                const auto next = std::upper_bound (mainTimes.begin(), mainTimes.end(), e.ppq + 1.0e-9);
                end = std::min ({ start + sliceQuarters, next != mainTimes.end() ? *next : pattern.length, pattern.length });
            }
            end = std::max (end, start + 1.0 / 16.0);   // at least a 1/64

            const int note = std::clamp (firstSliceNote + e.slice, 0, 127);
            const auto velocity = static_cast<std::uint8_t> (std::clamp (static_cast<int> (std::lround (e.gain * 127.0f)), 1, 127));
            events.push_back ({ toTick (start), 1, { 0x90, static_cast<std::uint8_t> (note), velocity } });
            events.push_back ({ std::min (toTick (end), endTick), 0, { 0x80, static_cast<std::uint8_t> (note), 0 } });
        }
        std::stable_sort (events.begin(), events.end(), [] (const MidiEvent& a, const MidiEvent& b)
                          { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

        std::vector<std::uint8_t> track;
        auto meta = [&] (std::uint8_t type, const std::vector<std::uint8_t>& data)
        {
            track.push_back (0);
            track.push_back (0xff);
            track.push_back (type);
            appendVariableLength (track, static_cast<std::uint32_t> (data.size()));
            track.insert (track.end(), data.begin(), data.end());
        };
        const std::string name = "SEAM fill";
        meta (0x03, std::vector<std::uint8_t> (name.begin(), name.end()));
        int denominatorPower = 0;
        while ((1 << denominatorPower) < options.timeSigDenominator) ++denominatorPower;
        meta (0x58, { static_cast<std::uint8_t> (options.timeSigNumerator), static_cast<std::uint8_t> (denominatorPower), 24, 8 });
        const auto microsPerQuarter = static_cast<std::uint32_t> (std::lround (60000000.0 / std::max (1.0, options.bpm)));
        meta (0x51, { static_cast<std::uint8_t> (microsPerQuarter >> 16), static_cast<std::uint8_t> (microsPerQuarter >> 8),
                      static_cast<std::uint8_t> (microsPerQuarter) });

        std::uint32_t lastTick = 0;
        for (const auto& e : events)
        {
            appendVariableLength (track, e.tick - lastTick);
            track.insert (track.end(), e.bytes.begin(), e.bytes.end());
            lastTick = e.tick;
        }
        // End of track exactly at the fill's length, so the clip is whole bars.
        appendVariableLength (track, endTick > lastTick ? endTick - lastTick : 0);
        track.insert (track.end(), { 0xff, 0x2f, 0x00 });

        std::vector<std::uint8_t> file { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1 };
        file.push_back (static_cast<std::uint8_t> (options.ticksPerQuarter >> 8));
        file.push_back (static_cast<std::uint8_t> (options.ticksPerQuarter & 0xff));
        file.insert (file.end(), { 'M', 'T', 'r', 'k' });
        append32 (file, static_cast<std::uint32_t> (track.size()));
        file.insert (file.end(), track.begin(), track.end());
        return file;
    }
}
