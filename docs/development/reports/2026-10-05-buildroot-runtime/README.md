# PiCDPlayer runtime要件とBuildroot適合性の棚卸し（#77）

調査日: 2026-10-05。PiCDPlayerの参照revisionは本報告を追加する直前の`master`
`173e088`。Buildrootのpackage定義確認は公式`master`
[`c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b`](https://github.com/buildroot/buildroot/tree/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b)。
この表は**コードから分かる要件**と**Buildrootにrecipeがあること**を区別する。
package定義の存在はPi 3でのbuild、device access、実画面・実音声の成立を保証しない。
実機の現在の検証範囲は[検証状況](../../verification.md)を参照する。

## build・配布の境界

現行[`CMakeLists.txt`](../../../../CMakeLists.txt)はC++20の`cdplayerd`を
ALSA/Threadsでbuildする。標準[`build-container.sh`](../../../../scripts/build-container.sh)は
Debian Trixie arm64のDocker内で`ENABLE_METADATA=ON`、`ENABLE_API=ON`、
`ENABLE_PARANOIA=OFF`、daemon/kiosk unitのinstallを指定する。
同一のCMake `install()`規則からstageと開発用`.deb`を作り、
[`deploy.sh`](../../../../scripts/deploy.sh)でPiへ導入する。
Buildrootの[`cmake-package`](https://buildroot.org/downloads/manual/manual.html#_infrastructure_for_cmake_based_packages)で
同じinstall規則を使う可能性はあるが、recipe・依存・service pathを実際にbuildしていない。
開発用`.deb`の完成を最終imageの必須入力としない。

| 現行機能・根拠 | 必要なruntime/build条件 | Buildroot側の候補と状態 |
|---|---|---|
| 直接CD-DA読取・TOC・drive能力。`src/cdda_reader.cpp`、`src/optical_drive.cpp` | 通常必須。Linux CD-ROM ioctl、`/dev/sr0`、USB optical drive、必要なkernel moduleとdevice権限。 | Pi向けkernel/udevの組合せ、USB drive認識・CDROM ioctlを未検証。recipeの有無だけでは判断不可。 |
| PCM音声。`src/alsa_output.cpp`、`picdplayer.service.in` | 通常必須。ALSA `plughw:CARD=vc4hdmi,DEV=0`、`audio`権限、HDMI audio driver。 | [`alsa-lib`](https://github.com/buildroot/buildroot/tree/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/package/alsa-lib)は存在。Pi側のcard名、audio routing、underrun復旧は未検証。 |
| CEC。`src/cec_device.cpp`、daemon unit | 通常のリモコン操作に必要。`/dev/cec0`、Linux CEC ioctl、`video`権限、HDMI physical address。 | Pi kernel/device nodeと実TVのCEC登録・入力を要試験。deviceが遅れて現れる場合も現行daemonが再確認する。 |
| metadata/Disc ID。CMakeの`ENABLE_METADATA`、`src/musicbrainz_disc_id.cpp` | 現行標準buildでは有効。libdiscid、libcurl、nlohmann/json、HTTPS接続。失敗時も再生は継続。 | [`libcurl`](https://github.com/buildroot/buildroot/tree/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/package/libcurl)と[`json-for-modern-cpp`](https://github.com/buildroot/buildroot/tree/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/package/json-for-modern-cpp)は存在。調査revisionの`package/` treeに`libdiscid`/`musicbrainz`は見当たらず、独自recipe等の要否を確認する。 |
| local HTTP/API/WS。CMakeの`ENABLE_API`、`src/api_server.cpp` | 現行標準buildでは有効。libwebsockets、nlohmann/json、loopback port 8080。 | [`libwebsockets`](https://github.com/buildroot/buildroot/tree/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/package/libwebsockets)は存在。設定・socket動作・UIとのWS再接続を未検証。 |
| metadata通信。`src/http_client.cpp`、cache | 任意の外部Enrichment。HTTPS、DNS、CA certificate、妥当な時計、network、書込可能なcache。HTTP timeoutはbounded。 | network service、CA bundle、時刻同期、offline復帰は構成未決定。API/UIがnetworkなしでも再生できることを実機で確認する。 |
| Read Policy保存、metadata/artwork cache。daemon unit、`src/read_policy_store.cpp`、`src/metadata_cache.cpp` | cacheは`/var/cache/picdplayer`、設定は指定時に`/var/lib/picdplayer`。user所有権と永続領域が必要。 | rootfsをread-onlyにする場合の分離、容量上限、電源断後の保存は#20/#46と整合して決定する。 |
| daemon常駐。`systemd/picdplayer.service.in` | 現行運用ではsystemd、専用`picdplayer` user、`video cdrom audio` groups、journal、`CacheDirectory`/`StateDirectory`。 | [`systemd`](https://github.com/buildroot/buildroot/tree/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/package/systemd)は選択可能。user/group、device権限、unit install path、boot順序は未検証。 |
| 現行表示。`picdplayer-kiosk.service.in`、`picdplayer-kiosk.sh.in` | Cage+Chromium、Wayland、PAM/login・TTY1・seat、`video render input`、font、API待機。daemonとは別service。 | 現行kioskをそのまま移すかは未決定。Buildroot imageへのWPE採用判断とは分ける。fonts、入力とDRM権限を要検証。 |
| WPE直接DRMのPoC。#191/#193 | **未採用の候補**。WPEPlatform 2.54、DRM/KMS・GBM・EGL・libinput、cursor theme、WebKit sandbox条件。 | 調査revisionの[`wpewebkit` recipe](https://github.com/buildroot/buildroot/blob/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/package/wpewebkit/wpewebkit.mk)は2.50.5と従来の`wpebackend-fdo`。2.54直接DRMの供給・buildは追加検証が必要。 |
| `ENABLE_PARANOIA` | 実験用optional。通常配布ではOFF。 | [`libcdio`](https://github.com/buildroot/buildroot/tree/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/package/libcdio)の存在は確認したが、paranoia有効の採用・再配布判断は#66のlicense検討に従う。 |

上表で「通常必須」は現行の標準Playerを同等機能で運転する場合の意味であり、
optionalのmetadataが停止してもCD再生をブロックしない設計と区別する。
Buildrootの`Config.in`が存在するpackageについてもtargetへ何を入れるかは未決定。

## 現行開発経路との併存

PiCDPlayerの通常の変更検証はMac→Linux/aarch64 Docker→CTest→`.deb`→Piの短い経路である。
Buildroot imageを追加しても、この経路を直ちに捨てる理由はない。
Buildroot公式の[`OVERRIDE_SRCDIR`](https://buildroot.org/downloads/manual/manual.html#_using_buildroot_during_development)は
local sourceから特定packageを再buildする選択肢だが、PiCDPlayerでの所要時間は未測定。
最終imageの生成にはLinux build host、source/依存のversion固定、容量と時間を要する。
Macで使うならLinux container/VMの準備を要するか検証し、既存Docker imageと混同しない。
設定変更時の再build範囲をBuildrootが常に自動判定するわけではないため、
[full rebuild条件](https://buildroot.org/downloads/manual/manual.html#full-rebuild)を扱う手順が必要。

## #78へ渡す検証項目

1. Pi 3 arm64の最小imageがCD drive、ALSA HDMI、CEC、DRM/KMSを実際に認識するか。
   読取・音声・TV操作は必ずPi実機で確認する。
2. `libdiscid`のpackage供給、PiCDPlayer CMake install、systemd unit/user/groupsを
   Buildroot側で成立させる方法。service設定を`.deb`のmaintainer scriptへ依存させない。
3. WPEPlatform 2.54以降のrecipeと依存、Pi 3のMesa/DRM互換、WebKitの更新方法。
   #191のsid rootfs実験を製品imageの構成と同一視しない。
4. CA/時計/network、metadata/画像cache、Read Policy設定の永続領域、電源断時の扱い。
5. 現行`.deb`開発とimage buildの所要時間・資源を測り、CIでどこまで検証するか。
   #45/#46/#47のrelease・image責務と重複させない。

本調査ではBuildroot build・image書込・実機変更を行っていない。
