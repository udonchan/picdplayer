# WPEPlatform DRM/KMS直接表示PoC（#191、2026-10-05）

## 目的と境界

標準UIを変更せず、Cage/Chromiumを停止した状態でWPE WebKit 2.54の
WPEPlatform DRM backendからHDMIへ直接描画できるかを調べた。正式runtimeへの
切替ではない。標準`picdplayer-kiosk.service`とdaemonは変更していない。

## 環境

| 項目 | 実際の条件 |
|---|---|
| 実機 | Raspberry Pi 3 Model B Rev 1.2 |
| host OS/kernel | Debian 13 (Trixie) arm64 / `6.18.50+rpt-rpi-v8` |
| host Mesa | `libgbm1`, `libegl-mesa0` ともに `26.2.2-1~bpo13+0~rpt1` |
| host WPE/Cog | WPE WebKit `2.48.3-1`, Cog `0.18.4-1+b1` |
| 実験用WPE | 隔離したDebian sid rootfs内のWPE WebKit/WPEPlatform `2.54.0-2` |
| backend | `wpe-platform-drm-2.0`、`/dev/dri/card0`、vc4/GBM/KMS |
| 表示 | HDMI-A-1接続、DRM stateは`1920x1080` |

Trixieのglibcは2.41で、sid版WPEの依存条件を満たさないため、hostのapt sourceや
WPE packageは変更していない。Piの`/var/tmp/picdplayer-wpe-platform-poc/rootfs`へ
`debootstrap --variant=minbase --arch=arm64 sid`でrootfsを作り、その中へ
`gcc make pkgconf libwpewebkit-2.0-dev libgbm-dev`を導入した。実験launcherは
[`poc/wpe-platform`](../../../../poc/wpe-platform/README.md)にある。

## 実施と観測

1. `pkg-config --modversion wpe-platform-drm-2.0 wpe-webkit-2.0`は両方`2.54.0`。
   隔離rootfs内で`make`が成功した。
2. `systemd-run`の`RootDirectory`と`BindPaths`で`/dev`、`/proc`、`/run/dbus`を
   見せ、`/sys`、`/run/udev`を読み取り専用にした。`User=picdplayer`、
   `SupplementaryGroups=video render input`、`RuntimeDirectory`を設定した。
   `GSETTINGS_BACKEND=memory`を指定した。試験前に60〜85秒後のkiosk復旧timerを
   設置し、kioskだけ停止してdaemonは稼働を維持した。
3. 最初の試験はWebView生成でD-Bus初期化待ちになった。`/run/dbus`を見せると
   WebView生成が進んだ。次はWebProcessの`bwrap`が読み取り専用`/proc`へUID mapを
   書けず失敗した。`/proc`を書き込み可能なbindへ変更すると解消した。
4. 修正後、journalに`WPE PoC: loaded http://127.0.0.1:8080/player`を確認した。
   `WPEWebProcess`が起動し、remote inspectorの`http://127.0.0.1:9223/`は
   `PiCDPlayer`と`/player`を対象として列挙した。`/dev/dri/card0`をlauncherが
   開いており、試験中にCage/Chromium processは存在しなかった。DRM stateは
   `1920x1080`。TVを見たユーザーは画面表示を「問題なさそう」と報告し、
   背景上にはcursorが残り、CECリモコンの方向入力では選択枠が動いたと報告した。
5. 短時間試験を終了し、`picdplayer.service`と`picdplayer-kiosk.service`の双方が
   active、Cage/Chromium processが復旧したことを確認した。
6. SSH port forwardでWebKit remote inspectorへ接続し、`Runtime.enable`、
   `DOM.getDocument`、`CSS.enable`、`Console.enable`、`Network.enable`が応答した。
   WebKit Inspector Protocolの`Runtime.evaluate`では、document titleが
   `PiCDPlayer`、ready stateが`complete`、viewportが`1920×1080`、本文に
   『The Slip』・`PLAYING`・`INTEGRITY MONITOR`・`DAEMON · CONNECTED`が
   表示された。`Network.webSocketCreated`で`/api/events`と`/api/navigation`、
   resource timingで`/api/state`、cover、read-historyを確認した。
7. 読み込み後に`systemctl stop`でSIGTERMを送ると約1秒で`Result=success`、
   `ExecMainStatus=0`となった。その後、daemonと通常kioskはともにactiveに復旧した。
8. daemonを止めず、未使用port `127.0.0.1:65500`へ向けた試験では、launcherの
   `load-failed` callbackが`Connection refused`を記録した。PoCを正常停止して
   通常kioskへ復旧した。これはdaemon停止そのものの試験ではなく、UIのHTTP接続が
   できない状態の代用である。最初のlauncherは失敗後の`LOAD_FINISHED`を`loaded`と
   誤解しうるログにしたため、失敗の有無を明示するログへ修正した。
9. `SIGKILL`でlauncherの突然の終了を模擬すると、一時unitは`Result=signal`で
   failedになり、PoC processは残らなかった。通常kioskを再起動するとCage/Chromium
   が戻った。PoC unitに自動復旧設定はなく、正式移行時には別途必要である。

PoCには`Could not create cursor theme for 'default'`とaccessibility busへの接続警告、
WebProcessのremote inspector内部接続警告が残る。inspectorの各protocol domainは
応答したが、開発PCのGUI frontend上で各panelの使い勝手は未確認である。
WebSocket生成と画面の`DAEMON · CONNECTED`表示は確認したが、WebSocketの継続・切断復旧、artwork、
background、Integrity、Custom UI、CSS animation、CECの各操作はTVでの
個別確認がまだ不足する。cursorは今回**消えていない**。Piのdaemon APIやWebKit page loadだけでこれらを成功と
みなさない。実際の内部crashとdaemon service自体を停止した状態は未試験。

## 暫定判断

**B: 直接DRM構成と既存UIの読み込みは成立したが、追加PoCが必要。**
Chrome/CDPを使う既存測定・debug workflowをremote inspectorへどう移すか、
CEC/input/cursor、主要UIの実画面、安定性を確認してから正式移行を判断する。
WPE 2.54を得るための隔離sid rootfsは実験手段であり、製品配布方式ではない。
Buildroot等でのWPE 2.54供給可能性も未決定。性能の比較値は取得していない。
