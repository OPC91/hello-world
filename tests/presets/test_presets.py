import itertools
import json

import numpy as np
import pytest

from mastermind.dsp.metering.analysis import analyze
from mastermind.engine.offline import render
from mastermind.presets.factory_specs import FACTORY_SPECS, build_all, target_curve
from mastermind.presets.schema import PRESET_FORMAT_VERSION, SPECTRUM_FREQS, Preset, load_preset
from mastermind.presets.store import FAVORITES, PresetLibrary, load_factory_presets
from tests.conftest import music_like

EXPECTED = {
    "Electronic": {
        "House",
        "Deep House",
        "Tech House",
        "Techno",
        "Trance",
        "Dubstep",
        "Drum & Bass",
        "Future Bass",
        "Lo-fi",
        "Ambient",
    },
    "Hip-Hop": {"Boom Bap", "Trap", "Drill", "R&B", "Neo-Soul"},
    "Pop": {"Mainstream Pop", "Indie Pop", "K-Pop", "Synthpop"},
    "Rock": {"Classic Rock", "Indie Rock", "Punk", "Alternative"},
    "Metal": {"Heavy Metal", "Metalcore", "Djent", "Black Metal"},
    "Other": {"Folk", "Singer-Songwriter", "Jazz", "Classical", "Country", "Reggae", "Afrobeats", "Latin / Reggaeton"},
}


@pytest.fixture
def lib(tmp_path):
    return PresetLibrary(tmp_path)


def test_all_requested_genres_ship():
    shipped = load_factory_presets()
    by_cat: dict[str, set[str]] = {}
    for p in shipped:
        by_cat.setdefault(p.category, set()).add(p.name)
    assert by_cat == EXPECTED
    assert len(shipped) == 35


def test_shipped_json_matches_generator():
    """tools/build_factory_presets.py must have been run after editing factory_specs.py."""
    shipped = {p.id: json.loads(p.model_dump_json()) for p in load_factory_presets()}
    built = {p.id: json.loads(p.model_dump_json()) for p in build_all()}
    assert shipped == built


def test_presets_are_distinct():
    presets = build_all()
    for a, b in itertools.combinations(presets, 2):
        curve_diff = np.max(np.abs(np.array(a.target.spectrum) - np.array(b.target.spectrum)))
        chain_same = a.chain == b.chain
        assert not chain_same, (a.name, b.name)
        assert curve_diff > 0.5 or a.target.lufs != b.target.lufs, (a.name, b.name)


def test_targets_are_plausible():
    for spec, p in zip(FACTORY_SPECS, build_all(), strict=True):
        t = p.target
        assert -21 <= t.lufs <= -6
        assert t.crest_db == pytest.approx(t.true_peak_ceiling - t.lufs + 2.5)
        assert len(t.spectrum) == len(SPECTRUM_FREQS) == 31
        f = np.array(SPECTRUM_FREQS)
        sel = (f >= 100) & (f <= 10000)
        assert 10 * np.log10(np.mean(10 ** (np.array(t.spectrum)[sel] / 10))) == pytest.approx(0.0, abs=0.05)
        assert t.band_width[0] <= t.band_width[3]  # lows never wider than highs
        # Curves fall off at the extremes, peak somewhere in the bass.
        curve = target_curve(spec)
        assert curve[0] < curve[np.argmin(np.abs(f - 60))]
        assert curve[-1] < curve[np.argmin(np.abs(f - 1000))] + 3


def test_genre_character_is_reflected():
    t = {p.name: p.target for p in build_all()}
    f = np.array(SPECTRUM_FREQS)
    k50 = np.argmin(np.abs(f - 50))
    assert t["Trap"].spectrum[k50] > t["Classic Rock"].spectrum[k50] + 5  # 808 vs vintage rock
    assert t["Classical"].lufs < t["Jazz"].lufs < t["Folk"].lufs < t["House"].lufs
    assert t["Classical"].mono_freq == 0 and t["Techno"].mono_freq >= 120
    assert t["Future Bass"].band_width[3] > t["Boom Bap"].band_width[3]
    lofi = t["Lo-fi"].spectrum
    assert lofi[-1] < t["Trance"].spectrum[-1] - 10  # rolled-off top


