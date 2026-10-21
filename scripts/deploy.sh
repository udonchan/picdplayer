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

if [[ "${KIOSK_STATE_AFTER}" == active ]]; then
    # Type=simple can still be waiting for the API after try-restart returns.
    ssh "${TARGET}" 'bash -s --' <<'VERIFY'
set -eu
budget=$(timeout --kill-after=1s 5s /usr/local/libexec/picdplayer-kiosk --startup-wait-budget-seconds) || {
    echo 'ERROR: kiosk startup budget query failed' >&2; exit 1;
}
if ! [[ $budget =~ ^[1-9][0-9]{0,5}$ ]]; then
    echo 'ERROR: invalid kiosk startup budget' >&2; exit 1
fi
echo "startup-wait-budget: ${budget}s (grace 5s)"
read_clock() {
    local uptime unused
    read -r uptime unused < /proc/uptime
    [[ $uptime =~ ^[0-9]+\.[0-9]{2}$ ]] || return 1
    now_cs=$((10#${uptime/./}))
}
read_clock
deadline_cs=$((now_cs + (budget + 5) * 100))
remaining_time() {
    read_clock
    if (( now_cs >= deadline_cs )); then
        echo 'ERROR: kiosk runtime startup deadline exceeded' >&2; exit 1
    fi
    local left_cs=$((deadline_cs - now_cs))
    printf -v remaining '%d.%02ds' "$((left_cs / 100))" "$((left_cs % 100))"
}
while true; do
    remaining_time
    state=$(timeout --signal=KILL "$remaining" systemctl show -p ActiveState --value picdplayer-kiosk.service) || {
        echo 'ERROR: kiosk service observation failed' >&2; exit 1;
    }
    [[ $state == active ]] || { echo 'ERROR: kiosk service is no longer active' >&2; exit 1; }
    remaining_time
    pid=$(timeout --signal=KILL "$remaining" systemctl show -p MainPID --value picdplayer-kiosk.service) || {
        echo 'ERROR: kiosk PID observation failed' >&2; exit 1;
    }
    [[ $pid =~ ^[1-9][0-9]*$ ]] || { echo 'ERROR: kiosk has no MainPID' >&2; exit 1; }
    remaining_time
    actual=$(timeout --signal=KILL "$remaining" ps -p "$pid" -o comm=) || {
        echo 'ERROR: kiosk process observation failed' >&2; exit 1;
    }
    remaining_time
    if [[ $actual == cage ]]; then
        echo "kiosk_runtime_verified: process=$actual pid=$pid"
        exit 0
    fi
    if [[ $actual != bash ]]; then
        echo "ERROR: kiosk runtime mismatch: expected=cage actual=$actual" >&2; exit 1
    fi
    remaining_time
    timeout --signal=KILL "$remaining" sleep 1 || {
        echo 'ERROR: kiosk runtime startup deadline exceeded' >&2; exit 1;
    }
done
VERIFY
else
    echo 'kiosk_runtime_verified: inactive (not started for verification)'
fi

echo
echo "==> Deployment completed successfully"
