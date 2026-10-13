#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE=picdplayer-wpe-source-probe:2.54.0-trixie
VOLUME=${PICDPLAYER_WPE_VOLUME:-picdplayer-wpe-runtime-2.54.0}
SDK="$ROOT/build-wpe-runtime/2.54.0"
[[ $VOLUME =~ ^[a-zA-Z0-9][a-zA-Z0-9_.-]*$ ]] || { echo 'Invalid WPE volume name' >&2; exit 2; }
recipe=$(shasum -a 256 "$ROOT/scripts/wpe-source-probe.Dockerfile" \
    "$ROOT/scripts/wpe-source-probe.sh" "$ROOT/scripts/wpe-source-video-disabled.patch" \
    "$ROOT/scripts/wpe-source-sandbox-link.patch" "$ROOT/scripts/build-wpe-runtime.sh" | cut -d ' ' -f 1 | shasum -a 256)
recipe=${recipe%% *}
if [[ -f "$ROOT/build-wpe-runtime/recipe" && $(<"$ROOT/build-wpe-runtime/recipe") == "$recipe" &&
      -f "$SDK/lib/pkgconfig/wpe-platform-drm-2.0.pc" &&
      -x "$SDK/libexec/wpe-webkit-2.0/WPEWebProcess" ]]; then
    echo '==> Reusing cached WPE SDK'
    exit 0
fi

docker build --platform linux/arm64 -f "$ROOT/scripts/wpe-source-probe.Dockerfile" -t "$IMAGE" "$ROOT"
# BuildKit attestations change the manifest ID even when all source layers match.
identity=$(docker image inspect "$IMAGE" --format '{{json .RootFS.Layers}}')
docker volume create "$VOLUME" >/dev/null
run_probe() {
    docker run --rm --platform linux/arm64 --network none --memory 6g --cpus 2 \
        -v "$VOLUME:/work" "$IMAGE" "$@"
}
if ! docker run --rm --network none -v "$VOLUME:/work:ro" --entrypoint bash "$IMAGE" \
    -c '[[ -f /work/runtime-image && $(</work/runtime-image) == "$1" && -f /work/stage/opt/picdplayer-wpe/2.54.0/lib/pkgconfig/wpe-platform-drm-2.0.pc ]]' bash "$identity"; then
    run_probe configure
    run_probe build
    run_probe stage
    docker run --rm --network none -v "$VOLUME:/work" --entrypoint bash "$IMAGE" \
        -c 'printf "%s\n" "$1" > /work/runtime-image' bash "$identity"
fi

# Export the SDK for read-only mounts into the ordinary application build image.
mkdir -p "$ROOT/build-wpe-runtime"
container=$(docker create -v "$VOLUME:/work:ro" "$IMAGE")
trap 'docker rm "$container" >/dev/null' EXIT
rm -f "$ROOT/build-wpe-runtime/recipe"
rm -rf "$SDK"
docker cp "$container:/work/stage/opt/picdplayer-wpe/2.54.0" "$SDK"
test -f "$SDK/lib/pkgconfig/wpe-webkit-2.0.pc"
printf '%s\n' "$recipe" > "$ROOT/build-wpe-runtime/recipe"
