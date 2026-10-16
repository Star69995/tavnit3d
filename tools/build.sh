#!/usr/bin/env bash
# Build (and optionally test / package) on Windows inside MSYS2 UCRT64.
# Usage: tools/build.sh [test] [dist] [installer]
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
        installer)
            # needs the dist step first; Inno Setup 6 (winget install JRSoftware.InnoSetup)
            ver=$(grep -o 'app_version = "[^"]*"' src/util/branding.hpp | cut -d'"' -f2)
            iscc="$LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe"
            [ -x "$iscc" ] || iscc="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
            "$iscc" //Q "//DAppVersion=$ver" tools/installer/tavnit3d.iss
            ls -la "dist/Tavnit3D-$ver-setup.exe" ;;
    esac
done
