"""``mastermind-cli``: batch mastering and analysis from the command line.

Examples::

    mastermind-cli master mix.wav -o master.wav --bits 24 --ceiling -1 --gain 6
    mastermind-cli master mix.wav -o master.wav --chain my_chain.json
    mastermind-cli chain-template > my_chain.json
    mastermind-cli analyze master.wav --json
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

from mastermind.dsp.chain import MasteringChain
from mastermind.dsp.metering.targets import LOUDNESS_TARGETS
from mastermind.engine.offline import render_to_file
from mastermind.io.audio_file import load_audio
from mastermind.io.export import BIT_DEPTHS
from mastermind.report import analyse, format_report


def _load_chain(path: str | None) -> MasteringChain:
    if path is None:
        return MasteringChain.default()
    return MasteringChain.from_dict(json.loads(Path(path).read_text()))


def cmd_master(args: argparse.Namespace) -> int:
    audio = load_audio(args.input)
    chain = _load_chain(args.chain)
    if args.ceiling is not None:
        chain.set_param("limiter", "ceiling_db", args.ceiling)
    if args.gain is not None:
        chain.set_param("limiter", "drive_db", args.gain)
    out = Path(args.output) if args.output else Path(args.input).with_name(Path(args.input).stem + "_master.wav")

    t0 = time.perf_counter()
    last = [-1]

    def progress(frac: float) -> None:
        pct = int(frac * 100)
        if not args.quiet and pct // 10 != last[0]:
            last[0] = pct // 10
            print(f"\r  rendering... {pct:3d}%", end="", file=sys.stderr, flush=True)

    y = render_to_file(
        chain, audio.samples, audio.sample_rate, out, args.bits, dither=not args.no_dither, progress=progress
    )
    if not args.quiet:
        print(f"\r  rendered in {time.perf_counter() - t0:.1f}s -> {out}", file=sys.stderr)

    before = analyse(audio.samples, audio.sample_rate)
    after = analyse(y, audio.sample_rate)
    target = _target(args)
    ceiling = chain.find("limiter").get_param("ceiling_db")
    if args.json:
        print(json.dumps({"input": before.summary(), "output": after.summary(), "file": str(out)}, indent=2))
    else:
        print(format_report(before, f"Input: {args.input}", target))
        print(format_report(after, f"Output: {out}", target, ceiling))
    return 0


def _target(args: argparse.Namespace) -> float | None:
    if getattr(args, "target_lufs", None) is not None:
        return args.target_lufs
    if getattr(args, "target", None):
        return LOUDNESS_TARGETS[args.target]
    return None


def cmd_analyze(args: argparse.Namespace) -> int:
    for path in args.inputs:
        audio = load_audio(path)
        rep = analyse(audio.samples, audio.sample_rate)
        if args.json:
            print(json.dumps({"file": path, **rep.summary()}))
        else:
            print(format_report(rep, path, _target(args)))
    return 0


def cmd_chain_template(args: argparse.Namespace) -> int:
    print(json.dumps(MasteringChain.default().to_dict(), indent=2))
    return 0


def _add_target_args(p: argparse.ArgumentParser) -> None:
    named = [k for k, v in LOUDNESS_TARGETS.items() if v is not None]
    p.add_argument("--target", choices=named, help="loudness target to compare against")
    p.add_argument("--target-lufs", type=float, help="custom loudness target")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="mastermind-cli", description="MasterMind batch mastering")
    sub = p.add_subparsers(dest="command", required=True)

    m = sub.add_parser("master", help="master a file through the chain")
    m.add_argument("input")
    m.add_argument("-o", "--output")
    m.add_argument("--chain", help="chain JSON (see chain-template)")
    m.add_argument("--bits", choices=BIT_DEPTHS, default="24")
    m.add_argument("--no-dither", action="store_true")
    m.add_argument("--ceiling", type=float, help="limiter ceiling in dBTP")
    m.add_argument("--gain", type=float, help="limiter input gain in dB")
    m.add_argument("--json", action="store_true", help="print the report as JSON")
    _add_target_args(m)
    m.add_argument("-q", "--quiet", action="store_true")
    m.set_defaults(func=cmd_master)

    a = sub.add_parser("analyze", help="print a loudness/peak/correlation report")
    a.add_argument("inputs", nargs="+")
    a.add_argument("--json", action="store_true")
    _add_target_args(a)
    a.set_defaults(func=cmd_analyze)

    t = sub.add_parser("chain-template", help="print the default chain as JSON")
    t.set_defaults(func=cmd_chain_template)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
