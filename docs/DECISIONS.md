# SEAM design decisions

Defaults and judgement calls, with reasons. Newest at the bottom of each section.

## Family look (shared with Nil2Max)
- Nil2Max's source was not available, so `shared/exnilo_ui` rebuilds the family look from exnilo.co.uk: base palette
  (`#0e0a1c` bg, `#140f27`, `#1c1534`, `#261d45`, line `#352a5c`, ink `#f4eefe`, dim `#a99fcb`), the seven genre themes
  with their exact accent pairs, and the layout of the Nil2Max screenshots (wordmark + genre pill + ADV header, energy
  display / hero knob / options card row, preset pill, tagline, hint line, corner grip).
- Themes tint the base palette towards accent A (after partly desaturating the violet base), so Drum & Bass reads teal
  like the Nil2Max D&B screenshot. Default theme: Drum & Bass.
- Energy display ring shapes: circles (Classic, House), squares (Techno), rising streaks (Trance), triangles (Dubstep),
  hexagons + rain (Drum & Bass), sparkles (Future Bass), taken from the screenshot captions on the Nil2Max page.
- Fonts: Segoe UI on Windows (as Nil2Max); the platform sans-serif elsewhere. No embedded fonts.
- Base editor size 960x640, scalable 75-200% with a locked aspect ratio. Scale and theme are saved in the session.

## Phrase tracking
- Bars are counted from PPQ 0 with the current time signature. Hosts that start at bar 1 = PPQ 0 (all major ones) put
  seams on bars 1, 17, 33 ... for a 16-bar phrase. Time-signature changes mid-song are not followed (documented limit).
- The position is recomputed from the host every block. A block "follows on" if the host position is within 2 samples
  of the previous block's end; otherwise it is a jump (loop, seek, start) and voices/tails are reset.
- Events belong to the half-open range [previous block end, this block end), so host rounding can never fire an event
  twice or skip it.
- Default phrase: 16 bars.

## TRANSITION chain
- One effective value drives everything: max(knob after the downbeat snap, auto ramp).
- High-pass: 12 dB/oct TPT state-variable filter, 20 Hz -> 700 Hz at 100% (x Filter amount), on a 1.4 power curve,
  resonance 0.71 -> 1.56. Coefficients refresh every 4 samples with a 3 ms log-frequency smoother.
- Reverb throw: snares are detected in the input (1.8 kHz band onset at least 2.2x its slow average and not dominated by
  the <120 Hz band, 80 ms refractory). A hit opens the send for a 1/16 note (1 ms attack, 40 ms release) into a
  Freeverb-style reverb (feedback 0.86, damping 0.32). Send level = 0.9 x e^1.3 x Reverb amount. Fill snares will
  trigger throws directly.
- Stutter: beat-locked (quarter-note beats). The first cell of each beat plays live and is captured; the rest of the
  beat repeats it. Off below 35%, 1/8 to 60%, 1/16 to 85%, then 1/32. The cell size is chosen at the start of each
  beat so it never changes mid-beat. 1.5 ms ramps at every repeat; a capture is only repeated if it started on its beat
  (so a seek into the middle of a beat cannot replay stale audio).
- Ducking dip: a beat-synced dip that lets the first 12% of each beat (the transient) through, dips fully by 30% and
  recovers by 90%. Depth = 0.55 x e^1.5 x Duck amount (max 0.9).
- Snap back: increases follow the knob within 30 ms. Decreases of more than 5% wait for the next downbeat (bar start),
  unless they arrive within a 1/16 after it (late automation still counts as on the downbeat). Smaller decreases
  follow at once, so fine adjustments and slow automation fades work. At a snap (or when the auto ramp drops at the
  seam) the reverb tail and stutter fade out over 10 ms and are cleared.
- True bypass: when the effective value is 0 for a whole 256-sample chunk, nothing is ringing and the dry/wet fade is
  at 0, the audio is not touched. Entering and leaving use a 5 ms dry/wet crossfade written as dry*(1-m) + wet*m,
  which is exact at m = 0 and m = 1.
- Transport stopped: the chain fades out in 5 ms and is cleared; after that the audio passes through untouched. So
  TRANSITION has no audible effect while stopped (by design: no noise or tails when stopped).
- Auto ramp: over the last N bars of each phrase (2/4/8/16, clamped to the phrase), linear or "late surge" (x^2.5),
  exactly 0 from the seam's first sample.

## Slicer
- Onsets: SuperFlux-style spectral flux (log-magnitude STFT, ~23 ms Hann window, ~3 ms hop, each frame compared with a
  3-bin max-filtered frame two hops back, which ignores the flicker of noisy snare/hat tails). Peaks must beat the
  local median x 1.4 + a small floor, and be 45 ms apart.
- Each onset is refined to the sample on the signal's first difference (follows transients, ignores low ringing tails):
  back to the half-level point of the rise, then at most 2 ms further, then a 0.5 ms pre-roll. Guarantee tested on the
  built-in break: never more than 0.5 ms late (late would clip the transient), at most 2.5 ms early.
