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
