#!/usr/bin/env bash
# Build (and optionally test / package) on Windows inside MSYS2 UCRT64.
# Usage: tools/build.sh [test] [dist] [installer]
# (dist/installer build and test the optimised build-release/ instead of build/)
set -e
cd "$(dirname "$0")/.."
# meson's optional-dependency lookup crashes when it can't find a home directory
export HOME="${HOME:-/tmp}" USERPROFILE="${USERPROFILE:-C:\Users\$USERNAME}"
# build/ is for development (debug: no optimisation, assertions). What's handed out
# (dist, installer) is built optimised in build-release/, and tested there.
BUILD_DIR=build
for arg in "$@"; do
    case "$arg" in dist|installer) BUILD_DIR=build-release ;; esac
done
if [ "$BUILD_DIR" = build-release ]; then
    [ -d build-release ] || meson setup build-release --buildtype=release
else
    [ -d build ] || meson setup build
fi
meson compile -C "$BUILD_DIR"
for arg in "$@"; do
    case "$arg" in
        test) meson test -C "$BUILD_DIR" --print-errorlogs ;;
        dist) BUILD_DIR="$BUILD_DIR" ./make_bindist.sh && ./check_dll.sh ;;
        installer)
            # needs the dist step first; Inno Setup 6 (winget install JRSoftware.InnoSetup)
            ver=$(grep -o 'app_version = "[^"]*"' src/util/branding.hpp | cut -d'"' -f2)
            iscc="$LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe"
            [ -x "$iscc" ] || iscc="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
            "$iscc" //Q "//DAppVersion=$ver" tools/installer/tavnit3d.iss
            ls -la "dist/Tavnit3D-$ver-setup.exe" ;;
    esac
done
