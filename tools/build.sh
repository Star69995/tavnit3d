#!/usr/bin/env bash
# Build (and optionally test / package) on Windows inside MSYS2 UCRT64.
# Usage: tools/build.sh [test] [dist]
set -e
cd "$(dirname "$0")/.."
# meson's optional-dependency lookup crashes when it can't find a home directory
export HOME="${HOME:-/tmp}" USERPROFILE="${USERPROFILE:-C:\Users\$USERNAME}"
[ -d build ] || meson setup build
meson compile -C build
for arg in "$@"; do
    case "$arg" in
        test) meson test -C build --print-errorlogs ;;
        dist) ./make_bindist.sh && ./check_dll.sh ;;
    esac
done
