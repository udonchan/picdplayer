#!/usr/bin/env bash
set -euo pipefail

# Development-only WPEPlatform trial. The normal kiosk is restored even if this
# SSH session disappears; the independent transient timer is the last resort.

duration=${1:-120}
if [[ ! $duration =~ ^[0-9]+$ ]] || (( duration < 20 || duration > 300 )); then
  echo 'usage: run-pi.sh [duration-seconds: 20..300]' >&2
  exit 2
fi

rootfs=/var/tmp/picdplayer-wpe-platform-poc/rootfs
url=http://127.0.0.1:8080/player
kiosk=picdplayer-kiosk.service
unit=picdplayer-wpe-trial-$(date +%s)-$$
restore=${unit}-restore

if ! sudo -n true; then
  echo 'run-pi.sh: authenticate with sudo -v first' >&2
  exit 1
fi
if ! systemctl is-active --quiet "$kiosk"; then
  echo "run-pi.sh: $kiosk is not active; leaving it stopped" >&2
  exit 1
fi
if [[ ! -x $rootfs/tmp/picdplayer-wpe-poc ]]; then
  echo "run-pi.sh: missing $rootfs/tmp/picdplayer-wpe-poc" >&2
  exit 1
fi
if [[ ! -e /usr/share/icons/Adwaita/cursors/default ]]; then
  echo 'run-pi.sh: host Adwaita cursor theme is missing' >&2
  exit 1
fi

armed=false
cleanup() {
  if [[ $armed != true ]]; then return; fi
  # Keep the rescue timer armed until the normal kiosk is running again.
  if ! sudo -n systemctl stop "$unit.service" >/dev/null 2>&1 &&
      systemctl is-active --quiet "$unit.service"; then
    echo "run-pi.sh: WPE is still active; $restore.timer remains armed" >&2
    return
  fi
  if sudo -n systemctl start "$kiosk"; then
    sudo -n systemctl stop "$restore.timer" >/dev/null 2>&1 || true
  else
    echo "run-pi.sh: $kiosk start failed; $restore.timer remains armed" >&2
  fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# The timer is a separate systemd unit, so it survives SSH disconnection.
sudo -n systemd-run --quiet --unit="$restore" --on-active="$((duration + 20))s" \
  /bin/sh -c "systemctl kill --signal=SIGKILL $unit.service >/dev/null 2>&1 || true; systemctl start $kiosk"
armed=true

sudo -n systemctl stop "$kiosk"
if ! sudo -n systemd-run --quiet --unit="$unit" \
    --property="RootDirectory=$rootfs" \
    --property=User=picdplayer --property=Group=picdplayer \
    --property='SupplementaryGroups=video render input' \
    --property=RuntimeDirectory=picdplayer-wpe-platform-poc \
    --property=RuntimeDirectoryMode=0700 \
    --property='BindPaths=/dev /proc /run/dbus' \
    --property='BindReadOnlyPaths=/sys /run/udev /usr/share/icons' \
    --property="RuntimeMaxSec=${duration}s" --property=TimeoutStopSec=5s \
    -E HOME=/tmp -E XDG_RUNTIME_DIR=/run/picdplayer-wpe-platform-poc \
    -E XDG_CACHE_HOME=/tmp/wpe-cache -E GSETTINGS_BACKEND=memory \
    /tmp/picdplayer-wpe-poc "$url"; then
  echo 'run-pi.sh: WPE could not start; restoring the normal kiosk' >&2
  exit 1
fi

echo "WPE trial: $unit.service (up to ${duration}s); normal kiosk will be restored."
while systemctl is-active --quiet "$unit.service"; do
  sleep 1
done
result=$(systemctl show --property=Result --value "$unit.service")
echo 'WPE trial ended; restoring the normal kiosk.'
if [[ $result != success && $result != timeout ]]; then
  echo "run-pi.sh: WPE service ended with $result (see journalctl -u $unit.service)" >&2
  exit 1
fi
