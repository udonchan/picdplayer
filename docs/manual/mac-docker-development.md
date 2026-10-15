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

`picdplayer-build` imageには、CMake、Ninja、C++ compiler、ALSAやmetadata/API機能、任意の
libcdio-paranoia readerに必要なdevelopment packageが含まれています。`BUILD_TESTING=ON`（既定）の自動試験用に
Node.jsとPython 3も含みます。`BUILD_TESTING=ON`ではPython 3が必須で、見つからない場合は
CMake configure時に失敗します。どちらもPiのdaemon/kiosk実行時には不要で、
ハードウェアを使わないJavaScript・Python試験のために使用します。

Dockerfileを変更した場合は、build imageを再作成してください。profile試験の実multi-config拒否確認に
Ninjaを追加したため、既存imageも更新が必要です。WPE SDK image/recipeの更新ではありません。

現在この手順を確認しているのはApple Silicon Macです。Intel Macやx86-64 Linux hostを含む
異なるarchitectureからのcross buildは、現時点では標準の開発手順として検証していません。

### ビルドする

通常のビルドはrepository rootから次を実行します。

```sh
./scripts/build-container.sh
```

このscriptは次の処理を行います。

1. WPE 2.54.0と2patchのSDKをcacheから取得し、なければsource buildする
2. `picdplayer-build` container内でCMakeをconfigureする
3. Linux/aarch64向けにビルドする
4. CMakeのinstall ruleを使い、`stage/`へDESTDIR installする
5. 同じinstall ruleからCPackで`package-container/`へDebian packageを生成する

主なCMake設定は次の通りです。

