"""Preset library: read-only factory presets + a user directory.

User data lives in ``$MASTERMIND_HOME`` (default ``~/.mastermind``)::

    presets/<slug>.json     user presets (saved, imported, learned)
    favorites.json          list of favourite preset ids
"""

from __future__ import annotations

import json
import os
from importlib import resources
from pathlib import Path

from mastermind.presets.schema import CATEGORIES, Preset, load_preset, slugify

FAVORITES = "Favorites"


def user_home() -> Path:
    return Path(os.environ.get("MASTERMIND_HOME", Path.home() / ".mastermind"))


def load_factory_presets() -> list[Preset]:
    folder = resources.files("mastermind.presets") / "factory"
    out = []
    for entry in sorted(folder.iterdir(), key=lambda e: e.name):
        if entry.name.endswith(".json"):
            p = load_preset(entry.read_text())
            p.factory = True
            out.append(p)
    return out


class PresetLibrary:
    def __init__(self, home: Path | None = None):
        self.home = Path(home) if home else user_home()
        self.user_dir = self.home / "presets"
        self._fav_path = self.home / "favorites.json"
        self.reload()

    # ---- loading --------------------------------------------------------
    def reload(self) -> None:
        self.factory = {p.id: p for p in load_factory_presets()}
        self.user: dict[str, Preset] = {}
        self.errors: list[str] = []
        if self.user_dir.is_dir():
            for f in sorted(self.user_dir.glob("*.json")):
                try:
                    p = load_preset(f.read_text())
                    p.factory = False
                    self.user[p.id] = p
                except Exception as e:  # keep going; report bad files
                    self.errors.append(f"{f.name}: {e}")
        try:
            self.favorites: set[str] = set(json.loads(self._fav_path.read_text()))
        except (OSError, ValueError):
            self.favorites = set()

    def all(self) -> list[Preset]:
        return list(self.factory.values()) + list(self.user.values())

    def get(self, preset_id: str) -> Preset:
        if preset_id in self.user:
            return self.user[preset_id]
        return self.factory[preset_id]

    def categories(self) -> list[str]:
        present = {p.category for p in self.all()}
        return [c for c in CATEGORIES if c in present] + sorted(present - set(CATEGORIES))

    # ---- browsing -------------------------------------------------------
    def search(self, query: str = "", category: str | None = None, favorites_only: bool = False) -> list[Preset]:
        """Case-insensitive search over name, category, tags and description.

        All whitespace-separated terms must match. ``category`` may be
        :data:`FAVORITES` as a shortcut for ``favorites_only``.
        """
        if category == FAVORITES:
            category, favorites_only = None, True
        terms = query.lower().split()
        out = []
        for p in self.all():
            if category and p.category != category:
                continue
            if favorites_only and p.id not in self.favorites:
                continue
            hay = " ".join([p.name, p.category, p.description, " ".join(p.tags)]).lower()
            if all(t in hay for t in terms):
                out.append(p)
        return out

    # ---- favourites -----------------------------------------------------
    def is_favorite(self, preset_id: str) -> bool:
        return preset_id in self.favorites

    def set_favorite(self, preset_id: str, on: bool) -> None:
        (self.favorites.add if on else self.favorites.discard)(preset_id)
        self.home.mkdir(parents=True, exist_ok=True)
        self._fav_path.write_text(json.dumps(sorted(self.favorites), indent=2))

    def toggle_favorite(self, preset_id: str) -> bool:
        on = not self.is_favorite(preset_id)
        self.set_favorite(preset_id, on)
        return on

    # ---- user presets ---------------------------------------------------
    def unique_id(self, name: str) -> str:
        base = f"user/{slugify(name)}"
        pid, n = base, 2
        while pid in self.user or pid in self.factory:
            pid, n = f"{base}-{n}", n + 1
        return pid

    def _path(self, preset_id: str) -> Path:
        return self.user_dir / (preset_id.replace("/", "-") + ".json")

    def save_user(self, preset: Preset, overwrite: bool = False) -> Preset:
        """Store a user preset. A new id is assigned unless ``overwrite`` targets an existing user preset."""
        if preset.id in self.factory or not (overwrite and preset.id in self.user):
            preset = preset.model_copy(update={"id": self.unique_id(preset.name)})
        preset = preset.model_copy(update={"factory": False})
        self.user_dir.mkdir(parents=True, exist_ok=True)
        self._path(preset.id).write_text(preset.to_json() + "\n")
        self.user[preset.id] = preset
        return preset

    def delete_user(self, preset_id: str) -> None:
        if preset_id not in self.user:
            raise KeyError(f"{preset_id} is not a user preset")
        self._path(preset_id).unlink(missing_ok=True)
        del self.user[preset_id]
        if preset_id in self.favorites:
            self.set_favorite(preset_id, False)

    def import_file(self, path: str | Path) -> Preset:
        """Validate a preset file and add it to the user library (new id if it collides)."""
        p = load_preset(Path(path).read_text())
        return self.save_user(p)

    def export(self, preset_id: str, path: str | Path) -> Path:
        path = Path(path)
        path.write_text(self.get(preset_id).to_json() + "\n")
        return path
