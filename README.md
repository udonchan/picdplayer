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
HDMIを標準の音声出力・TV表示先とします。Now Playing画面はbrowserで表示でき、任意導入の
Chromium/Cage kiosk用systemd unitも用意しています。quiet bootと専用imageは別段階で扱います。

開発・実機確認環境はRaspberry Pi 3 Model B、64-bit Raspberry Pi OS Lite、ASUS USB光学ドライブ、
REGZA TV → HDMI ARC → Marantz NR1200です。動作確認の範囲は
[検証状況](docs/development/verification.md)を参照してください。

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

### Raspberry Pi上で直接ビルドする

Raspberry Pi単体で試す場合は、
[ビルド手順](docs/manual/build.md)で依存パッケージとデバイス権限を準備した後、次を実行します。
既存のserviceが動作中の場合は、先に停止してください。

```sh
cmake -S . -B build-direct
cmake --build build-direct -j1
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct
```

Raspberry Pi 3の負荷を抑えるため、Pi上でのビルドは `-j1` とします。終了はCtrl-Cです。

この方法は初期セットアップ、単体での動作確認、開発環境を別途用意しない場合に利用できます。
通常の開発では、Raspberry Piをビルドマシンではなく実行・ハードウェア検証環境として扱い、
次節のMac + Docker環境を使用します。

任意機能の有効化は[ビルド手順](docs/manual/build.md)、インストール・自動起動は
[systemd運用手順](docs/manual/systemd.md)、CLI・APIは
[操作・診断手順](docs/manual/operations.md)を参照してください。

## Mac + Dockerで開発する

通常の開発では、ソース編集とビルドをMac側で行い、Raspberry Piへ成果物をdeployして
実機確認します。

現在確認している開発ホストはApple Silicon Macです。Docker上でDebian Trixie arm64を動かし、
Raspberry Pi向けのLinux/aarch64 binaryをビルドします。

Apple Siliconではarm64 Linux containerをnative architectureのまま実行できるため、
通常の開発ではRaspberry Pi自身にコンパイル負荷をかける必要がありません。

開発フローは次のようになります。

```text
Apple Silicon Mac
│
├─ source / editor
│
├─ Docker: Debian Trixie arm64
│    ├─ CMake configure
│    ├─ C++ build
│    └─ CMake DESTDIR install
│
├─ build-container/
└─ stage/
      │
      │ SSH / rsync
      ▼
Raspberry Pi
├─ systemd
├─ cdplayerd
├─ Cage / Chromium
├─ optical drive
├─ ALSA / HDMI
└─ HDMI-CEC
```

Raspberry Pi側では、光学ドライブ、ALSA、HDMI、CEC、DRM/KMS、Cage、Chromiumなど、
実機でなければ確認できない部分の試験を行います。

### Docker build imageを作る

Dockerを利用できる状態でrepository rootから次を実行します。

```sh
docker build -t picdplayer-build .
```

`picdplayer-build` imageには、CMake、C++ compiler、ALSAやmetadata/API機能に必要な
development packageが含まれています。

Dockerfileを変更した場合は、build imageを再作成してください。

現在この手順を確認しているのはApple Silicon Macです。Intel Macやx86-64 Linux hostを含む
異なるarchitectureからのcross buildは、現時点では標準の開発手順として検証していません。

### ビルドする

通常のビルドはrepository rootから次を実行します。

```sh
./scripts/build-container.sh
```

このscriptは次の処理を行います。

1. `picdplayer-build` container内でCMakeをconfigureする
2. Linux/aarch64向けにビルドする
3. CMakeのinstall ruleを使い、`stage/`へDESTDIR installする

主なCMake設定は次の通りです。

```text
CMAKE_BUILD_TYPE=Release
ENABLE_METADATA=ON
ENABLE_API=ON
INSTALL_SYSTEMD_UNIT=ON
INSTALL_SYSTEMD_KIOSK_UNIT=ON
CMAKE_INSTALL_PREFIX=/usr/local
PICDPLAYER_SERVICE_USER=picdplayer
```

