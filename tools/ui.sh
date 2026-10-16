#!/usr/bin/env bash
# Runs a UI automation script against build/dune3d.exe without touching the
# real mouse/keyboard (the window is parked off-screen). Script commands are
# documented at the top of src/editor/editor_automation.cpp; coordinates are
# window pixels as seen in a "shot".
# Usage: tools/ui.sh SCRIPT [TIMEOUT_SECONDS]
set -e
cd "$(dirname "$0")/.."
export PATH="/c/msys64/ucrt64/bin:$PATH"
DUNE3D_AUTOMATION="$(cygpath -w "$(realpath "$1")")" timeout "${2:-120}" ./build/dune3d.exe 2>&1 | grep '^automation' || true
