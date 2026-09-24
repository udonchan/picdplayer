# systemd常駐運転

## player mode

`--player`は標準入力を監視しない常駐modeで、stdin EOFを終了条件にしない。
停止はSIGINT/SIGTERMで行う。SSH端末から直接起動したprocessはSIGHUPで終了し得るため、
SSH切断後も運転する場合はsystemdから起動する。

端末コマンドを使う開発試験だけ`--interactive`を指定する。

```sh
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct --interactive
```

実行時に指定できるhardware設定は次のとおり。

|設定|既定値または指定例|
|---|---|
|CD device|`--player /dev/sr0`|
|CDDA backend|`--cdda-reader direct`または`paranoia`|
|ALSA PCM|`--audio-device plughw:CARD=vc4hdmi,DEV=0`|
|CEC device|`--cec-device /dev/cec0`|
|CEC診断|`--cec-diagnostics`|
|CEC無効化|`--no-cec`|

CEC deviceが未出現、またはPhysical Addressが`f.f.f.f`なら、main loopを止めずに
250 ms周期で再確認する。ALSAのopen/configureに失敗した場合はprocessが失敗終了し、
serviceの`Restart=on-failure`で再試行する。CD deviceのopen失敗は同じerrorの連続表示を
抑制しながらmedia workerが再確認する。

## Buildとinstall

systemd unitはBuildroot等で不要な依存を増やさないよう、build時のinstall optionを
既定でOFFにしている。Raspberry Pi OS用には次のように明示する。

```sh
cmake -S . -B build-direct \
  -DENABLE_PARANOIA=OFF \
  -DINSTALL_SYSTEMD_UNIT=ON \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DPICDPLAYER_SERVICE_USER=picdplayer
cmake --build build-direct -j1
```

生成物は`build-direct/picdplayer.service`で確認できる。install先は既定で
`/usr/local/bin/cdplayerd`と`/usr/local/lib/systemd/system/picdplayer.service`になる。
unitの配置先は`PICDPLAYER_SYSTEMD_UNIT_DIR`で変更できる。既定はprefix相対の
`lib/systemd/system`で、architecture別のlibrary directoryには依存しない。
`ExecStart`の実行ファイルパスはconfigure時に確定するため、prefixの変更は
`cmake --install --prefix`だけで行わず、`CMAKE_INSTALL_PREFIX`を指定して再configureする。

専用の非loginユーザーを一度だけ作成する。daemonをrootでは実行しない。

```sh
sudo useradd --system --user-group --no-create-home \
  --home-dir /nonexistent --shell /usr/sbin/nologin picdplayer
```

unitの`SupplementaryGroups`に`video cdrom audio`を指定している。Raspberry Pi OS上で
各groupが存在し、`/dev/cec0`、`/dev/sr0`、ALSA deviceへアクセスできることを確認する。

```sh
sudo cmake --install build-direct
sudo systemctl daemon-reload
sudo systemd-analyze verify picdplayer.service
```

## 起動設定

unit内に開発機の既定値がある。変更は`/etc/default/picdplayer`へ必要な項目だけ書く。

```sh
PICDPLAYER_CD_DEVICE=/dev/sr0
PICDPLAYER_CDDA_READER=direct
PICDPLAYER_AUDIO_DEVICE=plughw:CARD=vc4hdmi,DEV=0
PICDPLAYER_CEC_DEVICE=/dev/cec0
PICDPLAYER_EXTRA_ARGS=--cec-diagnostics
```

`PICDPLAYER_EXTRA_ARGS`は空でもよい。複数の追加optionを指定する場合はsystemdの
EnvironmentFile構文に従って値を引用する。
これはshell scriptではないため、変数展開やcommand substitutionは利用しない。
`--interactive`は指定しない。serviceのstdinは`null`なので、指定するとEOFで正常終了する。
設定変更は`sudo systemctl restart picdplayer.service`で反映する。

