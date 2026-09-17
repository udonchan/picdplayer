# PiCDPlayer

Raspberry PiとUSB光学ドライブで物理Audio CDを再生する、家電型CDプレイヤーです。
C++20のdaemon `cdplayerd`がCD-DAを読み、ALSAからHDMIへ出力します。
metadataがなくても再生できます。Node.jsやPythonをdaemonのruntimeには要求しません。

開発・実機確認環境はRaspberry Pi 3 Model B、Raspberry Pi OS Lite、ASUS USB光学ドライブ、
REGZA TV → HDMI ARC → Marantz NR1200です。動作確認の範囲は[検証状況](docs/verification.md)を参照してください。

## 現状

| 機能 | 状況 |
|---|---|
| Audio CDの自動認識・TOC取得・HDMI連続再生 | 実装済み、通常動作を実機確認済み |
| TVリモコンのCEC再生・一時停止・停止・曲移動・シーク | 実装済み、実機確認済み |
| HTTP操作・eject・WebSocket状態配信 | 実装済み、状態照会とejectを実機確認済み |
| MusicBrainz metadata・JSON cache | 任意機能として実装済み、単一候補CDで実機確認済み |
| ジャケット画像 | URL取得まで実装。画像の取得・表示は未実装 |
| systemd常駐・自動起動 | 実装・基本構成で実機確認済み |
| ALSA underrun自動復旧 | 実装・自動試験済み。実機の異常系評価は未完了 |
| 読み取り状態・根拠の観測 | Phase 1aを実装し通常CDで確認済み。Phase 3の反復一致判定は自動試験済み、実機確認前 |
| 読み取り専用technical status画面 | Phase 1bとして実装、自動試験済み、ブラウザ実機確認待ち |
| CD-DA先読みbuffer設定・drive access直列化 | Phase 2の基礎を実装、自動試験済み、実機評価待ち |
| 複数metadata候補の選択、TV向け本番UI | 未実装 |
| quiet boot・read-only root・Buildroot image | 未実装 |

対象は音声のみのCDです。傷ディスクの評価やdirect/paranoia backendの性能比較は今後の課題です。
詳しい確認範囲と残課題は[検証状況](docs/verification.md)にまとめています。

## はじめる

[ビルド手順](docs/build.md)で依存パッケージとデバイス権限を準備した後、次を実行します。
既存のserviceが動作中の場合は、先に停止してください。

```sh
cmake -S . -B build-direct
cmake --build build-direct -j1
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct
```

Piの負荷を抑えるためビルドは `-j1` とします。終了はCtrl-Cです。
任意機能の有効化は[ビルド手順](docs/build.md)、インストール・自動起動は
[systemd運用手順](docs/systemd.md)、CLI・APIは[操作・診断手順](docs/operations.md)を参照してください。

## ドキュメント

[ドキュメント索引](docs/README.md)から、基本設計・機能設計・詳細設計、運用手順、検証状況を参照できます。
過去の試行や実機ログは `docs/history/` に保存しています。
