#!/usr/bin/env bash
set -euo pipefail

# Exercise the generated Debian package in a disposable instance of the same
# Debian/aarch64 image used for the supported build.  This never contacts a Pi
# and never writes outside the disposable container.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE="${PICDPLAYER_BUILD_IMAGE:-picdplayer-build}"
PACKAGE_DIR="${REPO_ROOT}/package-container"

if ! docker image inspect "${IMAGE}" >/dev/null 2>&1; then
    echo "ERROR: Docker image '${IMAGE}' does not exist." >&2
    exit 1
fi

if [[ ! -d "${PACKAGE_DIR}" ]]; then
    echo "ERROR: package directory does not exist: ${PACKAGE_DIR}" >&2
    exit 1
fi

PACKAGE_COUNT="$(find "${PACKAGE_DIR}" -maxdepth 1 -type f -name '*.deb' -print | wc -l | tr -d '[:space:]')"
if [[ "${PACKAGE_COUNT}" != 1 ]]; then
    echo "ERROR: expected exactly one Debian package in ${PACKAGE_DIR}, found ${PACKAGE_COUNT}" >&2
    exit 1
fi
PACKAGE="$(find "${PACKAGE_DIR}" -maxdepth 1 -type f -name '*.deb' -print)"

echo "==> Testing Debian package lifecycle in a disposable container"

docker run --rm \
    -v "${PACKAGE}:/package/picdplayer.deb:ro" \
    "${IMAGE}" \
    sh -eu -c '
        work="$(mktemp -d)"
        trap "rm -rf \"${work}\"" EXIT

        # This old package owns a file that the current package does not.  It
        # models the obsolete payload left by a real package version upgrade.
        mkdir -p "${work}/legacy/DEBIAN" \
                 "${work}/legacy/usr/local/share/picdplayer"
        cat > "${work}/legacy/DEBIAN/control" <<"EOF"
Package: picdplayer
Version: 0.0.1
Architecture: arm64
Maintainer: PiCDPlayer test
Description: legacy package lifecycle fixture
EOF
        printf legacy > "${work}/legacy/usr/local/share/picdplayer/obsolete-owned-file"
        dpkg-deb --build "${work}/legacy" "${work}/legacy.deb" >/dev/null
        dpkg -i "${work}/legacy.deb" >/dev/null

        # A package upgrade must retain files that have never been package-owned.
        printf unrelated > /usr/local/share/picdplayer/unrelated-test-file

        dpkg -i /package/picdplayer.deb >/dev/null
        test "$(dpkg-query -W -f="\${Status}" picdplayer)" = "install ok installed"
        test ! -e /usr/local/share/picdplayer/obsolete-owned-file
        test -f /usr/local/share/picdplayer/unrelated-test-file
        test -x /usr/local/bin/cdplayerd
        test -x /usr/local/libexec/picdplayer-kiosk

        # The same artifact is also a supported re-install path.
        dpkg -i /package/picdplayer.deb >/dev/null
        test "$(dpkg-query -W -f="\${Status}" picdplayer)" = "install ok installed"

        dpkg-query -L picdplayer | while IFS= read -r path; do
            if test -f "${path}" || test -L "${path}"; then
                printf "%s\\n" "${path}"
            fi
        done > "${work}/owned-files"
        dpkg --purge picdplayer >/dev/null

        while IFS= read -r path; do
            if test -e "${path}" || test -L "${path}"; then
                echo "ERROR: package-owned file remains after purge: ${path}" >&2
                exit 1
            fi
        done < "${work}/owned-files"
        test -f /usr/local/share/picdplayer/unrelated-test-file
    '

echo "==> Package lifecycle test completed successfully"