手動のplayerやCD読み取り診断を実行する前には`sudo systemctl stop picdplayer.service`で
serviceを停止する。同じdrive・ALSA・CECを複数のplayerから同時操作しない。
試験後は`sudo systemctl start picdplayer.service`で常駐運転へ戻す。
暫定の`picdplayer-cec.service`が残っている場合は無効化しておく。

## 段階的な有効化

boot時起動を有効化する前にforegroundと一時service起動を確認する。

```sh
sudo systemctl start picdplayer.service
systemctl status picdplayer.service
journalctl -u picdplayer.service -f
```

確認項目:

1. 空のdriveで`NO_DISC`として常駐する。
2. Audio CD挿入後にTOCを取得し、CECリモコンで再生できる。
3. 取り出しと再挿入を認識する。
4. TV入力切替後もCEC Playback Device登録とARCが維持される。
5. `systemctl stop`でSIGTERMを受け、終了コード0で停止する。
6. 手動stop後に`Restart=on-failure`が再起動しない。

ここまで確認してからboot時起動を有効化する。

```sh
sudo systemctl enable picdplayer.service
```

異常終了時は2秒後に再起動する。hardwareの準備が遅いbootでも再試行を継続できるよう
start rate limitは無効化している。SIGTERM後もkernel内のCD I/Oが戻らない場合、
30秒後にsystemdがSIGKILLを送る。ただしkernel内の割り込み不能なI/O待ちでは、
SIGKILLでも即時終了を保証できない。quiet boot、splash、read-only root filesystemは
この段階には含めない。

## metadata/APIの追加設定

metadata/APIを使う場合はbuild時に`ENABLE_METADATA=ON` / `ENABLE_API=ON`を指定する。
追加依存は[ビルド手順](../manual/build.md)を参照する。`CacheDirectory=picdplayer`が
service user用の`/var/cache/picdplayer`を作成する。

metadata/API有効版をインストールする場合のconfigure例:

```sh
cmake -S . -B build-metadata -DENABLE_METADATA=ON -DENABLE_API=ON \
  -DINSTALL_SYSTEMD_UNIT=ON \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DPICDPLAYER_SERVICE_USER=picdplayer
cmake --build build-metadata -j1
```

`/etc/default/picdplayer` の追加optionは次のように設定する。

```ini
PICDPLAYER_EXTRA_ARGS="--metadata musicbrainz --metadata-cache /var/cache/picdplayer --api-port 8080"
```

設定変更はserviceのrestartで反映する。

既存serviceの更新は停止してからinstallし、daemon-reload後にstartする。

```sh
sudo systemctl stop picdplayer.service
sudo cmake --install build-metadata
sudo systemctl daemon-reload
sudo systemctl start picdplayer.service
```

## Chromium/Cage kiosk