@pytest.mark.parametrize("pid", ["electronic/techno", "other/classical", "hip-hop/trap", "metal/black-metal"])
def test_factory_chains_render_cleanly(pid, lib):
    p = lib.get(pid)
    x = music_like(4.0, 48000) * 0.25
    y = render(p.build_chain(), x, 48000)
    a = analyze(y, 48000)
    assert np.all(np.isfinite(y))
    assert a.true_peak_dbtp <= p.target.true_peak_ceiling


def test_version_handling():
    p = build_all()[0]
    data = json.loads(p.model_dump_json())
    assert load_preset(data).format_version == PRESET_FORMAT_VERSION
    data["format_version"] = PRESET_FORMAT_VERSION + 1
    with pytest.raises(ValueError):
        load_preset(data)
    del data["format_version"]  # pre-versioned files are treated as v1
    assert load_preset(data).name == p.name


def test_invalid_presets_rejected():
    good = json.loads(build_all()[0].model_dump_json())
    for mutate in (
        lambda d: d["target"].update(spectrum=[0.0] * 5),
        lambda d: d["target"].update(band_width=[100, 100, 100, 300]),
        lambda d: d["chain"]["slots"].append({"type": "no_such_module"}),
        lambda d: d.update(id="Bad Id!"),
    ):
        d = json.loads(json.dumps(good))
        mutate(d)
        with pytest.raises(ValueError):
            load_preset(d)


def test_search_and_categories(lib):
    assert {p.name for p in lib.search("house")} == {"House", "Deep House", "Tech House"}
    assert {p.name for p in lib.search("", "Metal")} == EXPECTED["Metal"]
    assert {p.name for p in lib.search("bright wide")} >= {"Trance", "Future Bass"}
    assert lib.search("zzzz") == []
    assert lib.categories()[:6] == ["Electronic", "Hip-Hop", "Pop", "Rock", "Metal", "Other"]


def test_favorites_persist(tmp_path):
    lib = PresetLibrary(tmp_path)
    assert lib.toggle_favorite("electronic/house") is True
    lib.set_favorite("other/jazz", True)
    lib2 = PresetLibrary(tmp_path)
    assert {p.id for p in lib2.search(category=FAVORITES)} == {"electronic/house", "other/jazz"}
    lib2.toggle_favorite("electronic/house")
    assert {p.id for p in PresetLibrary(tmp_path).search(favorites_only=True)} == {"other/jazz"}


def test_save_import_export_delete(tmp_path):
    lib = PresetLibrary(tmp_path / "home")
    base = lib.get("pop/k-pop")
    mine = lib.save_user(base.model_copy(update={"name": "My K-Pop", "id": "x"}))
    assert mine.id == "user/my-k-pop" and not mine.factory
    again = lib.save_user(base.model_copy(update={"name": "My K-Pop"}))
    assert again.id == "user/my-k-pop-2"  # never overwrites by accident

    out = lib.export(mine.id, tmp_path / "exported.json")
    other = PresetLibrary(tmp_path / "other-home")
    imported = other.import_file(out)
    assert imported.name == "My K-Pop" and imported.chain == base.chain
    assert PresetLibrary(tmp_path / "other-home").get(imported.id).name == "My K-Pop"

    # Importing a factory preset file never shadows the factory one.
    lib.export("electronic/house", tmp_path / "house.json")
    dup = lib.import_file(tmp_path / "house.json")
    assert dup.id.startswith("user/") and lib.get("electronic/house").factory

    lib.set_favorite(mine.id, True)
    lib.delete_user(mine.id)
    assert mine.id not in {p.id for p in PresetLibrary(tmp_path / "home").all()}
    assert not lib.is_favorite(mine.id)
    with pytest.raises(KeyError):
        lib.delete_user("electronic/house")


def test_bad_user_file_is_reported_not_fatal(tmp_path):
    (tmp_path / "presets").mkdir()
    (tmp_path / "presets" / "broken.json").write_text("{not json")
    lib = PresetLibrary(tmp_path)
    assert len(lib.all()) == 35 and lib.errors


def test_preset_model_round_trip():
    p = build_all()[5]
    assert Preset.model_validate_json(p.to_json()) == p
