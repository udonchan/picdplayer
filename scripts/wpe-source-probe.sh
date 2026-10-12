#!/bin/bash
set -euo pipefail

# Run only inside the dedicated Trixie/arm64 probe container, with /work a volume.
[[ $(dpkg --print-architecture) == arm64 ]] || { echo 'arm64 container required' >&2; exit 2; }
action=${1:-configure}
case "$action" in configure|build|stage|verify) ;; *) echo 'Use configure, build, stage, or verify' >&2; exit 2 ;; esac
mkdir -p /work/logs
case "$action" in
  configure)
    dpkg-query -W -f='${Package}\t${Version}\t${Architecture}\n' > /work/logs/packages.tsv
    cmake --version > /work/logs/toolchain.txt
    c++ --version >> /work/logs/toolchain.txt
    ldd --version >> /work/logs/toolchain.txt
    cmake -S /opt/wpe-source -B /work/build -G Ninja \
      -DPORT=WPE -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/opt/picdplayer-wpe/2.54.0 \
      -DENABLE_WPE_PLATFORM=ON -DENABLE_WPE_PLATFORM_DRM=ON \
      -DENABLE_WPE_LEGACY_API=OFF -DENABLE_WPE_PLATFORM_WAYLAND=OFF \
      -DENABLE_WPE_PLATFORM_HEADLESS=OFF -DENABLE_WPE_QT_API=OFF \
      -DENABLE_BUBBLEWRAP_SANDBOX=ON \
      -DENABLE_DOCUMENTATION=OFF -DENABLE_INTROSPECTION=OFF \
      -DENABLE_VIDEO=OFF -DENABLE_WEB_AUDIO=OFF -DENABLE_WEB_CODECS=OFF \
      -DUSE_GSTREAMER=OFF \
      -DENABLE_SPEECH_SYNTHESIS=OFF -DENABLE_SPELLCHECK=OFF \
      -DUSE_LIBHYPHEN=OFF -DUSE_LIBBACKTRACE=OFF \
      -DENABLE_WEBDRIVER=OFF 2>&1 | tee /work/logs/configure.log
    ;;
  build)
    cmake --build /work/build --parallel 2 2>&1 | tee /work/logs/build.log
    ;;
  stage)
    DESTDIR=/work/stage cmake --install /work/build 2>&1 | tee /work/logs/install.log
    ;;
  verify)
    # Expose the configured prefix only inside this disposable container.
    ln -s /work/stage/opt/picdplayer-wpe /opt/picdplayer-wpe
    export PKG_CONFIG_PATH=/opt/picdplayer-wpe/2.54.0/lib/pkgconfig
    export LD_LIBRARY_PATH=/opt/picdplayer-wpe/2.54.0/lib
    {
      pkg-config --modversion wpe-webkit-2.0 wpe-platform-drm-2.0
      cc -std=c11 -Wall -Wextra -Wpedantic -Werror /src/src/wpe_kiosk.c \
        -o /work/picdplayer-wpe $(pkg-config --cflags --libs wpe-webkit-2.0 wpe-platform-drm-2.0)
      [[ $(/work/picdplayer-wpe --version) == 2.54.0 ]]
      /work/picdplayer-wpe --version
      for target in /work/picdplayer-wpe /opt/picdplayer-wpe/2.54.0/libexec/wpe-webkit-2.0/*Process; do
        printf '\nDependencies: %s\n' "$target"
        dependencies=$(ldd -r "$target" 2>&1)
        printf '%s\n' "$dependencies"
        [[ $dependencies != *'not found'* && $dependencies != *'undefined symbol:'* ]]
      done
    } 2>&1 | tee /work/logs/launcher.log
    ;;
esac