ビルド結果は `build-container/` に保持されるため、通常の変更ではincremental buildが利用できます。

一方、`stage/` は毎回作り直されます。ここにはCMakeが定義するインストール対象が、
Raspberry Pi上のfilesystem layoutを保った状態で生成されます。

例:

```text
stage/
└── usr/local/
    ├── bin/
    │   └── cdplayerd
    ├── libexec/
    │   └── picdplayer-kiosk
    ├── lib/systemd/system/
    │   ├── picdplayer.service
    │   └── picdplayer-kiosk.service
    └── share/picdplayer/
        └── ui/
            └── default/
```

インストール対象と配置先のsource of truthはCMakeの `install()` ruleです。
新しいruntime fileを追加するときは、deploy scriptへ個別のcopy処理を追加するのではなく、
原則としてCMakeのinstall ruleへ追加します。

`build-container/` と `stage/` は生成物であり、Gitでは管理しません。

### Raspberry Piへdeployする

通常のdeployは次のコマンドで行います。

```sh
./scripts/deploy.sh
```

deploy先はデフォルトでSSH host alias:

```text
picdplayer-pi
```

です。

IP address、username、秘密鍵などの環境依存情報はrepositoryへ保存せず、
開発マシンの `~/.ssh/config` で設定します。

例えば:

```sshconfig
Host picdplayer-pi
    HostName <Raspberry PiのIP addressまたはhostname>
    User <username>
    IdentityFile ~/.ssh/id_ed25519
```

接続できることは次のコマンドで確認できます。

```sh
ssh picdplayer-pi
```

別のSSH targetへdeployする場合は環境変数で指定できます。

```sh
PICDPLAYER_TARGET=picdplayer-test ./scripts/deploy.sh
```

deploy scriptは概ね次の順序で処理します。

```text
Mac stage/
    │
    │ rsync
    ▼
Pi ~/stage/
    │
    ├─ kiosk service停止
    ├─ daemon service停止
    │
    ├─ staged filesystem treeをinstall
    │
    ├─ systemctl daemon-reload
    │
    ├─ daemon service起動
    └─ kiosk service起動
```

`~/stage/` はRaspberry Piのroot filesystem全体を表すものではなく、
PiCDPlayerがインストールするファイルだけを含みます。

そのため、`~/stage/` から `/` への反映には `rsync --delete` を使用しません。

現在のdeploy方式では、以前のversionではインストールされていたものの、
後からCMakeのinstall ruleから削除された古いファイルは自動削除されません。
これが実際の問題になった場合は、install manifestによる管理やDebian package化を検討します。

### 日常の開発フロー

Docker build imageを一度作成した後は、通常の変更では次の2コマンドが基本です。

```sh
./scripts/build-container.sh
./scripts/deploy.sh
```

つまり、

```text
edit on Mac
    ↓
build in Debian Trixie arm64 container
    ↓
CMake DESTDIR install
    ↓
deploy over SSH
    ↓
test on Raspberry Pi
```

という役割分担です。

Dockerは再現可能なLinux/aarch64ビルド環境として使用し、
Raspberry Piは実際のCDドライブ、HDMI audio、CEC、TV表示、systemd起動などを
確認するruntime targetとして扱います。

## ドキュメント

画面の編集は[Custom UI](docs/manual/custom-ui.md)を参照してください。起動時の静的検証と
built-in画面へのfallbackを実装しています。設定画面・hot reloadは今後の対象です。

[Guide](docs/guide/README.md)で仕組みと設計意図を順に読めます。
[ドキュメントの案内](docs/README.md)から、設計書、運用手順、検証状況を参照できます。
過去の試行や実機ログは `docs/history/` に保存しています。

エージェントによる開発では、repository rootの
[`AGENTS.md`](AGENTS.md)に記載したビルド・deploy・実機操作の規約も参照してください。
