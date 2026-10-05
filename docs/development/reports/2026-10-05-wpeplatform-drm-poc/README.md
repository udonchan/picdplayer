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
10. 別port `18080`で無効drive・null音声・CECなしの試験daemonを起動し、
    [`custom-ui-fixture`](../../../../poc/wpe-platform/custom-ui-fixture/manifest.json)を
    `--custom-ui`で配信した。通常daemonは変更していない。WPE inspectorのDOMでは
    `Custom UI on WPE`、`Player: NO_DISC`、`Events: connected`を確認した。
    `Network.webSocketCreated`で同originの`/api/events`も確認した。これは代表的な
    HTML/CSS/JS/fetch/WebSocketの確認であり、任意のCustom UI互換保証ではない。

## 軽い資源測定

同じPiで『The Slip』挿入済みの`STOPPED`画面を表示し、
[`measure-unit.py`](../../../../poc/wpe-platform/measure-unit.py)で各runtimeの
MainPID配下process treeを10秒サンプルした。PAMによりCage/Chromium processが
service cgroupからuser sessionへ移るため、cgroupのprocess数だけでは測らない。

| runtime | process数 | PSS開始→終了 | CPU（1 core = 100%） |
|---|---:|---:|---:|
| Cage + Chromium | 12→12 | 487745→487821 KiB | 0.78% / 10.276秒 |
| WPEPlatform DRM（隔離sid rootfs） | 5→5 | 241410→241096 KiB | 0.30% / 10.145秒 |

両方とも短時間・逐次の1回測定で、browser profile、cache、rootfs、描画状態、起動からの
経過時間が揃っていない。これを製品版の性能優位や安定時消費量の保証とはしない。
測定中のPi温度は概ね61〜64°C、`get_throttled=0x70000`で、現在のthrottle bitは0、
過去のthrottle履歴bitは立っていた。

PoCには`Could not create cursor theme for 'default'`とaccessibility busへの接続警告、
WebProcessのremote inspector内部接続警告が残る。inspectorの各protocol domainは
応答したが、開発PCのGUI frontend上で各panelの使い勝手は未確認である。
WebSocket生成と画面の`DAEMON · CONNECTED`表示は確認したが、WebSocketの継続・切断復旧、artwork、
background、Integrity詳細、Custom UIのTV目視、CSS animation、CECの各操作は
TVでの
個別確認がまだ不足する。初回のthemeなし試験ではcursorは**消えていない**。Piのdaemon APIやWebKit page loadだけでこれらを成功と
みなさない。実際の内部crashとdaemon service自体を停止した状態は未試験。

### Cursorの追加切り分け

通常kioskとWPE直接DRMを短時間ずつ表示し、`/sys/kernel/debug/dri/0/state`を
比較した。どちらも表示中のframebufferは`plane-3`の1枚だけで、他のplaneの
`fb=0`だった。したがって、見えたcursorを**独立したDRM cursor planeの残像**と
断定する根拠はない。framebuffer内の合成や他の表示経路はこの測定だけでは区別できない。
試験後は通常kioskへ戻し、両serviceがactiveであることを確認した。

`/proc/bus/input/devices`では`vc4-hdmi`の`event0`がキーと相対ポインターの双方を
提供する。udevは同じdeviceへ`ID_INPUT_KEY=1`と`ID_INPUT_POINTINGSTICK=1`を
付けている。WPEのDRM backendもlibinputのkeyboard/pointer capabilityをそれぞれ
受け取る。そのためdevice全体を無効化する案はCEC入力との関係を検証せずに採用しない。
実験用rootfsにはcursor themeのファイルがなく、上記のtheme警告と整合するが、
この不足が画面上のcursorの直接原因かは未確認である。cursorの見た目・移動・
WebKitの`cursor: none`との関係は追加試験で確認した。
標準Playerの[`player.css`](../../../../ui/default/player.css)は既に`body { cursor: none; }`を
指定している。[WPE 2.54のDRM cursor実装](https://github.com/WebKit/WebKit/blob/webkitglib/2.54/Source/WebKit/WPEPlatform/wpe/drm/WPEDRMCursor.cpp)では、
themeが作成できないと`setFromName()`が`"none"`の判定より前に戻る。
したがって実験用rootfsにthemeを与えずに同APIだけを追加しても、cursor非表示の
検証にはならない。

追加試験では復旧timerを先に設置し、通常kioskだけ停止して、hostの`/usr/share/icons`を
隔離rootfsへ`BindReadOnlyPaths`で一時的に見せた。同じlauncher、同じPlayer CSSで
WPEのcursor theme警告が消え、ユーザーはTV上で**cursorが見えず、CEC方向キーでは
選択枠が動く**ことを確認した。DRM stateは引き続き`plane-3`のframebufferのみだった。
試験後にPoCを正常停止し、daemonと通常Cage/Chromium kioskの稼働を確認した。
これはWPE PoCの一条件での成功であり、host OSのcursor設定変更でも、現行Chromium
kioskの#28解決でもない。themeの有無以外の変動要因を統制した反復試験は未実施。

## 暫定判断

**B: 直接DRM構成と既存UIの読み込みは成立したが、追加PoCが必要。**
Chrome/CDPを使う既存測定・debug workflowをremote inspectorへどう移すか、
cursor制御、主要UIの実画面、安定性を確認してから正式移行を判断する。
WPE 2.54を得るための隔離sid rootfsは実験手段であり、製品配布方式ではない。
Buildroot等でのWPE 2.54供給可能性も未決定。上記の短時間測定だけで採否を決めない。
