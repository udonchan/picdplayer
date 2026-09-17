# PiCDPlayer

Raspberry PiとUSB光学ドライブで物理Audio CDを再生する、家電型CDプレイヤーです。
C++20のdaemon `cdplayerd`がCD-DAを読み、ALSAからHDMIへ出力します。
TVリモコンのHDMI-CEC操作、ディスク自動認識、任意のmetadata取得、HTTP操作・WebSocket状態配信を実装しています。
metadataがなくても再生できます。TV向けUI、quiet boot、Buildroot imageは今後の対象です。

開発・実機確認環境はRaspberry Pi 3 Model B、Raspberry Pi OS Lite、ASUS USB光学ドライブ、
REGZA TV → HDMI ARC → Marantz NR1200です。動作確認の範囲は[検証状況](docs/verification.md)を参照してください。

## ビルドと起動

Linux、C++20コンパイラ、CMake 3.20以上、ALSAとLinux CEC/CD-ROMの開発ヘッダーが必要です。
Raspberry Pi OSでの基本依存は次のとおりです。Pythonは開発用テストにのみ使用します。

```sh
sudo apt install build-essential cmake libasound2-dev linux-libc-dev python3
cmake -S . -B build-direct
cmake --build build-direct -j1
ctest --test-dir build-direct --output-on-failure
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct
```

Piのメモリ・CPU負荷を抑えるためビルドは `-j1` とします。`build/`、`build-*/` はGit管理対象外です。
daemonはrootで動かさず、実行ユーザーに `video`・`cdrom`・`audio` のデバイスアクセス権限を与えます。
手動起動前には既存の `picdplayer.service` を停止し、デバイスの重複使用を避けてください。
標準入力から操作する場合だけ `--interactive` を追加します。終了はCtrl-CまたはSIGTERMです。
引数なし起動はCEC確認用で、CD再生を開始しません。

## 任意機能

| CMake option（既定OFF） | Raspberry Pi OSの追加開発パッケージ | 用途 |
|---|---|---|
| `ENABLE_PARANOIA` | `pkg-config libcdio-paranoia-dev` | 比較用CD-DA reader |
| `ENABLE_METADATA` | `pkg-config libdiscid-dev libcurl4-openssl-dev nlohmann-json3-dev` | MusicBrainz・Cover Art参照 |
| `ENABLE_API` | `pkg-config libwebsockets-dev nlohmann-json3-dev` | HTTP操作・WebSocket状態配信 |
| `INSTALL_SYSTEMD_UNIT` | systemd運用環境 | service unitのインストール |

metadataのHTTPS通信にはCA証明書と正しいシステム時刻も必要です。
Node.jsやPythonをdaemonのruntimeには要求しません。

```sh
sudo apt install pkg-config libdiscid-dev libcurl4-openssl-dev   nlohmann-json3-dev libwebsockets-dev ca-certificates
cmake -S . -B build-metadata -DENABLE_METADATA=ON -DENABLE_API=ON
cmake --build build-metadata -j1
ctest --test-dir build-metadata --output-on-failure
./build-metadata/cdplayerd --player /dev/sr0 --cdda-reader direct   --metadata musicbrainz --metadata-cache /tmp/picdplayer-cache --api-port 8080
```

APIは `--api-port` 指定時だけ起動します。既定の接続先は `http://127.0.0.1:8080` です。
追加CLIや外部接続については[操作・診断手順](docs/operations.md)を参照してください。

## インストールと常駐運転

```sh
cmake -S . -B build-metadata -DENABLE_METADATA=ON -DENABLE_API=ON   -DINSTALL_SYSTEMD_UNIT=ON -DCMAKE_INSTALL_PREFIX=/usr/local   -DPICDPLAYER_SERVICE_USER=picdplayer
cmake --build build-metadata -j1
# 初回のみ専用ユーザーを作成
sudo useradd --system --user-group --no-create-home   --home-dir /nonexistent --shell /usr/sbin/nologin picdplayer
sudo cmake --install build-metadata
sudo systemctl daemon-reload
sudo systemd-analyze verify picdplayer.service
sudo systemctl start picdplayer.service
journalctl -u picdplayer.service -f
```

動作確認後、`sudo systemctl enable picdplayer.service` で自動起動を有効にします。
metadata/APIを常駐運転で有効にするには `/etc/default/picdplayer` に設定します。

```ini
PICDPLAYER_EXTRA_ARGS="--metadata musicbrainz --metadata-cache /var/cache/picdplayer --api-port 8080"
```

設定後は `sudo systemctl restart picdplayer.service` で反映します。
ユーザー権限、更新手順、停止期限、CEC暫定serviceの扱いは[systemd運用手順](docs/systemd.md)を参照してください。

## ドキュメント

[ドキュメント索引](docs/README.md)から、基本設計・機能設計・詳細設計、運用手順、検証状況を参照できます。
過去の試行や実機ログは `docs/history/` に保存しています。
