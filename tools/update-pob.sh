#!/bin/bash
# Moves the PathOfBuilding submodule to PoB's latest release and stages it.
# usage: tools/update-pob.sh   (then build, test and commit)
set -euo pipefail
cd "$(dirname "$0")/.."

latest="$(gh api repos/PathOfBuildingCommunity/PathOfBuilding/releases/latest --jq .tag_name)"
current="$(git -C PathOfBuilding describe --tags --exact-match 2>/dev/null || git -C PathOfBuilding rev-parse --short HEAD)"
if [ "$current" = "$latest" ]; then
    echo "PathOfBuilding is already at the latest release ($latest)"
    exit 0
fi

echo "Updating PathOfBuilding: $current -> $latest"
git -C PathOfBuilding fetch --depth 1 origin "refs/tags/$latest:refs/tags/$latest"
git -C PathOfBuilding checkout -q "$latest"
git add PathOfBuilding
echo "Staged. Build, run the self test, then commit: \"Bundle PoB ${latest#v}\""
