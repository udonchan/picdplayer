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
追加依存は[ビルド手順](build.md)を参照する。`CacheDirectory=picdplayer`が
service user用の`/var/cache/picdplayer`を作成する。

metadata/API有効版をインストールする場合のconfigure例:

```sh
cmake -S . -B build-metadata -DENABLE_METADATA=ON -DENABLE_API=ON \
  -DINSTALL_SYSTEMD_UNIT=ON -DCMAKE_INSTALL_PREFIX=/usr/local \
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

[実機確認状況](verification.md)と[過去のservice試験](history/systemd-validation.md)も参照する。
