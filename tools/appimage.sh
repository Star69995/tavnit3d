#!/usr/bin/env bash
# Builds dist/Tavnit3D-<version>-<arch>.AppImage: one file that runs on most
# Linux systems (needs glibc as new as the build machine's: build on Ubuntu 24.04).
# Usage: tools/appimage.sh          (needs the build dependencies of .github/workflows/all.yml, curl, patchelf)
# The release job (.github/workflows/linux-appimage.yml) runs this and attaches
# the file to the GitHub release, where the in-app updater finds it.
# Env: BUILD_DIR (default build-linux), TOOLS_DIR (linuxdeploy, default build-linux-tools)
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
ARCH=$(uname -m)
VERSION=$(sed -n 's/.*app_version = "\(.*\)";.*/\1/p' src/util/branding.hpp)
BUILD_DIR=${BUILD_DIR:-build-linux}
TOOLS_DIR=$(realpath -m "${TOOLS_DIR:-build-linux-tools}")
APPDIR=$ROOT/dist/AppDir
OUT=Tavnit3D-$VERSION-$ARCH.AppImage

if [ ! -d "$BUILD_DIR" ]; then
    meson setup "$BUILD_DIR" --buildtype=release --prefix=/usr
fi
ninja -C "$BUILD_DIR"

rm -rf "$APPDIR"
DESTDIR=$APPDIR meson install -C "$BUILD_DIR" --no-rebuild
# the upstream names; ours come with tools/appimage
rm -f "$APPDIR"/usr/share/applications/org.dune3d.dune3d.desktop "$APPDIR"/usr/share/metainfo/org.dune3d.dune3d.metainfo.xml

mkdir -p "$TOOLS_DIR"
fetch() { # url file
    [ -s "$TOOLS_DIR/$2" ] || curl -fsSL -o "$TOOLS_DIR/$2" "$1"
    chmod +x "$TOOLS_DIR/$2"
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage" linuxdeploy
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-$ARCH.AppImage" linuxdeploy-plugin-appimage
fetch "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" linuxdeploy-plugin-gtk.sh

mkdir -p dist
export PATH="$TOOLS_DIR:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1 # no FUSE needed (CI, containers)
export DEPLOY_GTK_VERSION=4
export LINUXDEPLOY_OUTPUT_VERSION=$VERSION
export OUTPUT=$OUT
(cd dist && "$TOOLS_DIR/linuxdeploy" --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/dune3d" \
    --desktop-file "$ROOT/tools/appimage/tavnit3d.desktop" \
    --icon-file "$ROOT/src/icons/scalable/apps/dune3d.svg" --icon-filename dune3d \
    --plugin gtk --output appimage)
echo "built dist/$OUT"