applicationのbuild typeは`PICDPLAYER_BUILD_TYPE`（既定`Release`）で選択する。
`Debug` / `RelWithDebInfo` / `MinSizeRel`も選択できる。診断profile・Inspector・ログの対応と
旧artifactを拒否するdeploy手順は[Application Profile](build.md#application-profile)参照。
WPE SDKのRelease recipe/cacheは共用し、applicationのDebug選択ではWebKitを再compileしない。
CIは同じSDKでReleaseとDebugのbuild/test/package lifecycleを順に実行し、候補artifactはReleaseだけ保持する。

```text
CMAKE_BUILD_TYPE=Release (PICDPLAYER_BUILD_TYPEで変更可能)
ENABLE_METADATA=ON
ENABLE_API=ON
INSTALL_SYSTEMD_UNIT=ON
INSTALL_SYSTEMD_KIOSK_UNIT=ON
ENABLE_WPE_KIOSK=ON
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
WPE launcher/post-stop helperも`usr/local/libexec/`、WPE runtimeも
`stage/opt/picdplayer-wpe/2.54.0/`へ含まれる。SDK cacheは`build-wpe-runtime/`でGit対象外。
新しいruntime fileを追加するときは、deploy scriptへ個別のcopy処理を追加するのではなく、
原則としてCMakeのinstall ruleへ追加します。

`build-container/`、`stage/`、`package-container/`は生成物であり、Gitでは管理しません。
`stage/`はCMakeのDESTDIR install結果を検査するために残す。deployするartifactは
`package-container/`内のDebian packageである。ビルド開始時に前回のpackageを破棄し、
configure/build/package検証に失敗した場合はdeploy可能な`.deb`を残さない。

### 自動試験を実行する

ビルド後、同じimageで実行します。build script自体はCTestを実行しません。

```sh
docker run --rm -v "$PWD:/src" -w /src picdplayer-build \
  ctest --test-dir build-container --output-on-failure
```

標準構成はmetadata/API有効、paranoia無効です。Node.jsとPython 3を含め、2026-09-28の標準構成では44件を登録します。
件数の正本はCMakeの登録結果です。`ctest --test-dir build-container -N`で確認できます。
実機deviceの代わりにfake、ALSA null、存在しないCD deviceを使用する試験があり、
loopback socket通信を許可した環境が必要です。実機の試聴・CEC・TV表示は別に確認します。

### 任意のparanoia build

実験・検証のためにlibcdio-paranoia readerを含める場合は、同じbuild scriptへ
`PICDPLAYER_ENABLE_PARANOIA=ON`を指定する。

```sh
docker build -t picdplayer-build .
PICDPLAYER_ENABLE_PARANOIA=ON ./scripts/build-container.sh
docker run --rm -v "$PWD:/src" -w /src picdplayer-build \
  ctest --test-dir build-container --output-on-failure
```

標準のCIと通常の`.deb`はparanoia無効のままである。paranoia有効の生成物を再配布する場合の
ライセンス条件は[#66](https://github.com/udonchan/picdplayer/issues/66)で整理中であり、実験・検証用途と
正式releaseを混同しない。Pi上でコンパイルせず、このDocker buildで生成したpackageをdeployする。

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
ただし初回package化より前に手動・rsyncで導入し、現在のpackageにも含まれない旧ファイルは
管理対象にならず、自動削除しない。現在も同じpathへinstallするファイルはpackage管理へ移行する。
`/usr/local`を維持する開発者専用packageであり、Debian公式配布向けのpackageではない。

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
実機検証時だけ手動で起動する。deploy前は両serviceが`active`または`inactive`であることを
要求し、`failed`や遷移中の状態は転送前にエラーとする。deploy後は状態の一致とpackageの
`install ok installed`を検査する。これは観測時点の確認であり、その後のcrashやTV表示・再生の
正常性までは保証しない。
標準deployはWPE launcherのversion 2.54.0も検査する。kioskがactiveの場合はMainPIDのprocess名を
最大35秒待って確認し、Chromium fallbackをWPE成功として報告しない。Chromiumを明示運用している
場合のみ`PICDPLAYER_EXPECT_KIOSK_RUNTIME=chromium ./scripts/deploy.sh`を使う。
このoptionはtarget設定を変更せず、期待値だけを変える。inactiveなら起動して検査しない。

upgrade時は実行中のserviceを残してdpkgがファイルを更新し、configure後に再起動する。
標準UIはdaemon内に埋め込まれ、Custom UIも起動時に読み込む。新しい配信内容と表示の確認は
daemonとkioskの再起動後に行う。
remove時はkiosk→daemonの順に停止し、停止に失敗したらファイル削除へ進まずエラーを返す。
CMakeでunitのinstallを無効にしたpackageは、そのunitのrestart/stopを行わない。
初回install時のenableや設定・cacheの削除は行わない。

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

sudoers設定がない、または固定commandと一致しない場合、deploy scriptはpackage転送前に
エラー終了する。password promptには進まない。

WPE供給を同じpackageへ統合したため、通常deployのsudoersは上記の既存`dpkg -i` ruleだけで
足りる。`cp`/`tar`/`env`/shell、`/opt`書き込みや任意`systemctl`のNOPASSWD権限は追加しない。
新しい依存が不足する初回だけ、利用者が対話的sudoで依存を導入する。deployはaptやsudoersを
変更しない。手動切り替え時の`/etc/default/picdplayer-wpe`と`90-wpe-source.conf`も自動上書きしない。
同じ2.54.0 prefixを使うため残して運用できるが、設定の変更/撤去は利用者が行う。

実機測定でdaemonとkioskを個別に起動・停止・再起動する場合は、必要なunitだけを次のように
追加で許可できる。`/usr/bin/systemctl`のpathはPiで`command -v systemctl`を確認する。

```sudoers
Cmnd_Alias PICDPLAYER_SERVICES = \
    /usr/bin/systemctl start picdplayer.service, \
    /usr/bin/systemctl stop picdplayer.service, \
    /usr/bin/systemctl restart picdplayer.service, \
    /usr/bin/systemctl start picdplayer-kiosk.service, \
    /usr/bin/systemctl stop picdplayer-kiosk.service, \
    /usr/bin/systemctl restart picdplayer-kiosk.service

<development-user> ALL=(root) NOPASSWD: PICDPLAYER_SERVICES
```

既存の`dpkg -i` ruleは残す。`visudo -cf /etc/sudoers.d/picdplayer-deploy`で構文を確認し、
起動はdaemon→kiosk、停止はkiosk→daemonの順に個別の`systemctl` commandを実行する。
`daemon-reload`、`enable`、任意のunit名は追加しない。

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

### packageの更新・削除を確認する

packageを生成した後、次で同じDebian arm64 build imageの使い捨てcontainerにおいて、旧版からの更新、
同一artifactの再install、purgeを確認できます。

```sh
./scripts/test-package-lifecycle.sh
```

Debug等のpackageを検証するときは、build時と同じ`PICDPLAYER_BUILD_TYPE`を指定する（既定Release）。
先にarchiveのmarkerと実binary/wrapperのprofileをnetworkなしcontainerで照合する。

この検証は旧package所有fileが更新時に削除されること、packageが所有しないfileが保持されること、
purge後にpackage所有fileが残らないことを確認する。Piへ接続せず、host filesystemも変更しない。
CIも`build-container.sh`の後に同じscriptを実行する。

`dpkg -i`はtransactional rollbackを提供しない。Piでconfigure失敗や中断を確認した場合は、まず
`sudo dpkg --audit`、`sudo systemctl status picdplayer.service picdplayer-kiosk.service`、
`journalctl -u picdplayer.service -u picdplayer-kiosk.service -b`で状態を記録する。原因を解消した後、
既知の正常なartifactを`./scripts/deploy.sh`で再installする。packageの依存関係などで半configured状態が
残る場合は、Piのconsoleで`sudo dpkg --configure -a`を実行してからservice状態を再確認する。
この復旧操作は限定sudoers ruleの対象外であり、対話的な管理者権限を使う。

Dockerは再現可能なLinux/aarch64ビルド環境として使用し、
Raspberry Piは実際のCDドライブ、HDMI audio、CEC、TV表示、systemd起動などを
確認するruntime targetとして扱います。

## Pull RequestのCI

`.github/workflows/ci.yml`はPull Requestと手動実行で`ubuntu-24.04-arm`を使い、
上記と同じDocker image作成、`scripts/build-container.sh`によるビルド・stage、
Docker内のCTest、使い捨てcontainer内の`dpkg` install/upgrade/reinstall/purge検証を実行します。実機のCD-ROM、CEC、ALSA/HDMI、TV表示やPiへのdeployは
対象外です。公開releaseやRaspberry Pi OS imageの生成もこのworkflowでは行いません。
WPE SDKはsource/patch/設定hash単位でActions cacheへ保存する。初回source buildを考慮し、
job上限は180分。cacheを利用した成功はclean buildやbyte-identical再現性の証明ではない。

### master側の共通WPE SDK cache

`.github/workflows/wpe-sdk.yml`はWPE SDKだけを準備・保存する専用workflowである。
`master`へのSDK recipe/関連workflow変更のpush、または`master`を指定した手動実行で動く。
job自体も`refs/heads/master`だけを受け付ける。feature branchでの手動実行はjobをskipし、
PRや`pull_request_target`からmaster側のcacheを書き換える経路は用意しない。

このworkflowをmasterへ取り込んだ後、手動で準備する場合は次を実行する。

```sh
gh workflow run wpe-sdk.yml --ref master
```

通常CIと同じcache key・`build-wpe-runtime/`を使い、restore後も既存の
`bash scripts/build-wpe-runtime.sh`によるrecipe/SDK marker検査を行う。SDK準備が成功した直後に
cacheを保存し、アプリbuild・CTest・package生成やPi deployは行わない。アプリの通常CIは変更せず、
fresh checkoutから自身をbuild/testしながら、自身のscopeまたはmasterで利用可能なcacheをWPEに再利用する。

Actions cacheはscopeを持ち、別PRの`refs/pull/.../merge`や兄弟feature branchにある同じkeyのcacheを
masterへrestoreできない。PR #223のcacheがあるだけではmasterの初回seedが済んだことにはならない。
master側に同じrecipeのSDKがない場合、この専用jobは一度source buildするため約2時間級の負荷があり得る。
通常PRのCIは本変更ではcache miss時のsource build fallbackを残す。master cacheの消失・recipe変更時も
必ず短時間で済むとは保証せず、まず専用workflowで準備してからPRのbuildを開始する。

cache keyはWPE source/probe/patch/SDK export scriptの入力に対応し、アプリ・UI・文書だけの変更では
更新しない。restore/saveは分離し、失敗・取消で未完成SDKを成功cacheとして保存しない。
cache保存完了と、次のPRでの実restore・所要時間短縮は別の検証である。専用workflowのmerge/実行前に
共通cacheの運用開始済みとは扱わない。正式SDK配布・依存完全固定・ABI/再現性精査は引き続き後続課題。

2026-10-14の実GitHub PoCでは、#224のmaster統合と初回seed成功後、新規PR #225の最初の通常CIが
master cacheを復元し、WebKit再buildなしで全62試験・package lifecycle・候補検証まで3分30秒で成功した。
初回seedは別途2時間21分12秒を要した。cold #223の2時間31分29秒に対する97.69%短縮は単回測定であり、
今後のcache消失やrecipe変更での所要時間を保証しない。[証跡](../development/verification.md#github初回seedと別pr-poc2026-10-14-utc)参照。

## 版付き候補artifactの検査と保存

CIはbuild、全CTest、package lifecycleの成功後に、`bash scripts/prepare-release.sh`で
候補を検査し、`.deb`と`manifest.json`をActions artifactへ7日間保存する。これは公開releaseでも
配布許可でもなく、#45の限定実装である。本体ライセンス・依存監査は[#66](https://github.com/udonchan/picdplayer/issues/66)、
配布metadataは[#67](https://github.com/udonchan/picdplayer/issues/67)で未完了。標準のparanoia OFFだけを受け付ける。

版の正本はCMakeの`project(... VERSION ...)`であり、configure済みの版とDebian `Version`が一致する必要がある。
refがtagの場合は`refs/tags/vX.Y.Z`だけを受け付け、CMakeの版との一致も検査する。
tagのpushや公開処理は追加していない。PRではcheckoutされたmerge commitを識別するため、source commitは
`git rev-parse HEAD`の完全な値を使い、PR headのSHAと混同しない。

検査はpackageが一つのregular fileであること、`Package=picdplayer`、`Architecture=arm64`、
非空のruntime `Depends`、stageとpayloadのfile一覧・内容・mode・symlink先の一致を要求する。
加えてRelease marker全文と実archive内daemon/WPE binary・wrapperのprofileを照合する。
identity CLIは秘密情報を引き継がない環境で実行する。信頼するローカル成果物だけが対象である。
install一覧はCMake、runtime依存はCPack/shlibdepsを正本とし、workflowに一覧を複製しない。
manifestには版、commit、ref、dirty状態、package名、Depends、SHA256、payloadを記録する。
`depends`はDebian fieldの原文、`runtime_depends`は同じ値から導出した依存groupの配列である。
`a | b`の代替依存は一つのgroupとして保持し、独立した必須packageとして読み替えない。
内容一致はstageとの比較であり、期待する製品仕様すべてや実機動作を証明するものではない。

ローカルでは通常のbuildと検証後に同じコマンドを実行する。

```sh
./scripts/build-container.sh
docker run --rm -v "$PWD:/src" -w /src picdplayer-build \
  ctest --test-dir build-container --output-on-failure
./scripts/test-package-lifecycle.sh
bash scripts/prepare-release.sh
```

検査成功時だけ`release-container/`に候補を作る。再実行開始時に前回の候補を破棄し、検査失敗時には
新しい候補を残さない。finalizerはhostのUID/GIDでcontainerを動かして保存し、Linux hostでも再実行時に
候補を削除できるようにする。Dockerを起動できない場合もwrapperが前回候補を先に破棄する。
コマンドの成功を確認せず残存fileを採用しない。生成先がsymlinkの場合は削除せずエラーにする。
開発中のdirty sourceはmanifestへ明示し、CIはclean sourceだけを保存する。手動検査はCTestやlifecycleの
成功を自動で証明しないので、上記順序を省略しない。

失敗時はログとmetadataを確認し、原因を修正して同じcommit・構成からbuildと検証を再実行する。
以前の正常候補を復旧に使う場合も、manifestのSHA256・版・commitを確認し、Pi操作は既存deploy手順と
復旧手順に従う。この手順は再生成可能な操作を提供するが、image/package版やtimestampを固定していないため
byte-identicalな再現性・署名・正式配布条件・Piでの動作は保証しない。
