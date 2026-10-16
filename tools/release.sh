#!/usr/bin/env bash
# Builds, tests and publishes a Tavnit 3D release on GitHub (the repo in
# update_repo, src/util/branding.hpp). Running copies pick it up as an update
# and the website lists it; nothing else needs deploying.
# Usage: tools/release.sh [NOTES_FILE] [--yes]
#   NOTES_FILE  release notes (Markdown); default: commit subjects since the last release
#   --yes       don't ask before publishing
# Bump app_version in branding.hpp, commit and push first.
set -e
cd "$(dirname "$0")/.."

notes_file="" yes=""
for arg in "$@"; do
    case "$arg" in
        --yes) yes=1 ;;
        *) notes_file="$arg" ;;
    esac
done

ver=$(grep -o 'app_version = "[^"]*"' src/util/branding.hpp | cut -d'"' -f2)
repo=$(grep -o 'update_repo = "[^"]*"' src/util/branding.hpp | cut -d'"' -f2)
tag="v$ver"
[ -n "$repo" ] || { echo "update_repo is empty in branding.hpp"; exit 1; }

# tags are checked on GitHub only: the local clone also has upstream Dune 3D's tags
if gh release view "$tag" -R "$repo" >/dev/null 2>&1; then
    echo "$tag is already released - bump app_version in src/util/branding.hpp"; exit 1
fi
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "uncommitted changes - commit them first"; exit 1
fi
git fetch -q origin
head=$(git rev-parse HEAD)
if ! git branch -r --contains "$head" | grep -q "origin/"; then
    echo "HEAD isn't pushed to origin - push it first"; exit 1
fi

# through a MSYS2 UCRT64 login shell like tools\build.cmd: the toolchain needs its environment
MSYSTEM=UCRT64 /c/msys64/usr/bin/bash.exe -lc "cd '$(pwd -W 2>/dev/null || pwd)' && tools/build.sh test dist installer"
setup="dist/Tavnit3D-$ver-setup.exe"
portable="dist/Tavnit3D-$ver-portable.zip"
latest_zip=$(ls -t dist/dune3d-*.zip | head -1)
cp "$latest_zip" "$portable"

notes=$(mktemp)
if [ -n "$notes_file" ]; then
    cp "$notes_file" "$notes"
else
    last=$(gh release list -R "$repo" -L 1 --exclude-drafts --exclude-pre-releases --json tagName -q '.[0].tagName')
    range="$head"
    if [ -n "$last" ]; then
        git fetch -q origin "+refs/tags/$last:refs/tavnit-releases/$last"
        range="refs/tavnit-releases/$last..$head"
    fi
    git log --no-merges --format='- %s' "$range" -- . ':!site' ':!tools' ':!CLAUDE.md' ':!README.md' > "$notes"
fi

echo "=== $repo $tag"
cat "$notes"
ls -la "$setup" "$portable"
if [ -z "$yes" ]; then
    read -r -p "Publish? [y/N] " answer
    [ "$answer" = "y" ] || { echo "not published"; exit 1; }
fi
gh release create "$tag" "$setup" "$portable" -R "$repo" --target "$head" \
    --title "Tavnit 3D $ver" --notes-file "$notes"
rm -f "$notes"
