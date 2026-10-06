# BuildrootでのWPEPlatform 2.54直接DRM供給試験（#200）

検証日: 2026-10-06。対象Buildrootは#198/#199と同じ公式revision
`c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b`。
Pi 3 Model B向け`raspberrypi3_64_defconfig`の隔離Buildroot出力を使用した。
現行のRaspberry Pi OS + `.deb`経路やPiのサービスは変更していない。

## 調査と試作の境界

このrevisionの`wpewebkit` recipeは2.50.5を指定し、`wpebackend-fdo`を選択する。
`cog`は0.18.5である。#191のPi実機PoCはWPE WebKit/WPEPlatform 2.54.0の
組込みDRM/KMS実装を使い、Cage/Cogなしで表示した。従って旧recipeの単純な有効化は
#191と同じruntimeの供給検証にならない。

[WPE公式の2.54説明](https://wpewebkit.org/blog/2026-09-16-wpewebkit-2.54.html)では、
WPEPlatformを新しいAPIとし、DRM/KMS・Wayland・headlessを組込みで扱う。
2.54.0の[公式source](https://wpewebkit.org/releases/wpewebkit-2.54.0.tar.xz)
（SHA-256 `efa9bcc3cb891c2d88f50eec710d9ccee71cbdf1040420361eb98c17355eb452`）を
[公式checksum](https://wpewebkit.org/releases/wpewebkit-2.54.0.tar.xz.sums)と照合した。
同tarballの`Source/cmake/OptionsWPE.cmake`を確認すると、DRMはGBM/libdrm、
libinput 1.19以上とudevを要求する。

試作の[recipeファイル](trial-package/)を保管した。
同revisionの`package/wpewebkit/`へ3ファイルを置き換え、旧版向けで2.54には
既に反映された`0001-CMake-4.4-...patch`を外した条件である。
これは採用済みの製品recipeではない。2.54のhash、WPEPlatform DRM選択、
旧API/Wayland/headless無効化、依存を追加した。WPE内のvideo/web audioは
標準Playerでは使用せず、CD音声はPiCDPlayer daemonが担当するため、
本試作ではGStreamer、hyphenation、spellcheckを無効にした。
Custom UIの将来のmedia要件をこの試作だけで確定しない。

## 隔離した設定

既存の#198/#199出力を別のDocker管理volumeへコピーし、元の出力は保持した。
Buildrootの`O=`を元と同じ`/work/output`に固定し、次を有効にした。

```text
BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_EUDEV=y
BR2_PACKAGE_EUDEV=y
BR2_PACKAGE_LIBINPUT=y
BR2_PACKAGE_LIBDRM=y
BR2_PACKAGE_MESA3D=y
BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_VC4=y
BR2_PACKAGE_MESA3D_GBM=y
BR2_PACKAGE_MESA3D_OPENGL_EGL=y
BR2_PACKAGE_MESA3D_OPENGL_ES=y
BR2_PACKAGE_WPEWEBKIT=y
BR2_JLEVEL=2
```

`olddefconfig`後に上記が選択され、`BR2_PACKAGE_HAS_LIBGBM`、
`BR2_PACKAGE_HAS_LIBEGL`、`BR2_PACKAGE_HAS_LIBGLES`、
`BR2_PACKAGE_HAS_LIBUDEV`も有効と確認した。Pi 3向けのVC4を選び、
Pi 4/5向けV3Dを追加しなかった。公式最小defconfigはBusyBox initであり、
この試作では製品サービスやlauncherをimageへ組み込んでいない。

## ビルド観測

- 元の出力を`/out`へコピーした最初の試行は、既存host toolchainの
  `/work/output`参照と新しい`/out`が混在した。結果を採用せず、
  元と同じ`/work/output`へ独立volumeをマウントしてやり直した。
- Mesa 26.1.8、libgbm、libinput 1.31.3等の依存がaarch64向けにbuild/installされた。
- 最初の2.54試行では旧版用CMake patchが既にupstreamへ反映済みで適用に失敗した。
  patchを外すとconfigureへ進んだ。
- configureで`libxkbcommon`が必須と判明し、依存へ追加した。
  任意のhyphenation、spellcheck、GStreamerは上記の方針で明示的に無効化した。
- WPE WebKit 2.54.0のCMake configureは**成功**した。aarch64向け本体buildの
  最終結果は追記する。
- 最初の本体buildは`BR2_JLEVEL=0`により内部Ninjaが17並列となり、
  4 CPU/6GiBのcontainerでメモリ上限に張り付いた。結果を採用せず停止し、
  `BR2_JLEVEL=4`に設定してコンパイル済み成果から再開したが、
  JavaScriptCoreの各compilerが約1.3〜1.9GiBを使い、4並列では6GiBの上限に近かった。
  完走を優先して`BR2_JLEVEL=2`へ下げ、同じ隔離出力から再開した。

## 未検証・製品化前の判断

本試作の成功は、Pi実機での画面・CEC・音声との並行動作やcursor制御を証明しない。
Buildroot imageは再生成しておらず、WPEPlatform launcher、font/cursor資産、
D-Bus、sandbox、user/group、systemdとの統合も未完了である。
#193のRaspberry Pi OS開発runtimeは維持し、Buildrootをその必須の置換とはしない。
製品imageへ採用する場合は、source/recipe更新の保守、security update、
Pi 3の実機性能、復旧手順、配布ライセンスを#46/#47等で評価する。
