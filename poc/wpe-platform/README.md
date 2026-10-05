# WPEPlatform DRM/KMS PoC（#191）

これは標準 kiosk の代替候補を調べるための**独立した実験用 launcher**である。
`picdplayer-kiosk.service`、Cage、Chromium、daemon、標準/Custom UIを置換しない。
正式な運用手順や配布物には含めない。

## 前提

- WPE WebKit **2.54以降**で`wpe-platform-drm-2.0`と`wpe-webkit-2.0`を提供するbuild。
- DRM/KMS、GBM、EGL、libinputを使えるRaspberry Piのruntime。
- `/player`を配信する既存daemon。Web UI側へのPoC専用変更は加えない。

PiのDebian 13/Trixieが提供するWPE 2.48.3/Cog 0.18.4はこのlauncherのbuild先ではない。
sidのWPE 2.54 binaryはTrixieより新しいglibcを要求するため、ホストOSのapt sourceを
sidへ変更したり、sid packageをホストへ直接installしたりしない。

2026-10-05のPi 3実験では、Piに`debootstrap`を入れ、以下の隔離rootfsを用いた。
この約1.5 GBのrootfsは実験用で、標準serviceや製品packageに含まれない。

```sh
sudo debootstrap --variant=minbase --arch=arm64 sid \
  /var/tmp/picdplayer-wpe-platform-poc/rootfs https://deb.debian.org/debian
sudo chroot /var/tmp/picdplayer-wpe-platform-poc/rootfs \
  apt-get install --no-install-recommends gcc make pkgconf libwpewebkit-2.0-dev libgbm-dev
```

## Build

隔離環境に上記のdevelopment packageとC compilerがある場合、ここで`make`を実行する。
`Makefile`はWPEPlatformのDRM moduleを`pkg-config`で確認し、単一のCファイルをbuildする。
launcherはDRM displayへ明示的に接続し、URLを読み込む。SIGINT/SIGTERMはGLib main loopを
終了させる。失敗時は標準errorへ理由を出す。

```sh
pkg-config --modversion wpe-platform-drm-2.0 wpe-webkit-2.0
make
```

## 実機試験の境界

DRM masterは通常1つのclientしか持てない。Cage + Chromiumが稼働中にこのlauncherを
同じHDMIへ起動しない。まず復旧方法と時間制限を用意してから、**kioskだけ**を短時間停止する。
daemonを停止・再起動する必要はない。試験後はlauncherが終了したことをprocess/DRM利用で確認し、
既存`picdplayer-kiosk.service`を再開する。正常終了しない場合は時間制限後に試験processを
強制停止してから復旧する。#190のCage + Cog試験ではSIGTERMだけで終了しなかった。

実機結果、起動コマンド、isolated runtimeの作り方、OS/Mesa/WPE version、seat/device権限、
DevTools、CEC、画面・CPU等の計測結果は#191と検証記録へ追記する。条件の異なる測定値を
Chromiumとの性能差とみなさない。

このrootfsでの起動には`/dev`、書き込み可能な`/proc`、`/run/dbus`をbindし、
`/sys`と`/run/udev`を読み取り専用で見せる必要があった。`/run/dbus`がないと
WebView生成時のD-Bus初期化で待ち、`/proc`を読み取り専用にするとWebProcessの
`bwrap`がUID mapを作れず失敗した。`GSETTINGS_BACKEND=memory`も設定した。
`User=picdplayer`、`SupplementaryGroups=video render input`で実行した。
**実行前に自動復旧timerを設置し**、60秒程度の`RuntimeMaxSec`を使う。
標準kioskを停止した後だけ起動し、試験後はPoC processを終了させて標準kioskを起動する。
この条件は実験結果であり、製品用systemd unitの推奨構成ではない。

参照： [WPEPlatform browser tutorial](https://wpewebkit.org/reference/2.54.0/wpe-platform-2.0/tutorial-browser.html)、
[build modules](https://wpewebkit.org/reference/2.54.0/wpe-platform-2.0/compiling.html)。
