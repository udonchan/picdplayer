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
│    ├─ CMake DESTDIR install
│    └─ CPack Debian package
│
├─ build-container/
├─ stage/
└─ package-container/
      │
      │ SSH / rsync / dpkg
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
development packageが含まれています。`BUILD_TESTING=ON`（既定）の自動試験用に
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
4. 同じinstall ruleからCPackで`package-container/`へDebian packageを生成する

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

`build-container/`、`stage/`、`package-container/`は生成物であり、Gitでは管理しません。
`stage/`はCMakeのDESTDIR install結果を検査するために残す。deployするartifactは
`package-container/`内のDebian packageである。

### 自動試験を実行する

ビルド後、同じimageで実行します。build script自体はCTestを実行しません。

```sh
docker run --rm -v "$PWD:/src" -w /src picdplayer-build \
  ctest --test-dir build-container --output-on-failure
```

標準構成はmetadata/API有効、paranoia無効です。Node.jsとPython 3を含めて30件を登録します。
実機deviceの代わりにfake、ALSA null、存在しないCD deviceを使用する試験があり、
loopback socket通信を許可した環境が必要です。実機の試聴・CEC・TV表示は別に確認します。

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

`build-container.sh`はCMakeの同じ`install()`規則から、aarch64用の`.deb`を1個生成する。
CPackが動的library依存をDebian dependencyとして算出し、dpkgは旧versionが所有するファイルを
更新・削除する。そのためroot filesystemへのrsyncや`rsync --delete`は使わない。

deploy scriptは概ね次の順序で処理します。

```text
Mac package-container/picdplayer_*.deb
    │
    │ rsync
    ▼
Pi ~/picdplayer-package/picdplayer.deb
    │
    └─ sudo dpkg -i
         ├─ package install
         ├─ systemd daemon-reload
         └─ activeだったdaemon → kioskを順にrestart
```

packageのpostinstは、起動済みsystemd hostだけでdaemon-reloadと`try-restart`を実行する。
inactive serviceを新たにstart/enableせず、初回のservice user作成、runtime package導入、service enableは
[systemd手順](systemd.md)に従って一度だけ行う。CPU負荷の調査などで意図的に停止中のserviceは
deploy後も停止したままとし、deploy scriptは前後のactive stateが一致することを確認する。
実機検証時だけ手動で起動する。

### passwordless deploy（信頼する開発者用）

同じ開発者がMacのsource/packageとPiのSSH accountを管理する開発環境では、初回だけPiで
限定したsudoers ruleを設定できる。`<development-user>`を実際のlogin nameへ置き換える。

```sudoers
<development-user> ALL=(root) NOPASSWD: /usr/bin/dpkg -i -- /home/<development-user>/picdplayer-package/picdplayer.deb
```

rootで`visudo -f /etc/sudoers.d/picdplayer-deploy`を使って保存し、`visudo -cf`で構文を検査する。
このruleは`deploy.sh`が使う固定path・固定filename・`dpkg -i`だけを許可する。SSH accountが
そのpackageを置き換えられるため、この設定はそのaccountとMacのbuild環境にroot相当のdeploy権限を
委譲する。複数利用者・CI・配布artifactには使わず、署名検証を含む別のrelease設計で扱う。

設定後は、password promptなしで許可されたcommandを確認できる。これはinstallを実行しない。

```sh
ssh picdplayer-pi 'sudo -n -l /usr/bin/dpkg -i -- ~/picdplayer-package/picdplayer.deb'
```

sudoers設定をしていない環境では、deploy scriptは従来どおり
一度だけpasswordを求める。

### 日常の開発フロー

Docker build imageを一度作成した後は、通常の変更ではビルド、自動試験、deployの順に進めます。

```sh
./scripts/build-container.sh
docker run --rm -v "$PWD:/src" -w /src picdplayer-build ctest --test-dir build-container --output-on-failure
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
