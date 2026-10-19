#!/usr/bin/env bash
# Runs pluginval at strictness 5 against a built VST3.
#   scripts/pluginval.sh [path/to/pluginval] [build-dir] [config]
set -euo pipefail

PLUGINVAL=${1:-pluginval}
BUILD=${2:-build}
CONFIG=${3:-Release}
PLUGIN="$BUILD/BreakForge_artefacts/$CONFIG/VST3/BreakForge.vst3"

if [[ ! -e "$PLUGIN" ]]; then
  echo "No plugin at $PLUGIN - build first (cmake --build $BUILD --config $CONFIG)" >&2
  exit 1
fi

if [[ "$(uname)" == "Linux" && -z "${DISPLAY:-}" ]]; then
  exec xvfb-run -a "$PLUGINVAL" --strictness-level 5 --validate "$PLUGIN"
fi

exec "$PLUGINVAL" --strictness-level 5 --validate "$PLUGIN"
