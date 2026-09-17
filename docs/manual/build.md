# ビルド手順

## 必要環境

Linux、C++20コンパイラ、CMake 3.20以上、ALSAとLinux CEC/CD-ROMの開発ヘッダーが必要。
Raspberry Pi OSでは次のパッケージを使用する。Pythonは開発用試験のみ。

```sh
sudo apt install build-essential cmake libasound2-dev linux-libc-dev python3
cmake -S . -B build-direct
cmake --build build-direct -j1
ctest --test-dir build-direct --output-on-failure
```

Piのメモリ・CPU負荷を抑えるためビルドは常に `-j1` とする。
`build/`・`build-*/` はGit管理対象外。

## 実行権限と基本起動

daemonはrootで動かさず、実行ユーザーがCEC・CD・ALSA deviceへアクセスできるようにする。
開発機では `video`・`cdrom`・`audio` groupを使用する。所属変更後は再ログインする。
常駐用の専用ユーザーと権限は[systemd手順](../manual/systemd.md)で設定する。

手動起動前に既存serviceを停止し、同じdeviceを複数のplayerから操作しない。

```sh
sudo systemctl stop picdplayer.service
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct
```

serviceを未導入なら停止操作は不要。終了はCtrl-C/SIGTERM。
標準入力の操作には `--interactive` を追加する。引数なし起動はCEC確認用で、CD再生を開始しない。

## 任意機能

| CMake option（既定OFF） | Raspberry Pi OSの追加開発パッケージ | 用途 |
|---|---|---|
| `ENABLE_PARANOIA` | `pkg-config libcdio-paranoia-dev` | 比較用CD-DA reader |
| `ENABLE_METADATA` | `pkg-config libdiscid-dev libcurl4-openssl-dev nlohmann-json3-dev` | MusicBrainz・Cover Art参照 |
| `ENABLE_API` | `pkg-config libwebsockets-dev nlohmann-json3-dev` | HTTP操作・WebSocket状態配信 |
| `INSTALL_SYSTEMD_UNIT` | systemd運用環境 | service unitのインストール |

metadata/API有効版の例:

```sh
sudo apt install pkg-config libdiscid-dev libcurl4-openssl-dev \
  nlohmann-json3-dev libwebsockets-dev ca-certificates
cmake -S . -B build-metadata -DENABLE_METADATA=ON -DENABLE_API=ON
cmake --build build-metadata -j1
ctest --test-dir build-metadata --output-on-failure
./build-metadata/cdplayerd --player /dev/sr0 --cdda-reader direct \
  --metadata musicbrainz --metadata-cache /tmp/picdplayer-cache --api-port 8080
```

HTTPSにはCA証明書と正しいシステム時刻が必要。
build optionだけではmetadata/APIは起動せず、実行時optionも指定する。
APIは上の例では `http://127.0.0.1:8080` で待ち受ける。
paranoiaを使用する場合は `ENABLE_PARANOIA=ON` でconfigureし、`--cdda-reader paranoia` を指定する。

インストールと起動設定は[systemd手順](../manual/systemd.md)、操作例は[操作・診断](../manual/operations.md)を参照する。
