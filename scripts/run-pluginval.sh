#!/usr/bin/env bash
# Validates the built SEAM VST3 with pluginval (Linux/macOS).
# Usage: scripts/run-pluginval.sh [strictness=10] [build dir=build]
# Needs pluginval on PATH or in $PLUGINVAL. On a headless Linux box, run under xvfb-run (editor tests need a display).
set -euo pipefail
strictness="${1:-10}"
build="${2:-build}"
root="$(cd "$(dirname "$0")/.." && pwd)"
pluginval="${PLUGINVAL:-$(command -v pluginval || true)}"
if [[ -z "$pluginval" ]]; then echo "pluginval not found: set PLUGINVAL or put it on PATH" >&2; exit 2; fi

plugin="$(find "$root/$build" -type d -name 'SEAM.vst3' -path '*VST3*' | head -n 1)"
if [[ -z "$plugin" ]]; then echo "SEAM.vst3 not found under $build: build first" >&2; exit 2; fi

logs="$root/pluginval-logs"
mkdir -p "$logs"
echo "pluginval: $plugin at strictness $strictness"
"$pluginval" --strictness-level "$strictness" --validate-in-process --output-dir "$logs" --timeout-ms 600000 --validate "$plugin"
echo "pluginval PASSED at strictness $strictness"
