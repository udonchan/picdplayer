#!/usr/bin/env bash
set -euo pipefail

# PiCDPlayer deployment script
#
# Deploys the CMake DESTDIR staging tree to the Raspberry Pi.
#
# Default target:
#   picdplayer-pi
#
# Override:
#   PICDPLAYER_TARGET=some-other-host ./scripts/deploy.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

TARGET="${PICDPLAYER_TARGET:-picdplayer-pi}"
LOCAL_STAGE="${REPO_ROOT}/stage"
REMOTE_STAGE="stage"

DAEMON_SERVICE="picdplayer.service"
KIOSK_SERVICE="picdplayer-kiosk.service"

echo "==> PiCDPlayer deploy"
echo "    repository : ${REPO_ROOT}"
echo "    target     : ${TARGET}"
echo "    stage      : ${LOCAL_STAGE}"
echo

# ---------------------------------------------------------------------------
# Preconditions
# ---------------------------------------------------------------------------

if [[ ! -d "${LOCAL_STAGE}" ]]; then
    echo "ERROR: staging directory does not exist:"
    echo "  ${LOCAL_STAGE}"
    echo
    echo "Run the build/install step first."
    exit 1
fi

if [[ ! -x "${LOCAL_STAGE}/usr/local/bin/cdplayerd" ]]; then
    echo "ERROR: staged cdplayerd not found or not executable:"
    echo "  ${LOCAL_STAGE}/usr/local/bin/cdplayerd"
    exit 1
fi

echo "==> Checking SSH connection"
ssh "${TARGET}" true

# ---------------------------------------------------------------------------
# Upload staging tree
# ---------------------------------------------------------------------------

echo "==> Preparing remote staging directory"
ssh "${TARGET}" 'rm -rf ~/stage && mkdir -p ~/stage'

echo "==> Uploading staging tree"
rsync -av \
    "${LOCAL_STAGE}/" \
    "${TARGET}:~/${REMOTE_STAGE}/"

# ---------------------------------------------------------------------------
# Install on target
# ---------------------------------------------------------------------------

echo "==> Installing staged files and restarting PiCDPlayer"

# A single remote TTY lets sudo prompt once when the target requires a password.
# Remote bash exits on an installation/reload/start error. Never delete from /.
ssh -tt "${TARGET}" "
    set -e
    sudo -v
    sudo systemctl stop ${KIOSK_SERVICE} || true
    sudo systemctl stop ${DAEMON_SERVICE} || true
    sudo rsync -av ~/${REMOTE_STAGE}/ /
    sudo systemctl daemon-reload
    sudo systemctl start ${DAEMON_SERVICE}
    sudo systemctl start ${KIOSK_SERVICE}
"

# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------

echo "==> Verifying services"

ssh "${TARGET}" "
    systemctl --no-pager --full status ${DAEMON_SERVICE}
    systemctl --no-pager --full status ${KIOSK_SERVICE}
"

echo
echo "==> Deployment completed successfully"
