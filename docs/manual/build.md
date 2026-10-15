# Linux単体ビルド手順

通常の開発・CIは[Mac + Docker開発手順](mac-docker-development.md)を使う。
この文書は別の開発ホストを用意せずLinux上で直接試す場合の補助手順である。
通常のPiはruntime/hardware検証だけに使用する。

## Application Profile

`CMAKE_BUILD_TYPE`はfresh build treeで未指定、または明示的に空なら`Release`に確定する。
既存treeで引数を省略した場合はCMake cacheの値を維持する。single-config generatorを使用し、
未知の値・綴り違い・multi-config generatorはconfigure時に拒否する。

| Build type | 診断profile | WPE Inspector / Chromium debug port既定 | daemonログ既定 |
|---|---|---|---|
| `Debug` | Debug | ON、loopback限定 | DEBUG以上 |
| `Release` | Release | OFF | NOTICE・WARN・ERROR |
| `RelWithDebInfo` | Release | OFF | NOTICE・WARN・ERROR |
| `MinSizeRel` | Release | OFF | NOTICE・WARN・ERROR |

最適化とsymbolsはCMakeのbuild typeに従い、診断profileは上表で別途決める。
HTTP API/WS、technical status、read evidence、metadata・背景取得の可否はprofileで変えない。
WPE SDKは従来のRelease recipe/cacheを共用し、applicationのDebug選択ではWebKitを再buildしない。
`scripts/build-wpe-runtime.sh`とSDK workflowのcache keyも変更しない。

標準container build/package/deployは`PICDPLAYER_BUILD_TYPE`（既定Release）で選択する。

```sh
PICDPLAYER_BUILD_TYPE=Debug ./scripts/build-container.sh
docker run --rm -v "$PWD:/src" -w /src picdplayer-build ctest --test-dir build-container --output-on-failure
PICDPLAYER_BUILD_TYPE=Debug ./scripts/test-package-lifecycle.sh
# 実機deployはservice再起動を伴うため、承認・復元条件の確認後だけ実行する。
PICDPLAYER_BUILD_TYPE=Debug ./scripts/deploy.sh
```

