# Testing SEAM by ear

Automated tests cover timing, bypass and state (see `ctest`), and pluginval passes at strictness 10. These checks
are the ones only ears can do. Put a drum loop at 170-174 BPM on a track (or a drum bus), add SEAM, loop 32 bars,
and press play. A demo build drops out for 2 s every 40 s: that is expected.

## 1. Phrase tracking and the look
- The energy display says "PHRASE 16 BARS" and counts down "N BARS TO SEAM", switching to beats in the last bar.
  The strip under it lights bar by bar; the fill bars are outlined.
- Change the phrase to 8 and 32: the countdown jumps accordingly. Try a 7/8 or 6/8 project: bars still line up.
- Stop the transport: "PLAY TO SYNC". Loop a region that crosses a seam: the countdown follows the loop.
- Theme dropdown: all seven themes recolour everything; Drum & Bass is the default. Drag the corner from 75% to 200%.

## 2. TRANSITION and true bypass
- At 0% SEAM must be inaudible: null test it (duplicate the track, flip the polarity of one, SEAM on the other at 0%,
  you should hear silence; in Reaper the "phase invert" on a second send works too).
- Sweep up slowly: the low end thins, snares start throwing into a big reverb (from ~20%), the bus starts to breathe
  on the beat (ducking dip), and from ~35% the second half of each beat repeats its start (1/8), tightening to 1/16
  (60%) and 1/32 (85%). No clicks anywhere.
- At 80%, yank the knob (or an automation point) down to 0 mid-bar: the effect must hold until the next downbeat, then
  snap off clean, with the reverb tail cut, on the "1".
- Small moves down (a few %) and slow automation fades should follow smoothly, not wait for a downbeat.
- Stop the transport with the effect up: the tail stops within a few ms; no noise while stopped.
- Mono track into SEAM (mono in, stereo out) should behave the same.

## 3. Fills
- With TRANSITION at 50%+, the last bar(s) before each seam play a fill from the built-in break. Try each style:
  Liquid (gentle, often a reverse snare), Neuro (half-time switch, ghost chops), Jungle (busy amen-style chops),
  Jump-Up (half-time, big roll, reverse snare).
- The roll should speed up into the seam (1/16 -> 1/16T -> 1/32 -> 1/32T at high Complexity) and the reversed snare
  should swell right into the downbeat. Nothing from the fill rings past the "1".
- Complexity 0 vs 100: sparse vs busy. New fill: a different fill each click. Fill length 1/2/4 bars.
- Mix vs Replace: in Replace your loop drops out under the fill and is back exactly on the seam.
- Drop your own break (an amen, a think break...) on the fill panel: the waveform shows coloured slice markers
  (kick = accent A, snare = accent B, ghost = faded B, hat = white). Fills now use your break's hits.
- Drag DRAG MIDI onto a track: a clip as long as the fill, slice 1 on C1 upward. Load the same break into a
  slicer (e.g. Ableton's Slice to new MIDI track, FL's Slicex) and it should play the same fill.
- Loop a region that starts inside the fill window: hits after the loop start play on time each pass.
- Save, close and reopen the project: the dropped break, theme, preset name and the same fill come back. Move the
  project to another computer: the break is embedded, so it still works.

## 4. Vacuum and Auto mode
- Vacuum on, 1/8: everything goes silent for the last 1/8 before the seam and comes back exactly on the downbeat.
  Try 1/16 and 1/4. Add Tape stop: the final beat slows to a halt into the gap.
- With the vacuum on, the fill's reverse snare should finish where the gap begins (not get cut off).
- Auto ramp with TRANSITION at 0: over the last 4 bars of each phrase the effect rises by itself (the knob shows a
  faint "live" arc) and drops to zero on the seam. Late surge saves the push for the end.

## 5. Presets and Advanced
- Step through all 12 factory presets with the arrows; each has its own tagline. Change something: a "*" appears.
- Save preset as... then find it under User in the menu and in `%APPDATA%\Exnilo\SEAM\Presets`. Delete it.
- ADV: drag Reverb to 150% and Stutter to 0%: more wash, no stutter. Double-click a slider: back to 100%.

## 6. Licensing
- Demo: the "DEMO: ACTIVATE" pill is shown and the audio drops out for 2 s every 40 s.
- Activate (in a build configured with the Lemon Squeezy ids) with a key: dropouts stop in every SEAM instance at
  once. Click the SEAM title: "Deactivate this computer" frees the activation.

## DAWs to try
FL Studio (mixer insert; right-click TRANSITION > Create automation clip), Ableton Live, Bitwig, Reaper, Cubase,
Studio One. Rescan plugins after installing.
