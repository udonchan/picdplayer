#!/usr/bin/env bash
set -euo pipefail

# PiCDPlayer deployment script
#
# Deploys the CMake/CPack Debian package to the Raspberry Pi.
#
# Default target:
#   picdplayer-pi
#
# Override:
#   PICDPLAYER_TARGET=some-other-host ./scripts/deploy.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

TARGET="${PICDPLAYER_TARGET:-picdplayer-pi}"
LOCAL_PACKAGE_DIR="${REPO_ROOT}/package-container"
REMOTE_PACKAGE_DIR="picdplayer-package"
REMOTE_PACKAGE_NAME="picdplayer.deb"
EXPECTED_RUNTIME="${PICDPLAYER_EXPECT_KIOSK_RUNTIME:-wpe}"
BUILD_TYPE="${PICDPLAYER_BUILD_TYPE:-Release}"
case "${BUILD_TYPE}" in
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *) echo 'ERROR: invalid PICDPLAYER_BUILD_TYPE' >&2; exit 2 ;;
esac
case "${EXPECTED_RUNTIME}" in
    wpe|chromium) ;;
    *) echo 'ERROR: PICDPLAYER_EXPECT_KIOSK_RUNTIME must be wpe or chromium' >&2; exit 2 ;;
esac

echo "==> PiCDPlayer deploy"
echo "    repository : ${REPO_ROOT}"
echo "    target     : ${TARGET}"
echo "    package dir: ${LOCAL_PACKAGE_DIR}"
echo

# ---------------------------------------------------------------------------
# Preconditions
# ---------------------------------------------------------------------------

if [[ ! -d "${LOCAL_PACKAGE_DIR}" ]]; then
    echo "ERROR: package directory does not exist:"
    echo "  ${LOCAL_PACKAGE_DIR}"
    echo
    echo "Run the build/install step first."
    exit 1
fi

PACKAGE_COUNT="$(find "${LOCAL_PACKAGE_DIR}" -maxdepth 1 -type f -name '*.deb' -print | wc -l | tr -d '[:space:]')"
if [[ "${PACKAGE_COUNT}" != 1 ]]; then
    echo "ERROR: expected exactly one Debian package in ${LOCAL_PACKAGE_DIR}, found ${PACKAGE_COUNT}" >&2
    exit 1
fi
LOCAL_PACKAGE="$(find "${LOCAL_PACKAGE_DIR}" -maxdepth 1 -type f -name '*.deb' -print)"

# Read the CMake-installed identity from the actual archive, never a sidecar or
# the current CMake cache (which may describe a newer, failed build).
PACKAGE_PROFILE=$(docker run --rm --network none -v "${REPO_ROOT}:/src:ro" \
    "${PICDPLAYER_BUILD_IMAGE:-picdplayer-build}" python3 /src/scripts/package_profile.py \
    "/src/${LOCAL_PACKAGE#${REPO_ROOT}/}" "${BUILD_TYPE}") || {
    echo 'ERROR: package has no valid build profile; rebuild it before deploying' >&2
    exit 1
}
if [[ ${PACKAGE_PROFILE%%$'\n'*} != "build_type=${BUILD_TYPE}" ]]; then
    echo 'ERROR: package build type differs from PICDPLAYER_BUILD_TYPE; rebuild/select explicitly' >&2
    exit 1
fi
echo "    build type : ${BUILD_TYPE} (verified from package)"

echo "==> Checking SSH connection"
ssh "${TARGET}" true
echo "==> Checking passwordless package install permission"
if ! ssh "${TARGET}" "sudo -n -l /usr/bin/dpkg -i -- ~/${REMOTE_PACKAGE_DIR}/${REMOTE_PACKAGE_NAME} >/dev/null"; then
    echo "ERROR: configure the narrow dpkg sudoers rule described in docs/manual/mac-docker-development.md" >&2
    exit 1
fi

# An intentionally stopped kiosk must stay stopped after deployment.  dpkg's
# postinst uses try-restart, so record the state before replacing files.
DAEMON_STATE_BEFORE="$(ssh "${TARGET}" 'systemctl show -p ActiveState --value picdplayer.service')"
KIOSK_STATE_BEFORE="$(ssh "${TARGET}" 'systemctl show -p ActiveState --value picdplayer-kiosk.service')"
echo "    daemon    : ${DAEMON_STATE_BEFORE}"
echo "    kiosk     : ${KIOSK_STATE_BEFORE}"
for state in "${DAEMON_STATE_BEFORE}" "${KIOSK_STATE_BEFORE}"; do
    case "${state}" in
        active|inactive) ;;
        *)
            echo "ERROR: service state must be stable (active or inactive) before deployment: ${state}" >&2
            exit 1
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Upload package
# ---------------------------------------------------------------------------

