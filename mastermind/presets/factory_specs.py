"""Factory preset definitions.

Each genre is described by a compact :class:`GenreSpec`; :func:`build_preset`
turns it into a full :class:`Preset` (chain settings + target profile). The
JSON files in ``presets/factory`` are generated from this table by
``tools/build_factory_presets.py`` and a test checks they stay in sync.

Where the numbers come from
---------------------------
These are *informed starting points*, not measurements of a reference corpus
(Phase 4 refines them from real references):

* **Loudness**: typical integrated loudness of contemporary commercial releases
  in each genre (club/EDM, trap, metalcore ~-6.5 to -8 LUFS; pop/rock ~-8 to
  -12; acoustic/jazz ~-14 to -16; classical ~-20). Streaming services normalise
  playback (Spotify/YouTube -14, Apple -16), which is why the meter targets are
  separate from the genre's own loudness.
* **Tonal curve**: third-octave long-term spectra of commercial music sit close
  to pink noise, falling gently (~-1 to -2.5 dB/oct on a 1/3-octave sum) with a
  low-frequency bump; genre differences show mainly in sub/bass weight and HF
  extension (Pestana et al., "Spectral characteristics of popular commercial
  recordings 1950-2010", AES 135, 2013). The curve is built from a tilt (above
  100 Hz), gaussian bumps (in octaves), a natural roll-off below 40 Hz and the
  genre's band-limiting slopes.
* **Stereo**: mono low end for club playback and vinyl-era conventions (mono
  below 100-160 Hz), wider highs for trance/future bass/ambient, natural width
  and no mono-maker for jazz/classical.
* **Crest factor**: follows from loudness and ceiling: whole-file crest
  (sample peak - RMS) ~ (ceiling - LUFS) + 2.5 dB for stereo programme,
  because LUFS sums the two channels (+3 dB) while RMS averages them.
* **Chain**: EQ applies ~40 % of the curve's deviations as broad moves (a
  starting tilt, not a full correction); dynamics come from five style
  templates assuming a mix around -18 LUFS; the limiter drive is a first guess
  (the browser's auto-gain fits it to the actual mix).
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from mastermind.dsp.chain import MasteringChain
from mastermind.presets.schema import SPECTRUM_FREQS, Preset, TargetProfile, normalise_spectrum, slugify

EQ_FRACTION = 0.4


@dataclass(frozen=True)
class GenreSpec:
    name: str
    category: str
    description: str
    lufs: float
    # tonal curve (dB unless noted)
    tilt: float  # dB/oct around 1 kHz
    sub: float  # ~50 Hz
    low: float  # ~130 Hz
    lowmid: float  # ~400 Hz
    presence: float  # ~3.5 kHz
    air: float  # shelf above ~11 kHz
    widths: tuple[float, float, float, float]
    mono_freq: float
    dynamics: str  # none | gentle | moderate | punchy | dense
    hp: float = 28.0
    lp: float = 20000.0
    ceiling: float = -1.0
    lra: float = 6.0
    saturation: tuple[str, float] | None = None  # (mode, drive dB)
    limiter_release: float = 80.0
    linear_phase: bool = False
    tags: tuple[str, ...] = field(default_factory=tuple)
    notes: str = ""


S = GenreSpec
FACTORY_SPECS: list[GenreSpec] = [
    # ---- Electronic --------------------------------------------------
    S(
        name="House",
        category="Electronic",
        description="Warm, driving four-on-the-floor with a solid sub and smooth top.",
        lufs=-8.0,
        tilt=-1.5,
        sub=3.0,
        low=2.0,
        lowmid=-1.5,
        presence=0.5,
        air=1.0,
        widths=(0, 90, 115, 125),
        mono_freq=140,
        dynamics="punchy",
        lra=4.5,
        limiter_release=60,
        tags=("club", "4x4"),
        notes="Kick/bass carry the groove: mono below 140 Hz, gentle low-mid scoop for clarity.",
    ),
    S(
        name="Deep House",
        category="Electronic",
        description="Rounder, darker and more dynamic than House; soft highs.",
        lufs=-10.0,
        tilt=-2.0,
        sub=3.0,
        low=2.5,
        lowmid=-1.0,
        presence=-0.5,
        air=0.0,
        widths=(0, 95, 115, 120),
        mono_freq=130,
        dynamics="gentle",
        lra=5.5,
        saturation=("tape", 3.0),
        limiter_release=100,
        tags=("club", "warm"),
        notes="Tape warmth and less top-end; keeps more dynamics than peak-time house.",
    ),
    S(
        name="Tech House",
        category="Electronic",
        description="Tight, percussive low end and crisp percussion.",
        lufs=-8.0,
        tilt=-1.5,
        sub=3.5,
        low=1.5,
        lowmid=-2.0,
        presence=1.0,
        air=1.0,
        widths=(0, 85, 110, 120),
        mono_freq=150,
        dynamics="punchy",
        lra=4.0,
        limiter_release=50,
        tags=("club", "percussive"),
        notes="Mono up to 150 Hz for club systems; deeper low-mid scoop for drum definition.",
    ),
    S(
        name="Techno",
        category="Electronic",
        description="Heavy, relentless sub and dense mids; loud and controlled.",
        lufs=-8.0,
        tilt=-1.2,
        sub=4.0,
        low=2.0,
        lowmid=-2.5,
        presence=1.0,
        air=0.5,
        widths=(0, 80, 110, 115),
        mono_freq=150,
        dynamics="dense",
        lra=4.0,
        saturation=("tube", 2.0),
        limiter_release=60,
        tags=("club", "dark"),
        notes="Dense multiband control and light tube drive; narrower low-mids keep the kick centred.",
    ),
    S(
        name="Trance",
        category="Electronic",
        description="Wide, bright and euphoric with an airy top end.",
        lufs=-8.0,
        tilt=-1.0,
        sub=2.5,
        low=1.0,
        lowmid=-2.0,
        presence=1.5,
        air=2.5,
        widths=(0, 100, 130, 140),
        mono_freq=120,
        dynamics="moderate",
        lra=5.0,
        limiter_release=80,
        tags=("club", "wide", "bright"),
        notes="Extra width on high-mid/high bands for supersaws; air shelf for shimmer.",
    ),
    S(
        name="Dubstep",
        category="Electronic",
        description="Massive sub, aggressive mids, very loud.",
        lufs=-6.5,
        tilt=-1.0,
        sub=5.0,
        low=1.0,
        lowmid=-1.5,
        presence=2.0,
        air=1.5,
        widths=(0, 90, 120, 125),
        mono_freq=150,
        dynamics="dense",
        lra=4.0,
        saturation=("soft_clip", 3.0),
        limiter_release=40,
        tags=("bass", "loud"),
        notes="Soft-clip before the limiter to reach -6.5 LUFS without pumping.",
    ),
    S(
        name="Drum & Bass",
        category="Electronic",
        description="Fast, punchy drums over deep sub; bright and loud.",
        lufs=-7.0,
        tilt=-0.8,
        sub=4.5,
        low=0.5,
        lowmid=-2.0,
        presence=2.0,
        air=2.0,
        widths=(0, 95, 120, 130),
        mono_freq=160,
        dynamics="dense",
        lra=4.5,
        saturation=("soft_clip", 2.0),
        limiter_release=40,
        tags=("bass", "fast"),
        notes="Fast release for 170+ BPM transients; mono below 160 Hz for reese basses.",
    ),
    S(
        name="Future Bass",
        category="Electronic",
        description="Huge wide chords, bright top, bouncy sub.",
        lufs=-7.5,
        tilt=-1.0,
        sub=3.5,
        low=1.0,
        lowmid=-2.0,
        presence=1.5,
        air=3.0,
        widths=(0, 110, 140, 150),
        mono_freq=120,
        dynamics="punchy",
        lra=5.0,
        limiter_release=60,
        tags=("wide", "bright"),
        notes="Widest highs of the electronic set; strong air for supersaw chords.",
    ),
    S(
        name="Lo-fi",
        category="Electronic",
        description="Dusty, warm and soft: rolled-off highs, gentle dynamics.",
        lufs=-13.0,
        tilt=-3.0,
        sub=1.0,
        low=2.0,
        lowmid=1.0,
        presence=-1.5,
        air=-4.0,
        widths=(10, 90, 95, 90),
        mono_freq=100,
        dynamics="gentle",
        hp=35,
        lp=11000,
        lra=6.0,
        saturation=("tape", 8.0),
        limiter_release=150,
        tags=("chill", "warm", "vintage"),
        notes="Heavy tape drive and an 11 kHz low-pass; slightly narrowed highs like old samples.",
    ),
    S(
        name="Ambient",
        category="Electronic",
        description="Spacious, wide and dynamic; minimal compression.",
        lufs=-17.0,
        tilt=-1.8,
        sub=1.0,
        low=0.5,
        lowmid=-0.5,
        presence=0.0,
        air=2.0,
        widths=(40, 110, 135, 150),
        mono_freq=60,
        dynamics="none",
        hp=22,
        lra=9.0,
        limiter_release=200,
        linear_phase=True,
        tags=("wide", "dynamic"),
        notes="Linear-phase EQ, no compression; mono-maker only below 60 Hz to preserve space.",
    ),
    # ---- Hip-Hop -----------------------------------------------------
    S(
        name="Boom Bap",
        category="Hip-Hop",
        description="Punchy sampled drums, warm mids, slightly dark top.",
        lufs=-9.5,
        tilt=-2.2,
        sub=2.0,
        low=3.0,
        lowmid=0.5,
        presence=0.5,
        air=-1.0,
        widths=(0, 85, 100, 105),
        mono_freq=120,
        dynamics="punchy",
        lp=16000,
        lra=5.0,
        saturation=("tape", 4.0),
        limiter_release=70,
        tags=("vintage", "warm"),
        notes="Kick/snare punch over vinyl-era warmth; a 16 kHz roll-off mimics sampler bandwidth.",
    ),
    S(
        name="Trap",
        category="Hip-Hop",
        description="Booming 808 sub, crisp hats, loud.",
        lufs=-7.5,
        tilt=-1.2,
        sub=6.0,
        low=1.0,
        lowmid=-2.5,
        presence=1.0,
        air=2.5,
        widths=(0, 90, 115, 125),
        mono_freq=130,
        dynamics="punchy",
        lra=4.5,
        saturation=("soft_clip", 2.0),
        limiter_release=50,
        tags=("808", "loud"),
        notes="The 808 dominates: +6 dB sub in the target, mono below 130 Hz, scooped low-mids.",
    ),
    S(
        name="Drill",
        category="Hip-Hop",
        description="Sliding 808s, dark aggressive mids, hard limiting.",
        lufs=-8.0,
        tilt=-1.2,
        sub=5.5,
        low=1.0,
        lowmid=-2.5,
        presence=1.5,
        air=2.0,
        widths=(0, 90, 110, 120),
        mono_freq=140,
        dynamics="dense",
        lra=4.0,
        saturation=("soft_clip", 3.0),
        limiter_release=40,
        tags=("808", "aggressive"),
        notes="Dense control to keep gliding 808s even; presence lift for vocal bite.",
    ),
    S(
        name="R&B",
        category="Hip-Hop",
        description="Smooth, warm low end with silky vocals and air.",
        lufs=-10.0,
        tilt=-1.8,
        sub=3.0,
        low=2.0,
        lowmid=-1.0,
        presence=0.5,
        air=1.5,
        widths=(0, 95, 115, 120),
        mono_freq=110,
        dynamics="moderate",
        lra=5.5,
        saturation=("tube", 2.0),
        limiter_release=100,
        tags=("smooth", "vocal"),
        notes="Gentle tube warmth and air for vocals; moderate limiting keeps it silky.",
    ),
    S(
        name="Neo-Soul",
        category="Hip-Hop",
        description="Organic, warm and dynamic; live-band feel.",
        lufs=-12.0,
        tilt=-2.2,
        sub=1.5,
        low=2.0,
        lowmid=0.5,
        presence=0.0,
        air=0.5,
        widths=(10, 100, 115, 115),
        mono_freq=90,
        dynamics="gentle",
        lra=7.0,
        saturation=("tape", 3.0),
        limiter_release=150,
        tags=("organic", "warm"),
        notes="Preserves live dynamics: gentle compression, tape glue, modest loudness.",
    ),
    # ---- Pop ---------------------------------------------------------
    S(
        name="Mainstream Pop",
        category="Pop",
        description="Polished, bright and loud with a controlled low end.",
        lufs=-9.0,
        tilt=-1.2,
        sub=2.5,
        low=1.0,
        lowmid=-1.5,
        presence=1.5,
        air=2.5,
        widths=(0, 100, 120, 125),
        mono_freq=110,
        dynamics="moderate",
        lra=5.0,
        limiter_release=80,
        tags=("radio", "bright"),
        notes="Vocal presence and air; balanced sub; competitive but not crushed.",
    ),
    S(
        name="Indie Pop",
        category="Pop",
        description="Natural, slightly lo-fi brightness with more dynamics.",
        lufs=-11.0,
        tilt=-1.6,
        sub=1.5,
        low=1.0,
        lowmid=-0.5,
        presence=1.0,
        air=1.5,
        widths=(10, 100, 115, 120),
        mono_freq=100,
        dynamics="gentle",
        lra=6.0,
        saturation=("tape", 2.0),
        limiter_release=120,
        tags=("natural",),
        notes="Less limiting and a touch of tape for character.",
    ),
    S(
        name="K-Pop",
        category="Pop",
        description="Hyper-polished, very bright, wide and loud.",
        lufs=-7.0,
        tilt=-0.9,
        sub=3.0,
        low=1.0,
        lowmid=-2.0,
        presence=2.0,
        air=3.0,
        widths=(0, 105, 125, 135),
        mono_freq=120,
        dynamics="dense",
        lra=4.5,
        limiter_release=50,
        tags=("bright", "loud", "wide"),
        notes="Loudest pop preset: strong presence and air, wide high band, dense control.",
    ),
    S(
        name="Synthpop",
        category="Pop",
        description="Glossy synths, punchy drums, wide stereo image.",
        lufs=-9.0,
        tilt=-1.3,
        sub=2.0,
        low=1.5,
        lowmid=-1.5,
        presence=1.0,
        air=2.0,
        widths=(0, 105, 125, 130),
        mono_freq=110,
        dynamics="moderate",
        lra=5.0,
        limiter_release=80,
        tags=("synth", "wide"),
        notes="80s-inspired width on synth bands with modern low-end control.",
    ),
    # ---- Rock --------------------------------------------------------
    S(
        name="Classic Rock",
        category="Rock",
        description="Warm mids, natural dynamics, gentle top.",
        lufs=-12.0,
        tilt=-2.0,
        sub=-1.0,
        low=1.5,
        lowmid=1.0,
        presence=1.0,
        air=0.0,
        widths=(10, 100, 110, 110),
        mono_freq=90,
        dynamics="moderate",
        hp=35,
        lp=18000,
        lra=7.0,
        saturation=("tube", 3.0),
        limiter_release=150,
        tags=("vintage", "warm"),
        notes="Analogue-era balance: mid-forward, no hyped sub, tube colour, modest loudness.",
    ),
    S(
        name="Indie Rock",
        category="Rock",
        description="Gritty guitars, present vocals, moderate loudness.",
        lufs=-10.5,
        tilt=-1.7,
        sub=0.0,
        low=1.0,
        lowmid=0.5,
        presence=1.5,
        air=1.0,
        widths=(10, 100, 115, 115),
        mono_freq=100,
        dynamics="moderate",
        lra=6.0,
        saturation=("tape", 3.0),
        limiter_release=100,
        tags=("guitar",),
        notes="Tape grit and presence for guitars and vocals.",
    ),
    S(
        name="Punk",
        category="Rock",
        description="Raw, loud and forward; aggressive presence.",
        lufs=-8.5,
        tilt=-1.2,
        sub=-1.0,
        low=1.0,
        lowmid=0.5,
        presence=2.5,
        air=1.0,
        widths=(0, 95, 110, 110),
        mono_freq=110,
        dynamics="dense",
        lra=4.5,
        saturation=("tube", 4.0),
        limiter_release=60,
        tags=("raw", "loud"),
        notes="Forward upper mids and dense compression; more drive than other rock presets.",
    ),
    S(
        name="Alternative",
        category="Rock",
        description="Big guitars, punchy drums, modern clarity.",
        lufs=-9.5,
        tilt=-1.4,
        sub=0.5,
        low=1.0,
        lowmid=-0.5,
        presence=2.0,
        air=1.5,
        widths=(0, 100, 115, 120),
        mono_freq=100,
        dynamics="moderate",
        lra=5.5,
        saturation=("tube", 2.0),
        limiter_release=80,
        tags=("guitar", "modern"),
        notes="Modern rock balance: clean low-mids, presence for guitars.",
    ),
    # ---- Metal -------------------------------------------------------
    S(
        name="Heavy Metal",
        category="Metal",
        description="Tight lows, scooped low-mids, cutting guitars.",
        lufs=-8.5,
        tilt=-1.3,
        sub=0.0,
        low=1.5,
        lowmid=-2.5,
        presence=2.5,
        air=1.0,
        widths=(0, 100, 120, 120),
        mono_freq=120,
        dynamics="dense",
        hp=35,
        lra=4.5,
        saturation=("soft_clip", 2.0),
        limiter_release=60,
        tags=("guitar", "aggressive"),
        notes="Classic low-mid scoop and upper-mid bite; high-pass at 35 Hz keeps palm mutes tight.",
    ),
    S(
        name="Metalcore",
        category="Metal",
        description="Very loud, wide guitars, huge kick and breakdowns.",
        lufs=-7.0,
        tilt=-1.0,
        sub=1.5,
        low=1.0,
        lowmid=-3.0,
        presence=3.0,
        air=2.0,
        widths=(0, 100, 125, 125),
        mono_freq=130,
        dynamics="dense",
        hp=32,
        lra=4.0,
        saturation=("soft_clip", 3.0),
        limiter_release=40,
        tags=("loud", "modern"),
        notes="Loudest metal preset: deep low-mid scoop, strong presence, soft-clip for density.",
    ),
    S(
        name="Djent",
        category="Metal",
        description="Extended-range chug: tight sub, surgical mids, polished.",
        lufs=-7.5,
        tilt=-1.0,
        sub=2.0,
        low=0.5,
        lowmid=-3.0,
        presence=2.5,
        air=1.5,
        widths=(0, 100, 120, 125),
        mono_freq=140,
        dynamics="dense",
        lra=4.0,
        limiter_release=50,
        tags=("modern", "tight"),
        notes="Mono to 140 Hz for 8-string lows; clean, precise rather than saturated.",
    ),
    S(
        name="Black Metal",
        category="Metal",
        description="Thin, bright and atmospheric; raw tremolo guitars.",
        lufs=-9.5,
        tilt=-0.6,
        sub=-2.0,
        low=-1.0,
        lowmid=-1.0,
        presence=2.0,
        air=2.5,
        widths=(5, 105, 125, 130),
        mono_freq=100,
        dynamics="moderate",
        hp=45,
        lra=5.0,
        saturation=("tube", 3.0),
        limiter_release=80,
        tags=("raw", "bright"),
        notes="Deliberately light low end and bright, wide top for the wall-of-tremolo sound.",
    ),
    # ---- Other -------------------------------------------------------
    S(
        name="Folk",
        category="Other",
        description="Natural, open and dynamic acoustic sound.",
        lufs=-14.0,
        tilt=-2.2,
        sub=-1.5,
        low=0.5,
        lowmid=0.5,
        presence=1.0,
        air=1.0,
        widths=(20, 100, 115, 115),
        mono_freq=80,
        dynamics="gentle",
        hp=35,
        lra=8.0,
        limiter_release=150,
        tags=("acoustic", "natural"),
        notes="Streaming-level loudness, gentle glue, natural width.",
    ),
    S(
        name="Singer-Songwriter",
        category="Other",
        description="Intimate vocal-forward acoustic production.",
        lufs=-14.0,
        tilt=-2.0,
        sub=-1.0,
        low=1.0,
        lowmid=0.0,
        presence=1.5,
        air=1.0,
        widths=(20, 95, 110, 110),
        mono_freq=80,
        dynamics="gentle",
        lra=8.0,
        limiter_release=150,
        tags=("acoustic", "vocal"),
        notes="Vocal presence, light compression and plenty of headroom.",
    ),
    S(
        name="Jazz",
        category="Other",
        description="Warm, natural and highly dynamic.",
        lufs=-16.0,
        tilt=-2.4,
        sub=0.0,
        low=1.5,
        lowmid=0.5,
        presence=0.0,
        air=0.5,
        widths=(40, 100, 110, 110),
        mono_freq=0,
        dynamics="none",
        lra=10.0,
        limiter_release=200,
        linear_phase=True,
        tags=("acoustic", "dynamic"),
        notes="No compression, linear-phase EQ, no mono-maker; the limiter only catches peaks.",
    ),
    S(
        name="Classical",
        category="Other",
        description="Transparent, full dynamic range.",
        lufs=-20.0,
        tilt=-2.6,
        sub=0.0,
        low=0.5,
        lowmid=0.5,
        presence=-0.5,
        air=0.5,
        widths=(60, 100, 105, 105),
        mono_freq=0,
        dynamics="none",
        hp=20,
        lra=15.0,
        limiter_release=300,
        linear_phase=True,
        tags=("acoustic", "dynamic", "transparent"),
        notes="Minimal processing: linear-phase EQ, no compression, safety limiting only.",
    ),
    S(
        name="Country",
        category="Other",
        description="Clear vocals, bright acoustic and electric guitars.",
        lufs=-10.0,
        tilt=-1.5,
        sub=0.5,
        low=1.0,
        lowmid=-0.5,
        presence=2.0,
        air=2.0,
        widths=(0, 100, 115, 120),
        mono_freq=100,
        dynamics="moderate",
        lra=6.0,
        saturation=("tape", 2.0),
        limiter_release=100,
        tags=("vocal", "bright"),
        notes="Modern country: radio-ready presence and air with a controlled low end.",
    ),
    S(
        name="Reggae",
        category="Other",
        description="Deep, round bass and laid-back, darker top end.",
        lufs=-10.0,
        tilt=-2.5,
        sub=4.0,
        low=3.0,
        lowmid=-0.5,
        presence=-0.5,
        air=0.0,
        widths=(0, 90, 105, 110),
        mono_freq=130,
        dynamics="moderate",
        lra=6.0,
        saturation=("tape", 3.0),
        limiter_release=120,
        tags=("bass", "warm"),
        notes="Bass-forward balance with relaxed highs and mono low end.",
    ),
    S(
        name="Afrobeats",
        category="Other",
        description="Bouncy percussion, warm sub and bright vocals.",
        lufs=-8.0,
        tilt=-1.3,
        sub=3.5,
        low=1.5,
        lowmid=-1.5,
        presence=1.5,
        air=2.0,
        widths=(0, 100, 120, 125),
        mono_freq=120,
        dynamics="punchy",
        lra=5.0,
        limiter_release=70,
        tags=("percussive", "bright"),
        notes="Punchy dynamics keep the percussion bouncing; bright vocal presence.",
    ),
    S(
        name="Latin / Reggaeton",
        category="Other",
        description="Dembow drive: strong kick and sub, loud and bright.",
        lufs=-7.5,
        tilt=-1.2,
        sub=4.0,
        low=1.5,
        lowmid=-2.0,
        presence=1.5,
        air=2.0,
        widths=(0, 95, 115, 120),
        mono_freq=130,
        dynamics="dense",
        lra=4.5,
        saturation=("soft_clip", 2.0),
        limiter_release=50,
        tags=("loud", "club"),
        notes="Club-loud with a strong dembow kick; mono below 130 Hz.",
    ),
]
del S

# ---- dynamics templates (assume a mix around -18 LUFS) --------------------
DYNAMICS = {
    "none": {"mb": None, "glue": None},
    "gentle": {
        "mb": {
            "threshold": (-26, -28, -30, -32),
            "ratio": 1.5,
            "attack": (30, 20, 15, 10),
            "release": (250, 200, 150, 120),
        },
        "glue": {"threshold": -24.0, "ratio": 1.5, "attack": 30.0, "release": 300.0},
    },
    "moderate": {
        "mb": {
            "threshold": (-22, -24, -26, -28),
            "ratio": 2.0,
            "attack": (25, 15, 10, 5),
            "release": (200, 150, 120, 100),
        },
        "glue": {"threshold": -20.0, "ratio": 2.0, "attack": 10.0, "release": 200.0},
    },
    "punchy": {
        "mb": {
            "threshold": (-20, -24, -26, -28),
            "ratio": 2.5,
            "attack": (20, 20, 10, 5),
            "release": (120, 120, 100, 80),
        },
        "glue": {"threshold": -18.0, "ratio": 4.0, "attack": 30.0, "release": 100.0},
    },
    "dense": {
        "mb": {"threshold": (-22, -24, -26, -28), "ratio": 3.0, "attack": (8, 5, 3, 2), "release": (100, 80, 60, 50)},
        "glue": {"threshold": -16.0, "ratio": 4.0, "attack": 10.0, "release": 80.0},
    },
}


def _gauss(f: np.ndarray, fc: float, width_oct: float) -> np.ndarray:
    return np.exp(-0.5 * (np.log2(f / fc) / width_oct) ** 2)


def target_curve(spec: GenreSpec, freqs: np.ndarray | None = None) -> np.ndarray:
    """Third-octave target curve (dB, normalised) for a genre."""
    f = np.array(SPECTRUM_FREQS) if freqs is None else np.asarray(freqs, dtype=float)
    db = (
        spec.tilt * np.log2(np.maximum(f, 100.0) / 1000.0)
        - 9.0 * np.maximum(0.0, np.log2(40.0 / f))
        + spec.sub * _gauss(f, 50.0, 0.7)
        + spec.low * _gauss(f, 130.0, 0.6)
        + spec.lowmid * _gauss(f, 400.0, 0.8)
        + spec.presence * _gauss(f, 3500.0, 0.8)
        + spec.air / (1.0 + (11000.0 / f) ** 4)
        - 12.0 * np.maximum(0.0, np.log2(spec.hp / f))
        - 18.0 * np.maximum(0.0, np.log2(f / spec.lp))
    )
    return normalise_spectrum(f, db)


def crest_for(spec: GenreSpec) -> float:
    return round(spec.ceiling - spec.lufs + 2.5, 1)


def build_chain(spec: GenreSpec) -> MasteringChain:
    c = MasteringChain.default()
    r = lambda v: round(float(v), 1)  # noqa: E731

    # EQ: broad moves following ~40 % of the curve's features.
    eq = c.find("eq")
    eq.params.update({"mode": "linear" if spec.linear_phase else "minimum"})
    bands = [
        ("low_cut", spec.hp, 0.0, 0.7071, True),
        ("low_shelf", 60.0, EQ_FRACTION * spec.sub, 0.7071, spec.sub != 0),
        ("bell", 130.0, EQ_FRACTION * spec.low, 0.9, spec.low != 0),
        ("bell", 400.0, EQ_FRACTION * spec.lowmid, 0.8, spec.lowmid != 0),
        ("bell", 3500.0, EQ_FRACTION * spec.presence, 0.8, spec.presence != 0),
        ("high_shelf", 11000.0, EQ_FRACTION * spec.air, 0.7071, spec.air != 0),
        ("high_cut", spec.lp, 0.0, 0.7071, spec.lp < 20000),
    ]
    for i, (kind, freq, gain, q, on) in enumerate(bands, start=1):
        eq.params.update(
            {f"b{i}_on": bool(on), f"b{i}_type": kind, f"b{i}_freq": freq, f"b{i}_gain": r(gain), f"b{i}_q": q}
        )

    dyn = DYNAMICS[spec.dynamics]
    if dyn["mb"]:
        mb = c.find("multiband")
        for i in range(4):
            mb.params.update(
                {
                    f"b{i + 1}_threshold": dyn["mb"]["threshold"][i],
                    f"b{i + 1}_ratio": dyn["mb"]["ratio"],
                    f"b{i + 1}_attack": dyn["mb"]["attack"][i],
                    f"b{i + 1}_release": dyn["mb"]["release"][i],
                }
            )
    else:
        c.set_bypass("multiband", True)
    if dyn["glue"]:
        c.find("glue").params.update({**dyn["glue"], "sc_hpf": 80.0})
    else:
        c.set_bypass("glue", True)

    if spec.saturation:
        mode, drive = spec.saturation
        c.set_bypass("saturation", False)
        c.find("saturation").params.update({"mode": mode, "drive_db": drive, "output_db": r(-drive / 4)})

    im = c.find("imager")
    im.params.update({f"width_b{i + 1}": w for i, w in enumerate(spec.widths)})
    im.params.update({"mono_maker": spec.mono_freq > 0, "mono_freq": spec.mono_freq or 100.0})

    lim = c.find("limiter")
    lim.params.update(
        {
            "ceiling_db": spec.ceiling,
            "release_ms": spec.limiter_release,
            # First guess assuming a ~-18 LUFS mix; auto-gain refines it per track.
            "drive_db": r(min(24.0, max(-12.0, spec.lufs + 18.0 - 2.0))),
        }
    )
    return c


def build_preset(spec: GenreSpec) -> Preset:
    target = TargetProfile(
        lufs=spec.lufs,
        true_peak_ceiling=spec.ceiling,
        crest_db=crest_for(spec),
        lra_lu=spec.lra,
        spectrum=[round(float(v), 2) for v in target_curve(spec)],
        band_width=[float(w) for w in spec.widths],
        mono_freq=spec.mono_freq,
    )
    return Preset(
        id=f"{slugify(spec.category)}/{slugify(spec.name)}",
        name=spec.name,
        category=spec.category,
        description=spec.description,
        notes=spec.notes,
        tags=list(spec.tags),
        author="MasterMind factory",
        factory=True,
        chain=build_chain(spec).to_dict(),
        target=target,
    )


def build_all() -> list[Preset]:
    return [build_preset(s) for s in FACTORY_SPECS]
