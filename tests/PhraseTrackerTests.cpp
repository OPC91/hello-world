#include "TestHelpers.h"

using namespace seam::test;
using Catch::Approx;

TEST_CASE ("Bars to the seam count down through a 16-bar phrase", "[phrase]")
{
    PhraseTracker tracker;
    auto pos = tracker.update (playingAt (0.0), 16);
    REQUIRE (pos.valid);
    CHECK (pos.barInPhrase == 0);
    CHECK (pos.seamPpq == Approx (64.0));
    CHECK (pos.barsToSeam == Approx (16.0));

    tracker.reset();
    pos = tracker.update (playingAt (13.0 * 4.0 + 2.0), 16); // bar 14 (0-based 13), beat 3
    CHECK (pos.barInPhrase == 13);
    CHECK (pos.barsToSeam == Approx (2.5));
    CHECK (pos.phraseStartPpq == Approx (0.0));
}

TEST_CASE ("Phrase length is selectable", "[phrase]")
{
    PhraseTracker tracker;
    for (const int bars : { 8, 16, 32 })
    {
        tracker.reset();
        const auto pos = tracker.update (playingAt (40.0 * 4.0 + 1.0), bars); // bar 40 + 1 beat
        const double phraseLen = bars * 4.0;
        CHECK (pos.phraseStartPpq == Approx (std::floor (161.0 / phraseLen) * phraseLen));
        CHECK (pos.seamPpq == Approx (pos.phraseStartPpq + phraseLen));
        CHECK (pos.barInPhrase == (40 % bars));
    }
}

TEST_CASE ("Exactly on a seam, the next seam is a whole phrase away", "[phrase]")
{
    PhraseTracker tracker;
    const auto pos = tracker.update (playingAt (64.0), 16);
    CHECK (pos.barInPhrase == 0);
    CHECK (pos.phraseStartPpq == Approx (64.0));
    CHECK (pos.barsToSeam == Approx (16.0));

    // Host rounding a hair below the seam still counts as the seam.
    tracker.reset();
    const auto rounded = tracker.update (playingAt (64.0 - 1.0e-12), 16);
    CHECK (rounded.phraseStartPpq == Approx (64.0));
}

TEST_CASE ("Time signatures change the bar length", "[phrase]")
{
    PhraseTracker tracker;
    // 7/8: a bar is 3.5 quarter notes, a beat is an eighth.
    const auto pos = tracker.update (playingAt (3.5 * 9, 174.0, 48000.0, 512, 7, 8), 8);
    CHECK (pos.quartersPerBar == Approx (3.5));
    CHECK (pos.quartersPerBeat == Approx (0.5));
    CHECK (pos.barInPhrase == 1);
    CHECK (pos.seamPpq == Approx (3.5 * 16));
}

TEST_CASE ("Continuous playback is not a jump; loops and seeks are", "[phrase]")
{
    PhraseTracker tracker;
    FakeHost host;
    host.ppq = 60.0;

    auto first = tracker.update (host.next (512), 16);
    CHECK (first.jumped);

    for (int i = 0; i < 50; ++i)
    {
        const double expectedFrom = first.blockEndPpq;
        const auto pos = tracker.update (host.next (100 + (i * 37) % 400), 16);
        CHECK_FALSE (pos.jumped);
        CHECK (pos.eventFromPpq == Approx (expectedFrom).margin (1.0e-12));
        first = pos;
    }

    // Loop back to the start of the phrase.
    host.ppq = 0.0;
    const auto looped = tracker.update (host.next (512), 16);
    CHECK (looped.jumped);
    CHECK (looped.eventFromPpq == Approx (0.0));
    CHECK (looped.barsToSeam == Approx (16.0));
}

TEST_CASE ("Event ranges tile the timeline with no gaps or overlaps", "[phrase]")
{
    // Host positions with float rounding noise: event ranges must still follow on exactly.
    PhraseTracker tracker;
    FakeHost host;
    double previousEnd = -1.0;
    for (int i = 0; i < 200; ++i)
    {
        auto t = host.next (64 + (i * 13) % 300);
        t.ppq += (i % 2 == 0 ? 1.0e-9 : -1.0e-9);
        const auto pos = tracker.update (t, 16);
        if (previousEnd >= 0.0)
            CHECK (pos.eventFromPpq == previousEnd);
        previousEnd = pos.blockEndPpq;
    }
}

TEST_CASE ("Stopped or position-less transports are not valid", "[phrase]")
{
    PhraseTracker tracker;
    auto t = playingAt (10.0);
    t.playing = false;
    CHECK_FALSE (tracker.update (t, 16).valid);

    t = playingAt (10.0);
    t.hasPosition = false;
    CHECK_FALSE (tracker.update (t, 16).valid);

    // After a stop, starting again counts as a jump.
    const auto restarted = tracker.update (playingAt (10.0), 16);
    CHECK (restarted.valid);
    CHECK (restarted.jumped);
}

TEST_CASE ("sampleAt finds the first sample at or after an event", "[phrase]")
{
    PhraseTracker tracker;
    const auto pos = tracker.update (playingAt (4.0, 120.0, 48000.0, 48000), 16); // 24000 samples per quarter
    CHECK (pos.sampleAt (4.0) == 0);
    CHECK (pos.sampleAt (5.0) == 24000);
    CHECK (pos.sampleAt (4.5) == 12000);
    CHECK (pos.sampleAt (4.0 + 0.5 / 24000.0) == 0);
}