- A candidate must add transient energy: slope energy in the 5 ms after the onset must be 1.6x the 10 ms before. This
  rejects false hits in decaying tails (an 808's pitch glide produced one) at the cost of ghost notes buried in a
  snare tail at nearly the same level, which are genuinely ambiguous. Missing such a ghost is better than a junk slice.
- Labels use the attack's spectrum minus the spectrum just before the onset (so a kick ringing under a hat does not
  make it look like a kick): kick = >35% of the added energy below 150 Hz and centroid < 1.5 kHz; hat = centroid
  > 5 kHz and < 5% lows; the rest are snare-like, and those under 45% of the loudest snare-like peak are ghosts.
- Slices run to the next onset (max 1.5 s). Only the first 30 s of a dropped file is used.
- Built-in break: a synthesised one-bar two-step at 170 BPM (2 kicks, 2 snares, 3 ghosts, 3 hats), so fills work
  before any WAV is dropped and the slicer has a reference test signal.

## Fills
- The fill occupies the last 1/2/4 bars before each seam (clamped to the phrase). Styles weight the vocabulary:
  Liquid (chopped groove, soft roll topping out at 1/32, reverse snare likely), Neuro (half-time switch and ghost
  chops, roll to 1/32T), Jungle (amen-style chops, dense ghosts, roll to 1/32T), Jump-Up (half-time switch, big roll,
  reverse snare likely). Complexity sets ghost/hat density, extra edits, roll length (half a bar to a full bar) and how
  fast the roll gets. Once a fill switches to half-time it stays there until the roll.
- The roll tightens 1/16 -> 1/16T -> 1/32 -> 1/32T in equal sections and never loosens; gain ramps up to the seam.
- The reversed snare ends 1.3 ms before the seam so the seam's anti-click fade never softens its peak.
- Playback: main lane monophonic (each hit chokes the last with a 2 ms fade, like chopped breaks); reversed hits on
  their own lane; linear-interpolated resampling from the break's rate; 0.3 ms fade-in. All voices are silent from the
  seam's first sample (1.3 ms pre-seam fade).
- Fills only start hits while TRANSITION (effective) is above 0, fading in over the first 25% (so 0% stays a true
  bypass). Advanced "Fill" scales the level (0-150%).
- Replace mode mutes the processed incoming drums inside the window (128-sample ramps) and has them back at full level
  on the seam's first sample. Mix mode adds the fill on top. Fill snares open the reverb throw and are sent into it.
- Timing: an event at PPQ p plays on the first sample at or after p (1e-6-sample tolerance), identical for every block
  size; recomputed every block from the host position, so loops and seeks replay correctly. A pattern only plays
  against the kit it was generated for (kit id).
- New fill increments a seed (saved in the session). The pattern is regenerated by a background thread within ~15 ms
  of any change to Style, Complexity, Fill length, time signature, kit or seed.
- MIDI clip: format 0, 960 PPQ, slice n -> note 35 + n, so slice 1 = MIDI 36 = C1 in the C3 = 60 convention used by
  Ableton, FL Studio, Cubase and Bitwig. Velocity = gain. Notes end at the next main-lane hit (min 1/64). Reversed hits
  start one slice-length before the seam. The clip is exactly the fill's length.
- Session state: the dropped file's bytes are embedded (up to 32 MB; base64 in the state XML) plus its path. On load the
  embedded audio wins (the session works on any computer); otherwise the path; otherwise the built-in break.

## Vacuum
- Works on the finished output (drums, fill, reverb), so the drop lands in true silence. The gap [seam - gap, seam)
  fades out over 1.5 ms from its start and is exactly 0 after that; audio is back from the seam's first sample (0.3 ms
  fade-in). Only while TRANSITION (effective) is above 0, so 0% stays a bypass.
- Tape stop: the final time-signature beat before the gap is replayed from a ring buffer at a speed falling linearly
  from 1 to 0 (pitch falls with it). 4 s ring buffer, sized in prepare().
- With the vacuum on, the fill finishes at the gap start (hits inside the gap are skipped; the reversed snare ends at
  the gap) instead of being swallowed by it.

## Presets
- 12 factory presets, 3 per style: Liquid Glide (= the parameter defaults), Rolling Silk, Sunset Lift; Neuro Charge,
  Reese Tear, Halftime Pressure; Amen Rinse, Rewind Selecta, Junglist Roll; Jump-Up Wind, Bounce Spring,
  Dancefloor Detonator.
- Presets store everything except TRANSITION (the performance knob) and the phrase length (belongs to the arrangement).
- User presets: XML in %APPDATA%\Exnilo\SEAM\Presets (shared PresetStore, same layout as Nil2Max's documented
  %APPDATA%\Exnilo\Nil2Max). Presets are internal, not host programs (getNumPrograms() = 1), so hosts that restore
  per-program state (FL Studio) cannot swap state behind the plugin's back.
- The preset bar shows "*" when the current values differ from the loaded preset. Preset name, theme, editor scale,
  seed and the break are saved in the session.
- The preset tagline replaces the style tagline under the preset bar when a factory preset is loaded.

## Advanced panel
- Filter, Reverb, Stutter, Duck and Fill, each 0-150% of the preset's design (double-click = 100%), with a reset
  button. Shown over the main row while ADV is on (as an overlay, so the rest of the UI keeps its place).
