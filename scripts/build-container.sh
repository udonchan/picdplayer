#!/usr/bin/env bash
set -euo pipefail

# PiCDPlayer Linux/aarch64 build script
#
# Builds PiCDPlayer inside the picdplayer-build Docker image and
# installs the result into a local DESTDIR staging tree.
#
# Output:
#   build-container/   CMake build tree
#   stage/             filesystem tree ready for deployment

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

IMAGE="${PICDPLAYER_BUILD_IMAGE:-picdplayer-build}"
BUILD_DIR="build-container"
STAGE_DIR="${REPO_ROOT}/stage"

echo "==> PiCDPlayer container build"
echo "    repository : ${REPO_ROOT}"
echo "    image      : ${IMAGE}"
echo "    build dir  : ${REPO_ROOT}/${BUILD_DIR}"
echo "    stage dir  : ${STAGE_DIR}"
echo

# ---------------------------------------------------------------------------
# Preconditions
# ---------------------------------------------------------------------------

if ! docker image inspect "${IMAGE}" >/dev/null 2>&1; then
    echo "ERROR: Docker image '${IMAGE}' does not exist."
    echo
    echo "Build it first with:"
    echo "  docker build -t ${IMAGE} ."
    exit 1
fi

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------

echo "==> Configuring CMake"

docker run --rm \
    -v "${REPO_ROOT}:/src" \
    -w /src \
    "${IMAGE}" \
    cmake -S . -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_METADATA=ON \
        -DENABLE_API=ON \
        -DINSTALL_SYSTEMD_UNIT=ON \
        -DINSTALL_SYSTEMD_KIOSK_UNIT=ON \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DPICDPLAYER_SERVICE_USER=picdplayer

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

echo "==> Building"

docker run --rm \
    -v "${REPO_ROOT}:/src" \
    -w /src \
    "${IMAGE}" \
    sh -c "cmake --build '${BUILD_DIR}' -j\$(nproc)"

# ---------------------------------------------------------------------------
# Stage install
# ---------------------------------------------------------------------------

echo "==> Preparing staging directory"

rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}"

echo "==> Installing into staging directory"

docker run --rm \
    -v "${REPO_ROOT}:/src" \
    -w /src \
    "${IMAGE}" \
    sh -c "DESTDIR=/src/stage cmake --install '${BUILD_DIR}'"

# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------

BINARY="${STAGE_DIR}/usr/local/bin/cdplayerd"

if [[ ! -x "${BINARY}" ]]; then
    echo "ERROR: expected installed binary not found:"
    echo "  ${BINARY}"
    exit 1
fi

echo "==> Installed files"
find "${STAGE_DIR}" \( -type f -o -type l \) -print

echo
echo "==> Build completed successfully"
echo "    staged installation: ${STAGE_DIR}"
echo
echo "Deploy with:"
echo "    ./scripts/deploy.sh"