`build-container/`・`stage/`・`package-container/`は最新一構成専用で、並列profile buildには使わない。
切替時は再configure/buildし、stageを再作成する。新規buildは不正入力・失敗時も旧packageを無効化する。
package名にbuild typeを付け、CMakeが同じ設定から生成する
`/usr/local/share/picdplayer/build-profile`をstage/packageへinstallする。
`cdplayerd --build-info`でもbinaryのbuild type/profileを確認できる。
build/package lifecycle/deploy/candidateは`scripts/package_profile.py`で実archiveのmarker全文と
package version、daemon・WPE binaryのbuild type/profile、生成wrapperのprofileを照合する。
旧形式・欠損・重複field・矛盾・binary混在は拒否し、deployはPiへの接続前に検査する。
この検査はnetworkなしの標準Docker build image内でarchiveを一時展開し、秘密情報や診断overrideを
引き継がない環境でidentity CLIだけを実行する。maintainer scriptは実行しない。
信頼するローカルCPack成果物専用であり、未信頼packageの安全性検査ではない。host/Piで直接実行しない。
インストール後もmarker全文と両binaryのidentityを確認するが、runtime overrideや実機の描画・音声確認の代わりではない。
既存packageの作成日やcheckoutとの新旧を自動判定する機能ではないため、必ず対象sourceから再buildする。
`prepare-release.sh`のcandidate検証はRelease限定であり、Debug packageは公開候補へ混入させない。
運用overrideの優先順位は[操作・診断](operations.md#診断profileとoverride)を参照する。

## 必要環境

Linux、C++20コンパイラ、CMake 3.20以上、ALSAとLinux CEC/CD-ROMの開発ヘッダーが必要。
Raspberry Pi OSでは次のパッケージを使用する。Pythonは開発用試験のみ。

```sh
sudo apt install build-essential cmake ninja-build libasound2-dev linux-libc-dev python3
cmake -S . -B build-direct
cmake --build build-direct -j1
ctest --test-dir build-direct --output-on-failure
```

Pi単体で試す場合はメモリ・CPU負荷を抑えるため `-j1` とする。
Dockerの標準scriptはcontainerのCPU数に応じて並列ビルドする。
`build/`・`build-*/` はGit管理対象外。

標準Docker buildはWPE kioskを有効にする。`scripts/build-container.sh`は
`scripts/build-wpe-runtime.sh`で公式WPE 2.54.0と2patchをビルドし、SDKを
`build-wpe-runtime/2.54.0`へexportしてからアプリをcompileする。source/patch/設定のhashが
一致するSDKは再利用し、アプリ変更だけでWebKitを再compileしない。SDK cacheがない場合は
Docker volume `picdplayer-wpe-runtime-2.54.0`の増分buildを使う。
既存試験volumeを使う場合は`PICDPLAYER_WPE_VOLUME=picdplayer-wpe221-build`を指定できる。
初回は最大6 GiB/2 CPU/並列2でWebKitをビルドするため時間がかかる。SDK cacheを更新する場合は
生成物`build-wpe-runtime/recipe`だけを削除して再実行する。volumeは自動削除しない。

CMakeはlauncherを`/usr/local/libexec`、runtimeのlib/libexec/shareを
`/opt/picdplayer-wpe/2.54.0`へinstall/packageする。header/pkg-configはSDK専用でpackageに入れない。
wrapperだけがprivate library pathを設定し、ホストのglibc/ldconfigを置き換えない。
`bubblewrap`/`xdg-dbus-proxy`は明示runtime依存、共有library依存はCPack/shlibdepsを正本とする。
標準Dockerfileを更新したため、既存imageは`docker build -t picdplayer-build .`で再生成する。
完全なbase/apt固定と再現性精査はv1.0段階まで保留する。

CI用の共通SDKはmaster限定の`.github/workflows/wpe-sdk.yml`で同じexport scriptとrecipeを使って
準備・保存する。通常CIとcache key/pathを合わせ、アプリはfresh checkoutからbuild/testする。
初回seed・cache scope・手動実行と未確認範囲は[共通WPE SDK cache](mac-docker-development.md#master側の共通wpe-sdk-cache)参照。
この追加workflowはローカルSDK/volume、build recipe、標準install/package内容を変更しない。

## TrixieでのWPE source build成立性調査（#221）

これは[#220](https://github.com/udonchan/picdplayer/issues/220)の供給方式を判断するための
[#221](https://github.com/udonchan/picdplayer/issues/221)の試験で、標準buildや製品runtimeではない。
WPE WebKit 2.54.0の公式sourceをSHA256固定で取得し、Trixie/arm64の依存でビルドする。
sid repository、ホストのglibc更新、Pi deploy、Buildrootは使用しない。

```sh
docker build --platform linux/arm64 -f scripts/wpe-source-probe.Dockerfile \
  -t picdplayer-wpe-source-probe:2.54.0-trixie .
docker volume create picdplayer-wpe221-build
docker run --rm --platform linux/arm64 --network none --memory 6g --cpus 2 \
  -v picdplayer-wpe221-build:/work picdplayer-wpe-source-probe:2.54.0-trixie configure
docker run --rm --platform linux/arm64 --network none --memory 6g --cpus 2 \
  -v picdplayer-wpe221-build:/work picdplayer-wpe-source-probe:2.54.0-trixie build
docker run --rm --platform linux/arm64 --network none --memory 6g --cpus 2 \
  -v picdplayer-wpe221-build:/work picdplayer-wpe-source-probe:2.54.0-trixie stage
docker run --rm --platform linux/arm64 --network none --memory 6g --cpus 2 \
  -v picdplayer-wpe221-build:/work -v "$PWD:/src:ro" \
  picdplayer-wpe-source-probe:2.54.0-trixie verify
```

volume内の`/work/logs/`にpackage版・toolchain・configure/build/install logを保持する。
stageはvolume内の`/work/stage/opt/picdplayer-wpe/2.54.0/`で、ホストの`/usr`へinstallしない。
並列数は2。失敗後もvolumeを保持し、configure成功・build成功・runtime成功を分けて記録する。
DRMとbubblewrap sandboxを有効化し、legacy API・Wayland/headless・動画・Web Audio・WebCodecs・GStreamer・音声合成・
spellcheck・hyphenation・documentation/introspection・WebDriver・libbacktraceを無効化する。
この省略構成はCustom UI全般の互換性を保証しない。base tagとapt package版は現時点では
取得時点依存であり、package一覧とimage IDを記録して評価する。製品供給の完全固定は後続課題。
2.54.0はVIDEO=OFFでmedia bindingの条件guard不足によるcompile failureを観測したため、
`scripts/wpe-source-video-disabled.patch`をfuzz 0で適用する。公式source hashとこの追加patchを
区別して追跡し、無改変upstreamのbuild成功とは扱わない。
加えてsandbox用libseccompが最終linkから欠落したため、
`scripts/wpe-source-sandbox-link.patch`でWPE/sandbox有効時のtargetへ明示linkする。
sandboxをOFFにして回避しない。`verify`は専用prefixを使って既存launcherをcompile/linkし、
`--version`とlauncher/Web/Network/GPU processの`ldd -r`を検査する。DRMには接続しない。

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
| `ENABLE_PARANOIA` | `pkg-config libcdio-paranoia-dev` | 実験・検証用CD-DA reader（公式配布には採用しない） |
| `ENABLE_METADATA` | `pkg-config libdiscid-dev libcurl4-openssl-dev nlohmann-json3-dev` | MusicBrainz・Cover Art参照 |
| `ENABLE_API` | `pkg-config libwebsockets-dev nlohmann-json3-dev` | HTTP操作・WebSocket状態配信 |
| `INSTALL_SYSTEMD_UNIT` | systemd運用環境 | daemon service unitのインストール |
| `INSTALL_SYSTEMD_KIOSK_UNIT` | 実行先にCage・Chromium | kiosk unit・wrapperのインストール |

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

API有効buildでは`ui/default/`のHTML/CSS/JSからbuilt-in UIを生成し、同じファイルを
`share/picdplayer/ui/default/`へinstallする。UI assetの生成にはNode.jsは不要。試験ではNode.jsがあると標準UIとwrapperの2件を追加し、
Python 3があるとCLI・daemon試験（API有効時はUI起動試験も）を追加する。
全試験の実行にはNode.jsとPython 3の両方を用意する。
profile試験は実`Ninja Multi-Config`の拒否を確認するためNinjaも必要である。
ユーザー編集版の指定と復旧方法は[Custom UI](custom-ui.md)を参照する。
paranoiaを使用する場合は `ENABLE_PARANOIA=ON` でconfigureし、`--cdda-reader paranoia` を指定する。
この設定は実験・検証用で、現時点の公式releaseには採用しない。対応containerのDebian
`libcdio-paranoia` packageはGPL-3-or-laterとして配布されているため、有効化したbinaryを
再配布する前に対象package版の条件、ライセンス互換性、source提供要件を確認する。configure時にも
同じ警告を表示する。プロジェクト本体と依存の正式なライセンス方針は[#66](https://github.com/udonchan/picdplayer/issues/66)で監査中である。

インストールと起動設定は[systemd手順](../manual/systemd.md)、操作例は[操作・診断](../manual/operations.md)を参照する。
