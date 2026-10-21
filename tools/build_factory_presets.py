"""Regenerate the factory preset JSON files from mastermind/presets/factory_specs.py.

    uv run python tools/build_factory_presets.py

Writes one file per preset to mastermind/presets/factory/<category>-<name>.json
and removes stale files. tests/presets/test_factory.py fails if the shipped JSON
and this generator disagree.
"""

from __future__ import annotations

from pathlib import Path

from mastermind.presets.factory_specs import build_all

OUT = Path(__file__).resolve().parents[1] / "mastermind" / "presets" / "factory"


def filename(preset_id: str) -> str:
    return preset_id.replace("/", "-") + ".json"


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    presets = build_all()
    wanted = {filename(p.id) for p in presets}
    for stale in OUT.glob("*.json"):
        if stale.name not in wanted:
            stale.unlink()
    for p in presets:
        (OUT / filename(p.id)).write_text(p.to_json() + "\n")
    print(f"wrote {len(presets)} presets to {OUT}")


if __name__ == "__main__":
    main()
