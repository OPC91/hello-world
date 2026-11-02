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
