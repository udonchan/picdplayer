# Buildroot公式構成・Raspberry Pi image経路の調査（#75）

調査日: 2026-10-05。参照元は[Buildroot公式マニュアル](https://buildroot.org/downloads/manual/manual.html)と
[公式ソース](https://github.com/buildroot/buildroot/tree/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b)。
ソースの参照revisionは`c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b`。
マニュアルは調査日の公開版であり、このrevisionと同一内容であることまでは確認していない。
以下は構成方法の調査で、PiCDPlayerのBuildroot採用・image実装・実機起動を意味しない。

## 製品固有設定の置き場所

[マニュアル9.1–9.2](https://buildroot.org/downloads/manual/manual.html#customize)は、
`board/`、`configs/`、`package/`等をBuildroot本体内に置く方法と、
`BR2_EXTERNAL`で外部に置く方法の**両方を有効**としている。
`BR2_EXTERNAL`はpackage recipe、board設定、defconfig、overlay等を本体から分離できる。
外部treeの必須ファイルは`external.desc`、`external.mk`、`Config.in`。
`configs/`内のdefconfigは`make list-defconfigs`と`make <name>_defconfig`から利用できる。
設定やscriptからは`BR2_EXTERNAL_<NAME>_PATH`で外部treeを参照できる。
指定した外部treeはoutput側に記録され、毎回`BR2_EXTERNAL`を渡す必要はない。

PiCDPlayer固有のrecipeやboard設定を本体更新から分離する点で`BR2_EXTERNAL`は有力。
一方、本体内管理も公式に認められており、最小の実験では単純な場合がある。
Buildroot本体をgit submoduleで取得・固定する案は**PiCDPlayer側のversion管理方法**であり、
公式が必須とする構成ではない。submodule、固定tarball等の選択は#78に残す。

## 仕組みと適用候補

| 仕組み | 公式の役割・制約 | PiCDPlayerでの候補・未決定点 |
|---|---|---|
| `configs/*_defconfig` | `make savedefconfig`で既定値を除いたBuildroot設定を保存し、`make <name>_defconfig`で復元する。[9.3](https://buildroot.org/downloads/manual/manual.html#customize-store-buildroot-config) | Pi 3 arm64を起点にする候補。対象Pi、toolchain、kernel版は未決定。 |
| custom package | `Config.in`と`.mk`で依存とbuild/installを宣言する。CMake projectには`cmake-package`を使える。[18.8](https://buildroot.org/downloads/manual/manual.html#_infrastructure_for_cmake_based_packages) | 既存CMake install規則を利用できる可能性がある。開発用`.deb`をimageへ入れる必要はない。実際のrecipe・依存対応は#77以降で検証。 |
| board設定 | kernel、boot設定、patch、overlay、image script等の入力をまとめる。[9.1](https://buildroot.org/downloads/manual/manual.html#customize-dir-structure) | Pi公式board例を基礎資料とする。kernelやboot設定はここで確定しない。 |
| rootfs overlay | package導入後、targetへ静的ファイルをコピーする。複数指定可能。merged `/usr`等では置けないpathがある。[9.5](https://buildroot.org/downloads/manual/manual.html#rootfs-custom) | 固定設定ファイルの候補。PiCDPlayer本体のbinaryを手作業でコピーする用途にはしない方向で検討。 |
| post-build | package build後、filesystem image化前にtargetの内容を修正する。package自体の誤りの回避策として多用しない。[9.5](https://buildroot.org/downloads/manual/manual.html#rootfs-custom) | 生成後に必要な最小の設定変更のみ候補。具体的なservice/権限設定は未決定。 |
| post-image | rootfs・kernel等のimage生成後に実行する。[9.7](https://buildroot.org/downloads/manual/manual.html#rootfs-custom) | SD-card imageの組み立てが必要なら候補。partition構成と更新方式は#46の領域。 |

Buildrootはlocal sourceを使う`<pkg>_OVERRIDE_SRCDIR`と`make <pkg>-rebuild all`も
[公式に提供](https://buildroot.org/downloads/manual/manual.html#_using_buildroot_during_development)。
これは後の開発反復の選択肢だが、既存Mac＋Docker＋`.deb`経路を置換する決定ではない。
Buildrootの設定変更時には必要な再buildを自動判定しない場合があり、package削除等では
[full rebuildが必要](https://buildroot.org/downloads/manual/manual.html#full-rebuild)。

## 公式Raspberry Pi 3 arm64例のimage経路

[公式`raspberrypi3_64_defconfig`](https://github.com/buildroot/buildroot/blob/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/configs/raspberrypi3_64_defconfig)は
arm64、外部glibc toolchain、Pi kernel/DTB/firmware、ext4 rootfs、post-build/post-image、
host `genimage`等を選ぶ。`board/raspberrypi3-64`は
[`board/raspberrypi`へのsymlink](https://github.com/buildroot/buildroot/blob/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/board/raspberrypi3-64)。
したがってPi 3固有のdefconfigから共通board scriptを参照する。

[`post-build.sh`](https://github.com/buildroot/buildroot/blob/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/board/raspberrypi/post-build.sh)は
init方式に応じHDMI consoleを設定する。
[`post-image.sh`](https://github.com/buildroot/buildroot/blob/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/board/raspberrypi/post-image.sh)は
`genimage`を呼び、[`genimage.cfg.in`](https://github.com/buildroot/buildroot/blob/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/board/raspberrypi/genimage.cfg.in)が
FAT boot領域とext4 rootfs領域を持つ`sdcard.img`を定義する。
[公式board readme](https://github.com/buildroot/buildroot/blob/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/board/raspberrypi/readme.txt)も
`make raspberrypi3_64_defconfig`→`make`→`output/images/sdcard.img`を案内する。
公式例の容量やpartitionをPiCDPlayerの完成仕様として採用しない。

## #78へ渡す未決定事項

- `BR2_EXTERNAL`と本体内管理のどちらがPiCDPlayerの規模に合うか。#76の他事例と比較する。
- Buildroot版固定をsubmodule、固定source archive等のどの方法で行うか。
- Pi 3公式例のkernel・Mesa・DRM/CEC/CD-ROM/ALSA要件との差。実装確認は#77が所有する。
- WPEPlatform 2.54以降をどう供給するか。調査revisionの
  [公式`wpewebkit` recipe](https://github.com/buildroot/buildroot/blob/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/package/wpewebkit/wpewebkit.mk)は
  2.50.5と従来の`wpebackend-fdo`を使用するため、現行WPE直接DRM PoCと同じ構成とは言えない。
- bootable imageのfilesystem、永続領域、更新・復旧方式は#46の判断に従う。

本調査ではBuildroot build、Piへの書き込み、WPE recipe更新を実施していない。
