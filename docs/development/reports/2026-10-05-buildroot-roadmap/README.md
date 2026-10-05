# Buildroot適合性評価と後続作業の整理（#78）

調査日: 2026-10-05。根拠は[公式構成調査（#75）](https://github.com/udonchan/picdplayer/pull/195)、
[採用OSS比較（#76）](https://github.com/udonchan/picdplayer/pull/196)、
[現行runtime要件（#77）](https://github.com/udonchan/picdplayer/pull/197)。
これらは調査PRであり、Buildroot imageのbuildやPi実機起動の証拠ではない。
本評価は[#46](https://github.com/udonchan/picdplayer/issues/46)の最終方式選定を代行しない。

## 暫定評価

Buildrootは、PiCDPlayer専用の再現可能なimageを作る**有力候補**である。
公式Pi 3 arm64設定からSD-card imageを生成でき、`BR2_EXTERNAL`で製品固有設定を分離できる。
`cmake-package`と現行CMake install規則は接続できる見込みがある。
Batoceraの外部treeと版固定は参考になるが、多機種向けfork・独自wrapperは不要。
OpenMikoの単一製品向けimageと開発containerの分離は参考になるが、古いBuildrootへ
標準packageをコピーして置換する方式を踏襲する理由はない。

ただし、**採用判断に必要な実機・build証拠は不足**している。
公式package treeに`libdiscid`を確認できず、WPE直接DRMのPoCが使用した2.54系と
調査時点のBuildroot `wpewebkit` 2.50.5 recipeには差がある。
Pi 3のCD-ROM/ALSA/CEC/DRM、systemd、永続領域もBuildroot imageでは未検証。
現行Raspberry Pi OS + `.deb`経路は実機開発に使えており、Buildroot導入のために
直ちに置き換える必要はない。開発反復の所要時間もまだ比較していない。

| 方式候補 | 利点 | 判断前に残る点 |
|---|---|---|
| Buildroot製品image + 現行`.deb`開発 | 製品rootfsを入力から構成し、通常開発を維持できる見込み。 | Pi 3周辺機器、libdiscid、WPE供給、build時間、更新・永続領域。 |
| Raspberry Pi OSベースのimage化 | 現行の実機実績とpackage供給を利用しやすい。 | imageの再現性、不要package、配布物inventory、更新・復旧の設計。 |
| Yocto等の別build system | 選択肢として残せる。 | 現時点のPiCDPlayer規模で採用する積極的根拠は未調査。必要になれば#46で比較範囲を定める。 |

上表は方式の確定ではなく、[#46](https://github.com/udonchan/picdplayer/issues/46)へ渡す比較軸である。
Buildrootを`#47`のhard dependencyにしない。

## Buildrootを選ぶ場合の最小構成候補

製品固有のpackage、Pi用defconfig、board設定を`BR2_EXTERNAL`へ置く案を第一候補とする。
Buildroot本体は変更を積み重ねず、revisionを固定する。submoduleは参照更新を
reviewしやすい候補だが、固定archive等でも再現性を実現でき、まだ決めない。
現行CMakeのinstall規則をpackage recipeから使用し、binaryをrootfs overlayで
手作業コピーしない方向を検証する。rootfs overlay/post-build/post-imageは
必要な固定設定・image組立てに限定して検討する。

この構成は実装前の仮説であり、最終directory名、Buildroot版、kernel、partition、
サービス起動、WPE/Chromium選択、`.deb`のimage内利用を決めるものではない。
開発用`.deb`の完成（#43）を最終imageの必須入力としない。

## 作成した検証Issueと実施順

1. [#198 Pi 3の最小Buildroot image・周辺機器確認](https://github.com/udonchan/picdplayer/issues/198):
   公式Pi設定からのbuildとCD-ROM/ALSA/CEC/DRMの実機リスクを明らかにする。
2. [#199 PiCDPlayer packageとlibdiscid供給](https://github.com/udonchan/picdplayer/issues/199):
   現行CMake install、metadata/API依存、unitのBuildroot供給を検証する。
3. [#200 WPEPlatform直接DRM供給](https://github.com/udonchan/picdplayer/issues/200):
   2.54系と公式recipeの差、Pi向けgraphics/input依存を検証する。

3件は独立した検証として並行できる。#198のimageを#200のPi実機試験で再利用できるが、
試験buildや依存の比較自体を妨げないためhard dependencyにはしない。
これらの結果を[#46](https://github.com/udonchan/picdplayer/issues/46)の方式選定へ渡す。
採用方式が決まった後、[#47](https://github.com/udonchan/picdplayer/issues/47)が製品imageの
build・初回起動・更新/復旧・実機確認を所有する。Buildroot専用のintegration skeletonや
PiCDPlayer package実装のIssue化は、検証結果と#46の選定を見て#47から分割する。
現段階で未確定の実装を子Issueとして量産しない。

## 既存Issueとの境界

- [#21](https://github.com/udonchan/picdplayer/issues/21)はrelease/image全体、
  [#45](https://github.com/udonchan/picdplayer/issues/45)はrelease artifactを所有する。
- [#20](https://github.com/udonchan/picdplayer/issues/20)はread-only rootと書込先、
  #46は採用可否を含むfilesystem・更新要件を所有する。
- [#66–#69](https://github.com/udonchan/picdplayer/issues/66)はproject licenseから
  image complianceまでを段階的に所有する。#198–#200はライセンス/SBOM成果を代替しない。
- [#193](https://github.com/udonchan/picdplayer/issues/193)はRaspberry Pi OSでの
  opt-in WPE開発runtime。#200はBuildroot image向け供給だけを扱う。

## 未決定事項と次の判断

Buildroot採用の可否は#198–#200の結果だけで決めず、#46でPiCDPlayerの更新・復旧、
永続領域、配布物の追跡、build資源、保守負担をRaspberry Pi OSベース案と比較する。
通常開発でBuildroot SDKを使うかも未決定であり、まず現行Mac＋Docker＋`.deb`を維持する。
本報告ではBuildrootやWPEの実装、image生成、Pi実機変更を行っていない。
