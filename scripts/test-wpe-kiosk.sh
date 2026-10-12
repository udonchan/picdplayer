#!/bin/bash
set -euo pipefail

duration=${1:-20}
launcher=${2:?Provide a launcher path inside the verified WPE rootfs}
rootfs=/var/tmp/picdplayer-wpe-platform-poc/rootfs
kiosk=picdplayer-kiosk.service
[[ $duration =~ ^[0-9]+$ ]] && (( duration >= 20 && duration <= 60 )) || exit 2
[[ $launcher == /tmp/picdplayer-wpe-primary-*/picdplayer-wpe ]] || exit 2
sudo -n true
systemctl is-active --quiet "$kiosk" || { echo 'Leaving intentionally stopped kiosk unchanged' >&2; exit 1; }
[[ -x $rootfs$launcher ]] || exit 1
[[ -e /usr/share/icons/Adwaita/cursors/default ]] || exit 1
unit=picdplayer-wpe-primary-test-$(date +%s)-$$
restore=${unit}-restore
armed=false
cleanup() {
  [[ $armed == true ]] || return 0
  sudo -n systemctl stop "$unit.service" || return
  if sudo -n systemctl start "$kiosk"; then
    sudo -n systemctl stop "$restore.timer" || true
  fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM
sudo -n systemd-run --quiet --unit="$restore" --on-active="$((duration + 20))s" \
  /bin/sh -c "systemctl stop $unit.service && systemctl start $kiosk"
armed=true
sudo -n systemctl stop "$kiosk"
sudo -n systemd-run --quiet --unit="$unit" \
  --property="RootDirectory=$rootfs" --property=User=picdplayer --property=Group=picdplayer \
  --property='SupplementaryGroups=video render input' \
  --property=RuntimeDirectory=picdplayer-wpe-primary-test --property=RuntimeDirectoryMode=0700 \
  --property='BindPaths=/dev /proc /run/dbus' \
  --property='BindReadOnlyPaths=/sys /run/udev /usr/share/icons' \
  --property="RuntimeMaxSec=${duration}s" --property=TimeoutStopSec=5s \
  -E HOME=/run/picdplayer-wpe-primary-test -E XDG_RUNTIME_DIR=/run/picdplayer-wpe-primary-test \
  -E XDG_CACHE_HOME=/run/picdplayer-wpe-primary-test/cache \
  -E XDG_STATE_HOME=/run/picdplayer-wpe-primary-test/state \
  -E XDG_DATA_HOME=/run/picdplayer-wpe-primary-test/data -E GSETTINGS_BACKEND=memory \
  -E WEBKIT_INSPECTOR_HTTP_SERVER=127.0.0.1:9223 \
  "$launcher" http://127.0.0.1:8080/player
echo "WPE test unit=$unit.service restore=$restore.timer"
while systemctl is-active --quiet "$unit.service"; do sleep 1; done
result=$(systemctl show -P Result "$unit.service")
[[ $result == success || $result == timeout ]] || { echo "WPE test failed: $result" >&2; exit 1; }
