# BuildrootでのPiCDPlayer package供給検証（#199）

検証日: 2026-10-06。Buildrootは[#198の検証](../2026-10-06-buildroot-pi3-probe/README.md)と
同じ公式revision `c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b`。
PiCDPlayer sourceは`master`の`173e088`に検証記録だけを加えたworktree。
この調査revisionの`package/`には`libdiscid`が見当たらず、PiCDPlayerの標準
`ENABLE_METADATA=ON`には別途供給方法が必要になる。

## 入力と試作

[MetaBrainz公式libdiscid 0.7.0 source](https://data.metabrainz.org/pub/musicbrainz/libdiscid/libdiscid-0.7.0.tar.gz)
（SHA-256 `c230ed462c5ed7d7403ceb6984c57c8d05de42386d796bcd893636c0b0ba222f`）を使用。
公式sourceはCMake buildを持ち、`COPYING`はLGPL-2.1-or-laterの本文である。
Buildrootの`cmake-package`を使う最小の[試作BR2_EXTERNAL](br2-external/)を置いた。
これは製品用packageの採用決定ではない。

試作時は#198のDocker volume内のtoolchainを再利用し、PiCDPlayer checkoutを
containerの`/src`、試作treeを`/probe/external`にread-only mountした。
Buildrootの`olddefconfig`で次を有効にし、`make libdiscid`、依存package、
`make picdplayer`の順にbuildした。

```text
BR2_PACKAGE_ALSA_LIB=y
BR2_PACKAGE_JSON_FOR_MODERN_CPP=y
BR2_PACKAGE_LIBCURL=y
BR2_PACKAGE_OPENSSL=y
BR2_PACKAGE_LIBCURL_OPENSSL=y
BR2_PACKAGE_LIBWEBSOCKETS=y
BR2_PACKAGE_CA_CERTIFICATES=y
BR2_PACKAGE_LIBDISCID=y
BR2_PACKAGE_PICDPLAYER=y
```

選択後、#198と同じDocker imageとoutput volumeを使い、次のpackage targetを実行した。
`/src`はPiCDPlayer checkoutのread-only mount、`/probe/external`は本報告の
`br2-external`のread-only mountである。

```sh
make O=/work/output BR2_EXTERNAL=/probe/external libdiscid
make O=/work/output BR2_EXTERNAL=/probe/external \
  alsa-lib json-for-modern-cpp libcurl libwebsockets ca-certificates
make O=/work/output BR2_EXTERNAL=/probe/external picdplayer
```

この変更は#198の基準image生成後に行った。追加前の`.config`と`sdcard.img`は
別に保全しており、後続のpackage build成功を基準imageの機能と混同しない。

## 検証結果

- 試作`BR2_EXTERNAL`を公式Pi 3 arm64 defconfigと共に読み込み、
  `BR2_PACKAGE_LIBDISCID=y`が`olddefconfig`後も有効: **確認済み**。
- #198で取得したBuildroot aarch64 toolchainを使い、公式libdiscid 0.7.0を
  CMake cross-buildし、`DESTDIR`にinstall: **成功**。
- install結果: `/usr/lib/libdiscid.so.0.7.0`（aarch64 ELF shared library）、
  `/usr/lib/pkgconfig/libdiscid.pc`、`/usr/include/discid/discid.h`。
- 試作`libdiscid` recipeをBuildrootの`make libdiscid`からbuildし、stagingと
  targetの両方へinstall: **成功**。
- `alsa-lib`、`libcurl`＋OpenSSL、`libwebsockets`、`json-for-modern-cpp`、
  `ca-certificates`をBuildroot package targetからbuild: **成功**。
- `ENABLE_METADATA=ON`、`ENABLE_API=ON`、`ENABLE_PARANOIA=OFF`で、現行CMake
  install規則をBuildroot cross toolchainと依存へ直接適用: **成功**。
- 試作`picdplayer` recipeをBuildrootの`make picdplayer`からbuild・target install:
  **成功**。target treeには`/usr/bin/cdplayerd`、標準UI資産、
  `/usr/lib/systemd/system/picdplayer.service`が入った。
- 生成daemonはaarch64 ELFで、`libasound.so.2`、`libcurl.so.4`、
  `libdiscid.so.0`、`libwebsockets.so.21`等へ動的リンクすることを確認した。
- これらを含むimage再生成とPi起動、実CDのDisc ID生成・音声・CEC・UI動作:
  **未確認**。#198の`sdcard.img`はpackage追加前の基準imageとして保持する。

## 製品化前の不足

試作`picdplayer` recipeは`SITE_METHOD=local`で開発checkoutを使用し、
`LICENSE=UNKNOWN`としている。source revision固定とライセンス方針・artifact追跡は
#66–#69と整合して決める必要がある。今回の試作をそのまま正式配布用にしない。

公式Pi 3最小imageはBusyBox initであり、installしたsystemd unitは**起動されない**。
unitは`User=picdplayer`、`video cdrom audio` group、`CacheDirectory`、
`StateDirectory`、`/usr/bin/cdplayerd`を要求する。専用user/groupの生成、
systemdの選択、書込先と起動順序は#46/#47で決め、試験する。
WPE/Chromiumのkiosk unitはこの試作でinstallしていない。#200のWPE供給判断を待つ。
metadata lookupが失敗してもCD再生を止めない現行fallbackは維持対象であり、
package build成功からPiでのnetworkやDisc ID動作を推測しない。
