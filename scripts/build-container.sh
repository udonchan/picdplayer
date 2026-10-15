#!/usr/bin/env bash
set -euo pipefail

# PiCDPlayer Linux/aarch64 build script
#
# Builds PiCDPlayer inside the picdplayer-build Docker image and
# installs the result into a local DESTDIR staging tree.
#
# Output:
#   build-container/   CMake build tree
#   stage/             filesystem tree for inspection
#   package-container/ Debian package ready for deployment

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

IMAGE="${PICDPLAYER_BUILD_IMAGE:-picdplayer-build}"
BUILD_DIR="build-container"
STAGE_DIR="${REPO_ROOT}/stage"
PACKAGE_DIR="${REPO_ROOT}/package-container"
# Invalidate old deployable output even for invalid profile/option input.
rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}"
trap 'if [[ $? != 0 ]]; then rm -f "${PACKAGE_DIR}"/*.deb; fi' EXIT
ENABLE_PARANOIA="${PICDPLAYER_ENABLE_PARANOIA:-OFF}"
BUILD_TYPE="${PICDPLAYER_BUILD_TYPE:-Release}"
case "${BUILD_TYPE}" in
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *) echo 'ERROR: invalid PICDPLAYER_BUILD_TYPE' >&2; exit 2 ;;
esac

case "${ENABLE_PARANOIA}" in
    ON|OFF) ;;
    *)
        echo "ERROR: PICDPLAYER_ENABLE_PARANOIA must be ON or OFF (got: ${ENABLE_PARANOIA})" >&2
        exit 1
        ;;
esac

echo "==> PiCDPlayer container build"
echo "    repository : ${REPO_ROOT}"
echo "    image      : ${IMAGE}"
echo "    build dir  : ${REPO_ROOT}/${BUILD_DIR}"
echo "    stage dir  : ${STAGE_DIR}"
echo "    package dir: ${PACKAGE_DIR}"
echo "    paranoia   : ${ENABLE_PARANOIA}"
echo "    build type : ${BUILD_TYPE} (application only; WPE SDK unchanged)"
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

echo "==> Preparing cached WPE runtime"
bash "${SCRIPT_DIR}/build-wpe-runtime.sh"
run_build() {
    docker run --rm -v "${REPO_ROOT}:/src" \
        -v "${REPO_ROOT}/build-wpe-runtime/2.54.0:/opt/picdplayer-wpe/2.54.0:ro" \
        -e PKG_CONFIG_PATH=/opt/picdplayer-wpe/2.54.0/lib/pkgconfig \
        -e LD_LIBRARY_PATH=/opt/picdplayer-wpe/2.54.0/lib \
        -w /src "${IMAGE}" "$@"
}
echo "==> Configuring CMake"

run_build \
    cmake -S . -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DENABLE_PARANOIA="${ENABLE_PARANOIA}" \
        -DENABLE_METADATA=ON \
        -DENABLE_API=ON \
        -DINSTALL_SYSTEMD_UNIT=ON \
        -DINSTALL_SYSTEMD_KIOSK_UNIT=ON \
        -DENABLE_WPE_KIOSK=ON \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DPICDPLAYER_SERVICE_USER=picdplayer

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

echo "==> Building"

run_build \
    sh -c "cmake --build '${BUILD_DIR}' -j\$(nproc)"

# ---------------------------------------------------------------------------
# Stage install
# ---------------------------------------------------------------------------

echo "==> Preparing staging directory"

rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}"

echo "==> Installing into staging directory"

run_build \
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

IDENTITY=$(run_build /src/stage/usr/local/bin/cdplayerd --build-info)
if [[ ${IDENTITY%% *} != "build_type=${BUILD_TYPE}" ]]; then
    echo 'ERROR: staged binary build type mismatch' >&2
    exit 1
fi
echo "==> Staged binary ${IDENTITY}"

echo "==> Installed files"
find "${STAGE_DIR}" \( -type f -o -type l \) -print

# ---------------------------------------------------------------------------
# Debian package
# ---------------------------------------------------------------------------

echo "==> Packaging Debian artifact"

run_build \
    cpack --config "/src/${BUILD_DIR}/CPackConfig.cmake" -G DEB -B /src/package-container

PACKAGE_COUNT="$(find "${PACKAGE_DIR}" -maxdepth 1 -type f -name '*.deb' -print | wc -l | tr -d '[:space:]')"
if [[ "${PACKAGE_COUNT}" != 1 ]]; then
    echo "ERROR: expected exactly one Debian package in ${PACKAGE_DIR}, found ${PACKAGE_COUNT}" >&2
    exit 1
fi
PACKAGE="$(find "${PACKAGE_DIR}" -maxdepth 1 -type f -name '*.deb' -print)"
echo "==> Debian package metadata"
docker run --rm \
    -v "${REPO_ROOT}:/src:ro" \
    -w /src \
    "${IMAGE}" \
    dpkg-deb --info "/src/${PACKAGE#${REPO_ROOT}/}"
docker run --rm --network none -v "${REPO_ROOT}:/src:ro" "${IMAGE}" \
    python3 /src/scripts/package_profile.py "/src/${PACKAGE#${REPO_ROOT}/}" "${BUILD_TYPE}"

echo
echo "==> Build completed successfully"
echo "    staged installation: ${STAGE_DIR}"
echo "    Debian package: ${PACKAGE}"
echo
echo "Deploy with:"
echo "    PICDPLAYER_BUILD_TYPE=${BUILD_TYPE} ./scripts/deploy.sh"
