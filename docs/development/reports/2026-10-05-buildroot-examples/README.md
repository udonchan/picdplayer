# Buildroot採用OSSの比較（#76）

調査日: 2026-10-05。対象は[Batocera](https://github.com/batocera-linux/batocera.linux/tree/77a355a5e4d13613e52416926020057e43b82232)
`77a355a5e4d13613e52416926020057e43b82232`（commit日2026-10-05）と
[OpenMiko](https://github.com/openmiko/openmiko/tree/00f72a6a2f701a038b39949fce239d54cd221065)
`00f72a6a2f701a038b39949fce239d54cd221065`（commit日2023-07-17）。
repositoryのファイル・READMEから確認できる事実と、PiCDPlayerへの適用判断を分ける。
どちらのimageもPiCDPlayer機材でbuild・起動しておらず、性能や保守品質は評価しない。

## Batocera: 多機種対応の専用distribution

[README](https://github.com/batocera-linux/batocera.linux/blob/77a355a5e4d13613e52416926020057e43b82232/README.md)は、
USB/SDから起動してPCや小型機をゲーム機にするdistributionを目的としている。
そのためPiCDPlayerと同じく「ユーザーがimageを書き込んで専用機として使う」例になる。

確認できた構成: [`.gitmodules`](https://github.com/batocera-linux/batocera.linux/blob/77a355a5e4d13613e52416926020057e43b82232/.gitmodules)で
Buildrootのforkをsubmoduleとして固定し、[外部tree記述](https://github.com/batocera-linux/batocera.linux/blob/77a355a5e4d13613e52416926020057e43b82232/external.desc)を持つ。
READMEは`board/`をplatform固有設定、`configs/`をarchitecture別build flag、
`package/`をemulatorやシステム機能の配置場所と説明する。
調査revisionの`configs/`には45項目があり、`board/batocera/`にはBroadcom以外の
多数のplatform別directoryがある。[トップレベルMakefile](https://github.com/batocera-linux/batocera.linux/blob/77a355a5e4d13613e52416926020057e43b82232/Makefile)も
独自のbuild wrapperを備える。[GitHub Actionsのchecks](https://github.com/batocera-linux/batocera.linux/blob/77a355a5e4d13613e52416926020057e43b82232/.github/workflows/checks.yml)は
確認した範囲ではPython test/lintを行う。これだけでimage全体のCI検証済みとは言わない。

適用判断（推論）: Buildroot本体のrevisionと製品固有設定を別々に追跡する点、
board/package/imageの責務を分ける点は参考になる。多機種、多数のemulator、独自Makefile、
Buildroot forkを同じ規模で持つ理由は現時点の単一Pi向けPiCDPlayerにはない。
最初は公式Pi defconfigを起点に、必要最小限の製品設定だけを分離する方が単純。
PiCDPlayerにもforkや複数platformが必要かは、実際の不足が分かった時点で判断する。

## OpenMiko: 特定カメラ向けの置換firmware

[README](https://github.com/openmiko/openmiko/blob/00f72a6a2f701a038b39949fce239d54cd221065/README.md)は、
Ingenic T20カメラのstock firmwareを置き換え、標準化したBuildroot toolchainと
Docker開発環境、bootloader/flash互換を目標に挙げる。
単一hardware向けの製品runtimeと開発containerを分ける点でPiCDPlayerの参考になる。

確認できた構成: [Dockerfile](https://github.com/openmiko/openmiko/blob/00f72a6a2f701a038b39949fce239d54cd221065/Dockerfile)は
Ubuntu 16.04上へBuildroot **2016.02**のtarballを取得する。
[`setup_buildroot.sh`](https://github.com/openmiko/openmiko/blob/00f72a6a2f701a038b39949fce239d54cd221065/buildscripts/setup_buildroot.sh)は
一部標準packageを削除し、独自package・patch・defconfigをBuildroot treeへコピーして
`make`する。`br_external_trees/full/`には外部tree用ファイルもあるが、確認した
Dockerfile→setup scriptの主経路がそれを使用することは確認できなかった。
[`.gitmodules`](https://github.com/openmiko/openmiko/blob/00f72a6a2f701a038b39949fce239d54cd221065/.gitmodules)の
submodule対象は映像関連等で、Buildroot本体ではない。
[release workflow](https://github.com/openmiko/openmiko/blob/00f72a6a2f701a038b39949fce239d54cd221065/.github/workflows/release.yml)は
tagからDocker buildし、firmware/rootfs/kernel artifactをGitHub releaseへ出す。
READMEの永続設定はSD上の`/config/overlay`を利用する。

適用判断（推論）: Linux製品runtime、開発container、生成artifactの境界や、
設定の永続領域を別に持つ考え方は参考になる。ただし古いBuildroot版と
stock firmwareのflash互換、Ingenic固有kernel/映像、Buildroot標準packageの置換は
PiCDPlayerの要件ではない。特に標準packageを消してコピーする方式を現行Buildrootへ
そのまま移す根拠はない。より単純な`BR2_EXTERNAL`と公式package機構を先に比較する。
2023年以降の更新を確認できないため、現行WPEPlatformやPi向けの実装手本にはしない。

## 横断比較と#78への入力

| 観点 | Batocera | OpenMiko | PiCDPlayerでの暫定判断 |
|---|---|---|---|
| Buildrootの取得・固定 | forkのsubmodule | Dockerfileが2016.02 tarballを取得 | 固定は必要。submoduleか固定archiveかは未決定。 |
| 製品設定の境界 | 外部tree、board/config/packageを大規模に分離 | 独自設定を主build scriptでBuildroot内へコピー | 本体への直接変更を最初から増やさない候補を優先。 |
| image/CI | 多機種distribution。確認したchecksはPython test/lint | 特定camera firmwareをtagからrelease | Pi向けbootable imageの生成は#47、releaseは#45が所有する。 |
| 開発と製品の分離 | 独自Makefile・Docker wrapper | Docker buildと生成firmware | 現在の`.deb`開発経路を維持しつつimage構築を追加できるか検証。 |

両事例ともディレクトリの見た目だけを採用理由にしない。#75の公式機構、#77の実際の
runtime要件、#46のimage要件と突き合わせて#78で候補構成を判断する。