echo "==> Preparing remote package directory"
ssh "${TARGET}" "mkdir -p ~/${REMOTE_PACKAGE_DIR}"

echo "==> Uploading Debian package"
rsync -av \
    "${LOCAL_PACKAGE}" \
    "${TARGET}:~/${REMOTE_PACKAGE_DIR}/${REMOTE_PACKAGE_NAME}"

# ---------------------------------------------------------------------------
# Install on target
# ---------------------------------------------------------------------------

echo "==> Installing Debian package"

# The package postinst reloads systemd and restarts only services which were
# active before installation.  A narrow NOPASSWD sudoers entry may permit this
# exact dpkg invocation for the trusted development account.
ssh "${TARGET}" "
    set -e
    sudo -n /usr/bin/dpkg -i -- ~/${REMOTE_PACKAGE_DIR}/${REMOTE_PACKAGE_NAME}
"

# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------

echo "==> Verifying services"

DAEMON_STATE_AFTER="$(ssh "${TARGET}" 'systemctl show -p ActiveState --value picdplayer.service')"
KIOSK_STATE_AFTER="$(ssh "${TARGET}" 'systemctl show -p ActiveState --value picdplayer-kiosk.service')"
echo "    daemon    : ${DAEMON_STATE_BEFORE} -> ${DAEMON_STATE_AFTER}"
echo "    kiosk     : ${KIOSK_STATE_BEFORE} -> ${KIOSK_STATE_AFTER}"
if [[ "${DAEMON_STATE_AFTER}" != "${DAEMON_STATE_BEFORE}" ||
      "${KIOSK_STATE_AFTER}" != "${KIOSK_STATE_BEFORE}" ]]; then
    echo "ERROR: PiCDPlayer service state changed during deployment" >&2
    exit 1
fi
ssh "${TARGET}" "test \"\$(dpkg-query -W -f='\${Status}' picdplayer)\" = 'install ok installed' && test -x /usr/local/bin/cdplayerd && test -x /usr/local/libexec/picdplayer-kiosk"
ssh "${TARGET}" "test \"\$(cat /usr/local/share/picdplayer/build-profile)\" = '${PACKAGE_PROFILE}'"
DIAGNOSTIC_PROFILE=Release
if [[ ${BUILD_TYPE} == Debug ]]; then DIAGNOSTIC_PROFILE=Debug; fi
ssh "${TARGET}" "test \"\$(/usr/local/bin/cdplayerd --build-info)\" = 'build_type=${BUILD_TYPE} profile=${DIAGNOSTIC_PROFILE}'"
ssh "${TARGET}" "test \"\$(env LD_LIBRARY_PATH=/opt/picdplayer-wpe/2.54.0/lib PICDPLAYER_INSPECTOR=off /usr/local/libexec/picdplayer-wpe --diagnostics)\" = 'wpe_profile: build_type=${BUILD_TYPE} profile=${DIAGNOSTIC_PROFILE} inspector=off'"
ssh "${TARGET}" 'test "$(env LD_LIBRARY_PATH=/opt/picdplayer-wpe/2.54.0/lib /usr/local/libexec/picdplayer-wpe --version)" = 2.54.0'
if [[ "${KIOSK_STATE_AFTER}" == active ]]; then
    # Type=simple can still be in the API-wait wrapper after try-restart returns.
    ssh "${TARGET}" "bash -s -- '${EXPECTED_RUNTIME}'" <<'VERIFY'
set -eu
case "$1" in wpe) expected=picdplayer-wpe ;; chromium) expected=cage ;; esac
for ((attempt=0; attempt<35; attempt++)); do
    pid=$(systemctl show -p MainPID --value picdplayer-kiosk.service)
    actual=$(ps -p "${pid:-0}" -o comm= 2>/dev/null) || actual=none
    if [[ $actual == "$expected" ]]; then
        echo "kiosk_runtime_verified: expected=$1 process=$actual pid=$pid"
        exit 0
    fi
    if [[ $actual == cage || $actual == picdplayer-wpe ]]; then break; fi
    sleep 1
done
echo "ERROR: kiosk runtime mismatch: expected=$expected actual=$actual" >&2
exit 1
VERIFY
else
    echo 'kiosk_runtime_verified: inactive (not started for verification)'
fi

echo
echo "==> Deployment completed successfully"