`picdplayer-kiosk.service`はtty1で[Cage](https://github.com/cage-kiosk/cage)を起動し、その中で
Wayland版Chromiumを`http://127.0.0.1:8080/player`へ固定して表示する任意の表示serviceである。
daemonとは別serviceで、kioskが停止・再起動しても再生状態はdaemon側に残る。Cageは一つの
maximized applicationだけを表示するkiosk compositorなので、通常利用時にdesktopやterminalを
表示しない構成にできる。

kioskのorderingは`After=picdplayer.service`とし、`Wants=picdplayer.service`と
`Conflicts=getty@tty1.service`を維持する。localhost UIの起動に外部networkや通常のlogin sessionの
完了を要求しない。`PAMName=login`、TTY、seat/logind設定は従来どおりである。
ユーザーの実機計測では、`After=systemd-user-sessions.service getty@tty1.service`を外すことで
kiosk service開始がuserspace +16.942秒から+9.914秒へ前倒しされた。
これはservice開始時刻であり、Chromium表示やHDMIへの最初の描画時刻ではない。
wrapperと標準Web UIの起動telemetryを追加し、cold bootで記録を確認した。
結果と未解決の起動時間短縮課題は[検証状況](../development/verification.md)を参照する。

このunitは既定でinstallしない。ChromiumとCageのpackageを導入してから、kiosk unitを明示して
configure/installする。Raspberry Pi OS/Debian系でのpackage名は次のとおりである。

```sh
sudo apt install chromium cage fonts-noto-cjk
cmake -S . -B build-metadata -DENABLE_METADATA=ON -DENABLE_API=ON \
  -DINSTALL_SYSTEMD_UNIT=ON -DINSTALL_SYSTEMD_KIOSK_UNIT=ON \
  -DCMAKE_INSTALL_PREFIX=/usr/local -DPICDPLAYER_SERVICE_USER=picdplayer
cmake --build build-metadata -j1
sudo systemctl stop picdplayer-kiosk.service picdplayer.service
sudo cmake --install build-metadata
sudo systemctl daemon-reload
```

日本語のalbum/track名を表示するため、`fonts-noto-cjk`も導入する。Pi OS Liteでは
日本語glyphを持つfontがない場合があり、UTF-8のmetadataでも四角などで表示される。
`fc-list :lang=ja family`で日本語対応fontを確認できる。後からfontを導入した場合は
`sudo systemctl restart picdplayer-kiosk.service`でChromiumを再起動する。daemonの再buildは不要。

unitは`/usr/local/lib/systemd/system/picdplayer-kiosk.service`、wrapperは
`/usr/local/libexec/picdplayer-kiosk`へ入る。CageはDRM/KMS、Wayland runtime、tty1を使うため、
unitは`video render input`を補助groupに加え、`/run/picdplayer-kiosk`をruntime directoryとして
作成する。実機にこれらのgroupまたはDRM deviceがない場合は、distributionの権限設計に合わせて
unitを調整する。daemonの`video cdrom audio`とは目的が異なる。

`/etc/default/picdplayer`ではdaemonがAPIを有効化している必要がある。metadataは任意であり、
次の例では曲名・画像のために併せて有効にする。

```ini
PICDPLAYER_EXTRA_ARGS="--metadata musicbrainz --metadata-cache /var/cache/picdplayer --api-port 8080"
```

画面URLやdistributionごとの実行ファイルパスを変える場合だけ、
`/etc/default/picdplayer-kiosk`を作成する。

```ini
PICDPLAYER_KIOSK_URL=http://127.0.0.1:8080/player
PICDPLAYER_CAGE=/usr/bin/cage
PICDPLAYER_CHROMIUM=/usr/bin/chromium
```

wrapperはChromiumを起動する前に、既定で30秒間`127.0.0.1:8080`のlistenを待つ。daemon側で
`--api-port 8080`を指定し忘れた場合やAPIの起動が遅い場合に、Chromiumの`This site can't be reached`
画面へ進む可能性を減らすためである。これはTCP接続確認であり、HTTPの正常応答や、その直後の
daemon停止まで保証しない。ページを読み込めた後の切断は既存のWebSocket再接続で復旧する。
別portを使う場合はURLと待受先を同時に変える。待受時間は1〜300秒、portは1〜65535。
wrapperはBashとcoreutilsの`timeout`を使用し、DNS/TCP待ちにも期限を適用する。

```ini
PICDPLAYER_KIOSK_URL=http://127.0.0.1:18080/player
PICDPLAYER_API_WAIT_HOST=127.0.0.1
PICDPLAYER_API_WAIT_PORT=18080
PICDPLAYER_API_WAIT_TIMEOUT_SECONDS=30
```

本番bootへ入れる前に、TVを接続した実機で一時起動する。tty1のgettyとは排他なので、SSHまたは
別ttyから実行する。

```sh
sudo systemctl restart picdplayer.service
sudo systemctl start picdplayer-kiosk.service
systemctl status picdplayer-kiosk.service
journalctl -u picdplayer-kiosk.service -b -o cat
```

確認項目:

1. tty1にNow Playingだけが表示され、browserの初回設定やaddress barが見えない。
2. CD挿入、metadata取得、ジャケット、CEC操作に画面が追従する。
3. `sudo systemctl restart picdplayer.service`の後、browserが接続を回復する。
4. `sudo systemctl stop picdplayer-kiosk.service`で表示を停止しても、daemonの再生とCECは続く。
   tty1のlogin promptへ戻すには下記のgetty起動が必要である。

ここまで確認してからboot時起動を有効化する。

```sh
sudo systemctl enable picdplayer.service picdplayer-kiosk.service
```

停止して通常のttyへ戻す場合は次を実行する。

```sh
sudo systemctl disable --now picdplayer-kiosk.service
sudo systemctl start getty@tty1.service
```

Chromiumはsandboxを有効にしたまま起動する。`--no-sandbox`を追加して問題を回避しない。
Now Playingのdocument内ではCSSでcursorを隠す。Cageのerror pageや他のapplicationまで
cursor非表示を保証するものではない。boot途中のkernel/systemd messageを消すquiet boot/splash、
browserの画面遷移や画面内操作は未実装である。これらはkiosk表示が実TVで安定してから別の変更として扱う。

[実機確認状況](../development/verification.md)と[過去のservice試験](../history/systemd-validation.md)も参照する。

## 起動時間の計測（Cage + Chromium + 標準UI）

wrapperはjournalへ`kiosk_boot: event=... wrapper_elapsed_ms=...`を出す。
`/proc/uptime`によるwrapper開始基準の経過時間（10 ms分解能）で、daemonの`+ms`とは原点が異なる。

|event|観測点|
|---|---|
|`kiosk_wrapper_start`|wrapperの初期処理開始|
|`api_wait_start`|既存TCP待機loopの直前|
|`api_ready`|TCP接続成功。HTTP応答・daemonの継続稼働を保証しない|
|`cage_exec`|Cageへのexec直前。CageやChromiumの起動完了ではない|

TCP待機のtimeout・再試行規則は変更していない。PAM、TTY、seat/logind、NetworkManager設定も
変更していない。Chromiumの既存loopback remote debugging設定（9222）も維持する。

標準UIは任意の`POST /api/ui-boot`へJSONを送る。再生commandとは独立しており、
custom UIは送信しなくても従来どおり動作する。共通bootstrap注入やSDKは追加していない。

```json
{"page_id":"m-page-1","event":"first_render","client_ms":123.4}
```

本文は512 bytes以下、上記3フィールドのみ。`page_id`は1〜64文字のASCII英数字・`-`・`_`、
`event`は下表の名前のみ、`client_ms`は0〜86400000の有限数値とする。
`client_ms`はbrowserの`performance.now()`（navigation基準ms）であり、daemon受信時刻ではない。
ページ読み込みごとに新しいランダム識別子を生成し、再接続では同じIDを使う。
IDは照合用で、認証情報ではない。

|event|定義|
|---|---|
|`ui_script_start`|JS先頭付近で取得した実行開始時刻|
|`dom_content_loaded`|登録したDOMContentLoaded listenerの実行時刻|
|`websocket_connected`|最初のWebSocket open callback|
|`first_render`|RESTまたはWebSocketの最初のsnapshot反映後、2回のrequestAnimationFrameを経た時点|
|`ui_ready`|snapshot反映後の上記描画機会を経て、WebSocketが接続中になった時点|

`first_render`はbrowserへ描画機会を与えた近似値。HDMI scanout、実際のfirst pixelや
ジャケット画像読込完了を保証しない。非表示ページではrAFが遅延し得る。
`ui_ready`は読み取り専用Now Playingの準備完了であり、CEC・ディスク・音声の再生準備ではない。
REST失敗時はWebSocket snapshotでも準備条件を満たせる。イベント順序や受信順序は仮定しない。
標準のdefer scriptでDOMContentLoadedを観測する。イベント後に動的読込されたscriptについては
過去の発火時刻を捏造せず、イベントを記録できないことがある。

telemetryのPOSTは本文あり／なしで同じpeer判定を通し、loopback以外は403にする。
既存commandも同じ判定を使う。
正常受信は204、不正入力は400、本文超過は413、流量制限は429（method違いは405）。
ApiServerごとにsteady clockの60秒窓で最大30件を記録し、その窓内で同一page/eventを重複排除する。
窓更新後は同じIDを再受信しても記録可能。抑制自体を毎回ログには出さない。
標準UIは各eventを一度だけ送信し、応答待ち・再試行をしない。送信失敗は描画・再生を止めない。
AsyncLoggerの容量制限や通信失敗で計測点が欠ける場合がある。

ログは`INFO ui_boot: event=... page_id=... client_ms=...`。
daemonログの`+ms`は既存AsyncLoggerのsteady clock基準の受信処理時刻であり、browserの時計とは
直接減算しない。journal時刻はログ出力・取り込みの遅延を含み、厳密な描画計測ではない。

### ユーザーによる実機確認

Macで以下を実行する（Piではコンパイルしない）。deployはサービスを停止・置換・再開する。

```sh
./scripts/build-container.sh
docker run --rm -v "$PWD:/src" -w /src picdplayer-build ctest --test-dir build-container --output-on-failure
./scripts/deploy.sh
ssh picdplayer-pi 'systemctl --no-pager --full status picdplayer.service picdplayer-kiosk.service'
```

`deploy.sh`はPi側でsudoのパスワードが必要ならMacの端末に一度だけ入力を求める。
非対話環境では実行せず、SSHの対話端末からinstallする。

TV表示・CEC再生と両サービスの正常稼働を確認してから、cold bootを測る場合は
`ssh -t picdplayer-pi 'sudo systemctl poweroff'`で安全に停止し、電源断可能な状態になってから
電源を入れ直す。単なる`sudo reboot`の結果はwarm rebootとして区別して記録する。
起動後、Macで取得する。

```sh
ssh picdplayer-pi 'systemd-analyze critical-chain picdplayer-kiosk.service'
ssh picdplayer-pi 'journalctl -b -o short-monotonic --no-pager | grep -E "(picdplayer|kiosk_boot|ui_boot)"'
ssh picdplayer-pi 'systemctl --no-pager --full status picdplayer.service picdplayer-kiosk.service'
```

`PAMName=login`で起動したwrapper/Chromiumのstdoutは、journalではkiosk unitではなく
user session scopeに分類されることがある。そのため`journalctl -u picdplayer-kiosk.service`だけでは
`kiosk_boot`が欠ける。上記はboot全体から識別子で抽出する。
journal上でservice開始→wrapper→TCP ready→Cage exec→UI eventを並べ、`page_id`ごとに読む。
wrapper内の差分、daemon内の受信時刻差分、browser内の`client_ms`差分をそれぞれ確認する。
DOMContentLoaded・WebSocket・描画イベントを固定の順序に並べ替えない。
ページ再読込でIDが変わること、daemon再起動後に画面が再接続すること、custom UI未送信でも
動作することを確認する。CDなし／あり、TV入力・ネットワーク条件を揃えて複数回計測する。

ordering変更時のユーザー実測はservice開始がuserspace +16.942秒→+9.914秒。
2026-09-25の別のcold bootではkiosk開始がuserspace +10.958秒、daemonが受信した`ui_ready`は
kernel起動後+47.411秒だった。両サービスの起動とTV表示は確認済み。
この2回の差からboot時間改善量を推定しない。HDMI first pixelは未計測で、
起動時間の短縮は[未解決課題](../development/verification.md#起動時間短縮の未解決課題)として残す。
