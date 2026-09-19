# PiCDPlayer

PiCDPlayerは、Raspberry Piと一般的な光学ドライブを使い、物理CDを現代のTV・オーディオ環境で
楽しむためのCDプレイヤーです。CDを手に取り、トレイへ入れ、TVリモコンで再生する。
その体験を、独立して常時動作する家電として実現することを目指しています。

物理CDを使う楽しさを残しながら、Linux、HDMI、HDMI-CEC、インターネット上の曲名・ジャケット情報を
組み合わせて、据え置きプレイヤーの使い方を再設計します。普段の操作でデスクトップを開いたり、
音楽ライブラリへ登録したりする必要のない装置が目標です。現在は再生daemon、ブラウザ表示、
任意導入のTV kiosk serviceまでを実装しており、再bootからのkiosk運転確認とLinux起動画面を隠す
仕上げはこれからです。

この開発では、音を出すだけでなく、CDを再生する仕組みを観測し、説明できるようにします。
例えば音が途切れたとき、ドライブの読み取りが遅れたのか、音声出力への供給が間に合わなかったのかを
調べられること。曲名の取得に失敗しても、CDの再生は続けられること。そのために、ディスクの状態、
再生の進行、表示情報を明示的に扱い、一つのdaemonが装置全体を管理します。

読み取りの信頼性（playback integrity）についても、確認できたことの範囲を大切にします。
「読み取れた」「同じデータを複数回得た」「独立した読み取りで一致した」「原盤と一致した」
「出力までbit-perfectだった」は、それぞれ別の確認です。**正確さに加え、正確さについて嘘をつかないこと**を
設計上の目標とし、観測だけでsecureやperfectを断言しません。
考え方は[読み取り結果について言えること](docs/guide/integrity.md)、現在の判定仕様は
[機能設計](docs/design/functional-design.md)で説明しています。

## 特徴

- Raspberry Piと一般的なUSB光学ドライブで、CD-DAをnative APIから読み取り、HDMIへ音声を出力します。
- HDMI-CECでTVリモコンの再生・停止・曲移動・シークを受け付け、systemdから常駐できます。
- CDの曲配置（TOC）を基に曲名やジャケットを取得し、ブラウザのNow Playing画面に表示します。
- CD再生はネットワークやmetadataサービスに依存せず、曲番号と時間だけでも利用できます。
- ドライブ、ディスク、再生の状態と読み取りの根拠をAPI・診断画面へ公開し、未確認事項も明示します。

C++20のdaemon `cdplayerd`が中心です。Node.jsやPythonをdaemonのruntimeには要求しません。
HDMIを標準の音声出力・TV表示先とする。Now Playing画面はbrowserで表示でき、任意導入の
Chromium/Cage kiosk用systemd unitも用意している。quiet bootと専用imageは別段階で扱う。

開発・実機確認環境はRaspberry Pi 3 Model B、Raspberry Pi OS Lite、ASUS USB光学ドライブ、
REGZA TV → HDMI ARC → Marantz NR1200です。動作確認の範囲は[検証状況](docs/development/verification.md)を参照してください。

## 現状

| 機能 | 状況 |
|---|---|
| Audio CDの自動認識・TOC取得・HDMI連続再生 | 実装済み、通常動作を実機確認済み |
| TVリモコンのCEC再生・一時停止・停止・曲移動・シーク | 実装済み、実機確認済み |
| HTTP操作・eject・WebSocket状態配信 | 実装済み、状態照会とejectを実機確認済み |
| MusicBrainz metadata・JSON cache | 任意機能として実装済み、単一候補CDで実機確認済み |
| 読み取り専用Now Playing画面 | 実装済み。metadata・CAA URLがあればalbum/track/cover artをブラウザに表示。Chromium/Cage kiosk unitを実装、TV表示を確認済み、boot・継続運転は確認待ち |
| ジャケット画像 | CAA URL取得とNow Playing画面でのbrowser表示を実装。daemonの画像binary cacheは未実装 |
| systemd常駐・自動起動 | 実装・基本構成で実機確認済み |
| ALSA underrun自動復旧 | 実装・自動試験済み。実機の異常系評価は未完了 |
| 読み取り状態・根拠の観測、反復一致 | 実装・通常CDで確認済み。起動中の設定切替は実機確認済み |
| 読み取り専用technical status画面 | Phase 1bとして実装、通常再生・再読み込み・再接続をブラウザで実機確認済み |
| CD-DA先読みbuffer設定・drive access直列化 | Phase 2を実装。通常CDで容量・開始閾値と操作応答を実機比較済み。速度制御は未実装 |
| 複数metadata候補の選択 | 未実装 |
| quiet boot・read-only root・Buildroot image | 未実装 |

対象は音声のみのCDです。傷ディスクの評価やdirect/paranoia backendの性能比較は今後の課題です。
詳しい確認範囲と残課題は[検証状況](docs/development/verification.md)にまとめています。

## はじめる

[ビルド手順](docs/manual/build.md)で依存パッケージとデバイス権限を準備した後、次を実行します。
既存のserviceが動作中の場合は、先に停止してください。

```sh
cmake -S . -B build-direct
cmake --build build-direct -j1
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct
```

Piの負荷を抑えるためビルドは `-j1` とします。終了はCtrl-Cです。
任意機能の有効化は[ビルド手順](docs/manual/build.md)、インストール・自動起動は
[systemd運用手順](docs/manual/systemd.md)、CLI・APIは[操作・診断手順](docs/manual/operations.md)を参照してください。

## ドキュメント

[Guide](docs/guide/README.md)で仕組みと設計意図を順に読めます。
[ドキュメントの案内](docs/README.md)から、設計書、運用手順、検証状況を参照できます。
過去の試行や実機ログは `docs/history/` に保存しています。
