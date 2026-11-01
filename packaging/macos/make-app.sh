#!/bin/bash
# Builds "Path of Building.app" from a finished build and ad-hoc signs it.
# usage: packaging/macos/make-app.sh <build-dir> <out-dir>
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$(cd "$1" && pwd)"
OUT="$2"
APP="$OUT/Path of Building.app"
EXE="$BUILD/host/PathOfBuilding"

HOST_VERSION="$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$REPO/CMakeLists.txt" | head -1)"
POB_VERSION="$(sed -n 's/.*<Version number="\([^"]*\)".*/\1/p' "$REPO/PathOfBuilding/manifest.xml" | head -1)"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$EXE" "$APP/Contents/MacOS/PathOfBuilding"
sed -e "s/@HOST_VERSION@/$HOST_VERSION/g" -e "s/@POB_VERSION@/$POB_VERSION/g" \
    "$REPO/packaging/macos/Info.plist.in" > "$APP/Contents/Info.plist"
cp "$REPO/packaging/macos/PathOfBuilding.icns" "$APP/Contents/Resources/"
cp "$REPO/LICENSE" "$REPO/LICENSE-THIRD-PARTY.md" "$APP/Contents/Resources/"

# PoB itself, exactly the files its manifest lists (no Windows binaries).
"$EXE" --stage-bundle "$REPO/PathOfBuilding" "$APP/Contents/Resources/PathOfBuilding"

# Ad-hoc signature: required on Apple Silicon, no developer account needed.
codesign --force --deep --sign - --timestamp=none "$APP"
codesign --verify --deep --strict "$APP"
echo "Built $APP (host $HOST_VERSION, PoB $POB_VERSION)"
