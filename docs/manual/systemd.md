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
