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

echo "==> Checking SSH connection"
ssh "${TARGET}" true

# An intentionally stopped kiosk must stay stopped after deployment.  dpkg's
# postinst uses try-restart, so record the state before replacing files.
DAEMON_STATE_BEFORE="$(ssh "${TARGET}" 'systemctl show -p ActiveState --value picdplayer.service')"
KIOSK_STATE_BEFORE="$(ssh "${TARGET}" 'systemctl show -p ActiveState --value picdplayer-kiosk.service')"
echo "    daemon    : ${DAEMON_STATE_BEFORE}"
echo "    kiosk     : ${KIOSK_STATE_BEFORE}"

# ---------------------------------------------------------------------------
# Upload package
# ---------------------------------------------------------------------------

echo "==> Preparing remote package directory"
ssh "${TARGET}" "rm -rf ~/${REMOTE_PACKAGE_DIR} && mkdir -p ~/${REMOTE_PACKAGE_DIR}"

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
ssh -tt "${TARGET}" "
    set -e
    sudo /usr/bin/dpkg -i -- ~/${REMOTE_PACKAGE_DIR}/${REMOTE_PACKAGE_NAME}
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
ssh "${TARGET}" 'dpkg-query -W picdplayer && test -x /usr/local/bin/cdplayerd && test -x /usr/local/libexec/picdplayer-kiosk'

echo
echo "==> Deployment completed successfully"
