# Mac + Dockerで開発する

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
development packageが含まれています。`BUILD_TESTING=ON`（既定）のJavaScript動作試験用に
Node.jsとPython 3も含みます。どちらもPiのdaemon/kiosk実行時には不要で、
ハードウェアを使わないJavaScript・Python試験のために使用します。

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

`deploy.sh`はPi側の`sudo`にパスワードが必要な場合、Macの端末へ一度だけ入力を求めます。
実行時には対話可能な端末を使います。

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

## Pull RequestのCI

`.github/workflows/ci.yml`はPull Requestと手動実行で`ubuntu-24.04-arm`を使い、
上記と同じDocker image作成、`scripts/build-container.sh`によるビルド・stage、
Docker内のCTestを実行します。実機のCD-ROM、CEC、ALSA/HDMI、TV表示やPiへのdeployは
対象外です。releaseやRaspberry Pi OS imageの生成もこのworkflowでは行いません。
