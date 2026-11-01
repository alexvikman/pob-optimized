#!/bin/bash
# Waits for the GitHub Actions run of a commit (or tag) and reports the result.
# usage: tools/ci-wait.sh <commit-or-tag> [artifact-name dest-dir]
#
# Prints "watching run <id> (<url>)" as soon as the run is found, so a caller can
# check right away that it is waiting on the right thing. Fails loudly if no run
# shows up within 3 minutes, instead of waiting forever.
set -uo pipefail

ref="${1:?usage: ci-wait.sh <commit-or-tag> [artifact dest]}"
sha="$(git rev-parse "$ref^{commit}" 2>/dev/null)" || { echo "error: unknown ref '$ref'"; exit 2; }

# A tag and a branch can point at the same commit; for a tag, take the tag's run.
filter=".headSha==\"$sha\""
if git show-ref --verify --quiet "refs/tags/$ref"; then
    filter="$filter and .headBranch==\"$ref\""
fi

id=""
for _ in $(seq 1 36); do
    id="$(gh run list --limit 20 --json databaseId,headSha,headBranch \
        --jq "[.[] | select($filter)][0].databaseId // empty")"
    [ -n "$id" ] && break
    sleep 5
done
if [ -z "$id" ]; then
    echo "error: no workflow run for $ref ($sha) after 3 minutes"
    exit 3
fi
echo "watching run $id ($(gh run view "$id" --json url --jq .url))"

gh run watch "$id" --interval 20 > /dev/null 2>&1
gh run view "$id" --json conclusion,jobs --jq '"result: \(.conclusion)", (.jobs[] | "  \(.name): \(.conclusion)")'
conclusion="$(gh run view "$id" --json conclusion --jq .conclusion)"

if [ $# -ge 3 ] && [ "$conclusion" = "success" ]; then
    rm -rf "$3"
    gh run download "$id" -n "$2" -D "$3" && echo "downloaded $2 to $3"
fi
[ "$conclusion" = "success" ]
