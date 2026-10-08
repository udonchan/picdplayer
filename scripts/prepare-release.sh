#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# Only invalidate this command's fixed output; never follow a redirected path.
if [[ -L release-container || ( -e release-container && ! -d release-container ) ]]; then
    echo 'ERROR: release-container must be a real directory' >&2
    exit 1
fi
rm -rf -- "$ROOT/release-container"
commit="$(git rev-parse HEAD)"
ref="${GITHUB_REF:-$(git symbolic-ref -q HEAD || printf detached)}"
set --
if [[ -n "$(git status --porcelain --untracked-files=normal)" ]]; then
    set -- --dirty
fi
docker run --rm -v "$ROOT:/src" -w /src "${PICDPLAYER_BUILD_IMAGE:-picdplayer-build}" \
    python3 scripts/release-artifact.py --commit "$commit" --ref "$ref" "$@"
