# Buildroot WPE供給の再開差分調査

## 現在の位置付け: v2.0以降へ延期

このreport作成後の2026-10-22の利用者指示により、v1.0の検討はRaspberry Pi OSベースへ限定した。
Buildroot等の代替方式、ここで提案したrecipe修正仕様・追加probe/buildはv2.0以降へ延期する。
同日のIssue整理で#198/#199/#200/#69へ延期理由・再開条件を追記し、Pendingラベルを付与した。
正式供給の#220全体は延期せず、v1.0のDebian系供給条件を維持する。
以下の「次の小単位」「実行カード」は当時の保存資料であり、現在の着手指示ではない。
成果・branch・試作・cacheは保持し、失敗/完成/恒久不採用として扱わない。
現在の作業列は[親計画のv1.0方針](../../appliance-image-plan.md)を参照する。
公式Imager一覧掲載・公開Imager repository運営は検討範囲外で、将来予定にも追加しない。

2026-10-22、[#46の計画](../../appliance-image-plan.md)のH0/H3のうち、
WPE供給の机上検討を先行した記録。[#200](https://github.com/udonchan/picdplayer/issues/200)と
[#220](https://github.com/udonchan/picdplayer/issues/220)へ渡す判断材料であり、
Buildroot採用・recipe実装・build開始・正式供給の承認ではない。

## 1. 当時の結論（保存資料）

先行する価値はあるが、次は最小imageの再buildではなく、**一構成のrecipe修正仕様を固める作業**とする。
公式recipeを選択するだけでは、現行launcherが使うWPEPlatform直接DRMを供給できない。
一方、graphics/input依存には公式packageを再利用できる見込みがあり、全stackの独自供給が必要とまでは言えない。

保存試作は既にWPE 2.54.0へ進んでおり、「ChromiumからWPEへ初めて移植する」再調査ではない。
主な未解決は現行2patch、sandbox、udev provider、実launcher、target installとruntime統合である。
依存build/configureの過去の成功を、本体完走・image統合・Pi適合へ昇格させない。

**今回の実行判断: 長時間buildにはまだ進まない。** source/patch/feature/provider/layoutの
一構成分の仕様と資源・成果保存条件を確定した後、別承認でbounded buildを選ぶ。
これはBuildroot不採用判断でも、Raspberry Pi OS案の採用決定でもない。

## 2. 入力と到達範囲

| 入力 | 固定参照 / 今回確認した範囲 |
|---|---|
| 現PiCDPlayer | `refs/remotes/origin/master` = `05a1a3e8cadc851e2ccb5f664586a55c431cab68`を再取得。旧glitter-lycheeの実装は基準にしない |
| 保存WPE試作 | `research/200-buildroot-wpe`の固定head `a2c669983f8a86cc8370cc434df6f14fa6dba7b8`。[report][T-report]と同commitのtrial-package [Config.in][T-config]/[.mk][T-mk]/[.hash][T-hash]を参照 |
| 過去のBuildroot | `c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b`。reportに加え公式の当該[wpewebkit.mk][B-old]を再取得 |
| 安定版の比較基準 | tag `2026.08` = `d5180309b1b66ef3b8eaccca70ad69be8e0729a1`。[WPE recipe][B-stable]、Mesa/libinputの版を確認。製品用revision採用ではない |
| upstream現況 | 2026-10-22取得のmaster `2efd7262dff4bc43d70d785d44eab12ca1603aed`。[WPE recipe][B-current]、[Config.in][B-config]/[hash][B-hash]、関連package、patchを確認。動くmasterをbuild入力にしない |
| 現行供給 | [Debian recipe][D-recipe]、[build/verify script][D-probe]、[SDK export][D-export]、2patch、[CMake][D-cmake]、[launcher][D-launcher]、systemd/wrapperを参照 |
| GitHub状態 | #200/#220はOpen、保留条件の本文を再取得。PR #256はDraft/Open、head `ec9fe96`のまま。Issue/PRは編集していない |

今回の取得は小規模なrecipe・文書・metadata・checksumのみ。source archive、base image、
toolchainをdownloadしていない。container起動・build・image生成・Pi/SSH/SD操作は実施していない。
旧volume/output/cacheの存在・hash・再利用可能性とhost資源は今回未監査であり、H0全体の完了とはしない。
一部の追加read-only Git照会は権限により実行できなかったため、取得済み固定資料の範囲で判断し、制限を回避していない。

## 3. 三者比較と更新範囲

| 観点 | 公式Buildroot（比較した過去版/2026.08/現master） | 保存試作 `a2c6699` | 現PiCDPlayer Debian `05a1a3e` / 引継ぎ判断 |
|---|---|---|---|
| WPE source | 2.50.5、`wpebackend-fdo`依存 | 2.54.0、WPEPlatform直接DRMへ変更 | 2.54.0。archiveは一致するがpatch/profileは同一でない |
| backend | Config.inはWayland/backend-fdoを選択。libdrm/GBM optionの存在だけでは直接DRM対応を意味しない | WPEPlatform/DRM ON、legacy API/Wayland/headless OFF | 同じ直接DRM方向。旧Cog/backend-fdo構成では現launcherの代替にならない |
| 必須pkg-config | 現launcher用moduleの供給は確認できない | configureは成功、最終生成物は未確認 | `wpe-platform-drm-2.0 >= 2.54`と`wpe-webkit-2.0 >= 2.54`が必須 |
| 動画無効patch | 2.50.5向けrecipeに現行2.54.0用修正はない | 現行のVIDEO guard patchを含まない | [VIDEO guard][D-video]を適用。2.54.0再検証時は必要性/適用を記録 |
| sandbox link patch | 比較した公式recipeから現行2.54.0構成の成立は証明できない | 現行のlibseccomp link patchを含まない | [sandbox link修正][D-sandbox]を適用。sandbox ONでの最終linkまで要検証 |
| sandbox設定 | opt-inでbubblewrap/xdg-dbus-proxy/libseccompを選択可能 | opt-in、default yなし。reportにON指定がなく有効の証拠なし | 明示ON。試作の暗黙OFFを比較基準へ持ち込まない。ON設定だけで実隔離保証としない |
| WebKit multimedia | ON/OFFのKconfigあり | optionを残す一方`USE_GSTREAMER=OFF`を無条件指定 | VIDEO/WEB_AUDIO/WEB_CODECS/GStreamer OFF。daemonのALSA再生とは別。狭い構成へ明示固定する案 |
| graphics | 2026.08/現masterに[Mesa 26.1.8][B-mesa]、libdrm、GBM/EGL/GLES供給がある | Mesa 26.1.8 build済みとの記録。Pi 3用VC4、GBM/EGL/GLESを手動選択 | DebianのMesa版はrecipeだけでは固定されない。同版・同ABIとは言わない |
| input / udev | 2026.08/現masterの[libinputは1.31.3][B-input]、依存はlibevdev/libudev | libinput 1.31.3 build済み。`HAS_LIBUDEV`条件に対し.mkは`eudev`固定 | systemd/eudev等のproviderを統一する修正仕様が必要。両方を無条件に選ばない |
| xkb / legacy libwpe | 現masterの[libwpe packageは1.16.3][B-libwpe]、legacy stackの構成要素 | libxkbcommonの追加でconfigureが前進。直接依存からlegacy stackを外す案 | launcherは新APIを使う。libwpe版の単純更新で解決する問題ではない。最終依存実体は別検査 |
| host/optional feature | toolchain/host GCC/CMakeと多数のoption条件あり | 古い条件分岐が残り、製品向け汎用recipeではない | Debianはapt/compiler/依存版が未完全固定。未指定featureをOFFと解釈しない |
| install | cmake-packageのstaging/target機構を利用可能 | `INSTALL_STAGING=YES`、本体最終installは未確認 | private SDKの`/opt/picdplayer-wpe/2.54.0`同梱とCPackはDebian向け。Buildroot配置へ機械的に転用しない |
| 証拠 | recipeを今回読んだだけで新規buildなし | 依存build/configureまで。launcher/image/Pi未確認 | Debianでの過去build/package/限定Pi記録はあるがBuildroot成立の証拠にしない |

WPE 2.54.0 archiveのSHA-256は両案で
`efa9bcc3cb891c2d88f50eec710d9ccee71cbdf1040420361eb98c17355eb452`。
[公式checksum][W-sums]とも照合した。archive自体を今回取得・hash計算したわけではない。

**2patchの役割を分ける。** VIDEO guardは
`Source/WebCore/bindings/js/JSHTMLMediaElementCustom.cpp`を`ENABLE(VIDEO)`で囲う修正、
sandbox linkは`Source/WebKit/CMakeLists.txt`でWPE+sandbox時に
`Libseccomp::Libseccomp`をWebKitへprivate linkする修正である。
patchがあることは、そのBuildroot toolchainでの適用・compile/link成功を証明しない。
適用時はfuzzなし・失敗を無視しない方針とし、別版を選ぶならupstream取込済みかを再評価する。

現Buildroot masterには[CMake 4.4補修patch][B-cmake-patch]がある。
保存reportは2.54側へ取込済みで旧patch適用が失敗したと記録しているため、
公式2.50.5 packageのpatch directoryを丸ごと2.54へコピーしない。
これは現行2patchとは別の修正であり、patch名が似ていることを理由に混同しない。

### 3.1 feature一致の限界

共通する明示設定はWPEPlatform/DRM ON、legacy API/Wayland/headless OFF等。
試作はSkiaをlittle-endianでON、ATK/API tests/MiniBrowser/WebRTC等をOFF、
AVIF/LCMS/WOFF2/JPEG XL/journald等を条件付き指定する一方、
Debianはこれらの一部を明示しない。逆にDebianはQt APIを明示OFFにしている。
**省略されたoptionの実効値は不明**で、recipe文字列の比較だけで全feature同一とはしない。
次のconfigureでCMakeCache/summaryと依存版を採取し、意図しない自動有効化/無視されたoptionを検出する。

### 3.2 供給方法の候補

| 案 | 評価 / 今回の扱い |
|---|---|
| 公式2.50.5をそのまま使用 | 現launcherのAPI/architectureと不一致。単純採用案としては進めない |
| 保存試作をそのまま再開 | source/依存発見は有用だが2patch・sandbox・provider・installの差が残る。現製品構成の検証には不足 |
| 固定Buildroot上で2.54.0+必要patchの限定recipe差分を作る | 次の検証用候補として推奨。公式graphics/input packageを再利用し、変更範囲を小さくする。実装・採用は未承認 |
| さらに新しいWPEへ追従 | security/保守上の比較候補。API/patch差分が増えるため、今回無断で版を上げない |

BR2_EXTERNAL内に同名packageを置くだけで公式recipeを安全に置換できるとは仮定しない。
固定Buildroot treeへの追跡可能なpatch方式か、競合しない外部package方式かを次の仕様で決める。
元tree/worktree/volumeは保持し、試験用入力・outputを分離する。

## 4. launcherとruntimeを分離して検証する

[現launcher][D-launcher]はC11で、`wpe/drm/wpe-drm.h`、`wpe/wpe-platform.h`、
`wpe/webkit.h`、`glib-unix.h`を使用する。
`wpe_display_drm_new()`と`wpe_display_connect()`、WebKit viewのdisplay指定、
`webkit_web_view_get_wpe_view`、toplevel fullscreen、URI loadとGLib main loop等を使う。
Cog/Cageや旧backend APIを呼ぶlauncherではない。

最初のlink検証はこの一つのsourceと上記2個のtarget `.pc`を使い、host pkg-configや
Debian SDKを混ぜずにcross compileする。full PiCDPlayer CMakeはdaemonのC++/ALSA等も
要求するため、この狭い検証と#199の全体package再検証を区別する。
cross binaryをMacで実行してversion確認をしない。target上のversion・プロセス起動は別段階である。

| 統合項目 | 既知の必要条件 / 仕様で決める点 | hostで得られる証拠 / Piへ残ること |
|---|---|---|
| ELF・配置 | WPE libraries/Web・Network・GPU等のhelper、runtime share資産、launcher。headers/.pcはstaging側と配布側を分ける | AArch64 ELF、interpreter、SONAME/DT_NEEDED、RPATH/RUNPATH、symlinkと配置。各processの実起動は未確認 |
| prefix | 現wrapperは`/opt/picdplayer-wpe/2.54.0/lib`をprivate library pathに使用 | Buildroot `/usr`配置かprivate prefix維持かを先に決め、CMake/wrapperと一致させる。SDKの丸コピーを自動採用しない |
| 描画 | libdrm/GBM/EGL/GLES、Pi 3 VC4 driverとkernel/DTBの整合が必要 | package/config/driver存在まで。DRM master取得、画面/メモリ/温度はPi受入 |
| 入力 | libinput/udev/libxkbcommon、device node/group設定 | provider/依存・rule/modeを静的確認。CEC→daemon→UIは実操作で確認 |
| font / cursor | 日本語glyphを持つfontは必要。fontconfig等の設定/探索先も必要。cursor資産の具体的packageは未決 | 配置・licenseを確認。欠字、TV可読性、cursor実表示はPi受入。Chromium用font/cache設定を流用しない |
| sandbox | bubblewrap、xdg-dbus-proxy、libseccompを明示ON構成へ。実行path/modeを整合 | source/link/target実体まで。user namespace/seccomp等kernel機能、権限、namespace実隔離は実動作で別確認 |
| D-Bus | proxy helperは既知依存。daemon/session busがどの構成で必須かは未確定 | 依存pathと設定を棚卸し。根拠なく全D-Bus stack必須または不要と決めない |
| init / seat | 現unitは非login user、`video render input`、PAM login、tty1/getty排他、cgroup/停止処理を利用 | unit/user/group/PAM/権限を静的確認。systemd採否は#46へ。BusyBoxなら同等の起動/停止設計が追加必要 |
| shell / runtime | wrapperはBash `/dev/tcp`、`/proc/uptime`、GNU timeoutの`--kill-after`等、sleep/rmを使用 | BusyBox shell/timeoutで代用できると仮定しない。runtime/state/cacheの書込み先・容量も確認 |
| fallback | WPE主系でも既定Chromium fallbackが残る | 非同梱のときの製品設定方針を明示。v1対象外のChromium/Cage受入を追加しない |
| network / 音声 | Playerはloopback API、metadata/画像にはCA/DNS/時刻。音声はdaemon ALSA経路 | WEB_AUDIO無効とCD音声を混同しない。API待受だけでPlayer正常表示とせず、実音声/CEC/表示はPiへ |

systemd採用やsandbox ONだけで製品安全性・Imagerカスタマイズ対応は成立しない。
WPE独自の失敗復帰/DRM cleanup全体はV2の#254のまま、今回の供給検証へ無条件に追加しない。

## 5. 次の一構成と実行カード

### 5.1 次の小単位: recipe修正仕様（まだbuildしない）

入力は本書と保存trial/current recipe。成果物は以下を一枚に固定した実行仕様とする。

| 項目 | 提案 / 残判断 |
|---|---|
| 対象 | Pi 3 Model B / aarch64、WPEPlatform直接DRM。製品対応機種の最終決定ではない |
| source | 比較の変数を減らす候補は2.54.0 + 現行2patch。別版なら変更理由とpatch再評価が必要 |
| Buildroot | 旧`c29ff5b...`は再現比較用、2026.08固定SHAは安定版の比較候補。製品revisionではなく検証用の一方を選ぶ。動くmasterは使わない |
| profile | DRM ON、legacy/Wayland/headless/Qt API OFF、WebKit multimedia OFF、sandbox ONを候補とする。その他の実効featureをconfigure結果で記録 |
| provider | VC4/GBM/EGL/GLES、libinput/libxkbcommon、単一udev provider。試作の`eudev`固定を解消する仕様を決める |
| package修正 | source/hash/2patch、Kconfig依存と.mk依存の一致、旧patch重複、矛盾するmultimedia option、prefix/install、target helperを明示する |
| launcher | 現master固定sourceを使い2個のtarget `.pc`でcompile/link。必要なら独立試験targetとし、#199全体の完了にしない |
| 保存・資源 | 新規の隔離outputと永続保存先、host/Docker割当、既存成果の再利用可否、monitor/停止方法を確認。旧cacheを変更/削除しない |

目安上限は机上作業90分、metadata転送100 MiB以内、source archive取得/buildなし、初回一組。
不足があれば未決一覧を残し、recipe大量改変やsource取得へ自動移行しない。
source供給の固定はこの技術検証の条件であり、本体license確定や正式source提供義務の完了ではない。

### 5.2 将来のbounded build（未承認・未実行）

親計画H4の上限案を維持し、ここで無断増額しない。
一構成・一回、壁時計12時間、最大8 vCPU / 16 GiB、compile jobsは明示2、
追加disk 80 GiB、network全転送10 GiB。開始時に空き100 GiB以上を確保する案。
これらは完走予測ではなく停止上限で、旧6 GiB環境での実績とも違う。
toolchain未保管ならその生成・取得を含む総資源を別途見積もり、H1/H4へ二重計上/黙って上乗せしない。

| checkpoint | 成果物 / 合格条件 | 停止・保留条件 |
|---|---|---|
| C0 input確定 | source/hash、Buildroot/config/toolchain、patch、prefix、資源/保存先が識別可能 | 未決ならarchive取得・buildを開始しない |
| C1 patch/依存/configure | patch適用log、target依存、CMakeCache/summary、sandbox ONと直接DRMを確認 | 適用失敗、option無視、provider競合は停止。sandbox OFF等で黙って通さない |
| C2 本体compile/link | WPE全体と必要helperの最終link成功、時間/peak memory/容量を保存 | OOM、上限、空き不足は停止。旧configure成功を完走と呼ばない |
| C3 staging/target install | 必要`.pc`/header/library/helper/share資産の配置とtarget ELF依存閉包を確認 | host/Debian path混入、loader/library欠落、prefix不整合なら供給成功としない |
| C4 launcher compile/link | 現sourceがtarget SDKだけでlinkし、target rootfs側依存と一致 | headerだけの存在、版文字列だけ、異なるbackendの成功で代用しない |
| C5 結果保全 | input/output hash、recipe差分、license/source記録、logと失敗も永続保存先へ照合 | 一時directory/volumeの存在だけを保全としない |

C1〜C4の時間は共通12時間枠内で積算し、checkpointごとに予算をリセットしない。
cap到達・OOM等では失敗/未完を保存し、無断再試行しない。監視/停止と成果保存の仕組みが
確認できなければ開始しない。今回その仕組みを実装・試験したわけではない。

この検証はimageを生成せずに開始でき、#198全体の実機完了や#199のdaemon再検証を
hard dependencyにしない。技術的に同一toolchain/stagingが必要なら、その成果物を入力として識別する。
hostでtarget binaryへ`ldd`を直接実行せず、cross readelf等による静的検査を基本にする。
target上の`ldd -r`相当、process起動、sandbox実隔離はPi再開後の別手順とする。

## 6. #46への戻し方と保守

| 結果 | 方式比較へ渡す結論 |
|---|---|
| 仕様時点で依存競合/大幅な独自stack更新が必要 | Buildrootの追加保守範囲を明記し、Raspberry Pi OS案と再比較。方式不採用を自動確定しない |
| compile/link/install/launcherが上限内で成功 | 「Buildrootによるhost側供給証拠」を追加。image/boot/表示/音声/sandbox実隔離の成功とはしない |
| 失敗または資源上限到達 | 失敗stage・再利用条件・必要な追加上限を明示。metadata/WPE/sandboxを落として同等比較を装わない |

公式packageを使えるgraphics/input依存は上流追従の負担を減らせるが、2.54向け差分・
patch・launcher・ABI整合は引き続き自分たちの保守対象になる。
[WSA-2026-0006][W-security]は2.54.0より前を対象とする脆弱性情報を含む。
公式recipeに存在することをsecurity更新済みの保証にせず、製品供給版と更新責任を#220で評価する。
本書は全CVEの適用監査でも、2.54.0+2patchの安全宣言でもない。外部画像/Custom UIを扱うため、
loopback表示を理由にWebKitの更新を不要とはしない。

#66は本体/依存license、#68は最終image実体のinventory、Buildroot採用時のみ#69へ接続する。
Debian開発経路と#67は維持し、今回の比較で正式供給・license・製品imageを完了扱いにしない。

Issue追記案（未投稿）は、#200へ「H0/H3の机上比較を実施、2patch/sandbox/provider/launcher差分を特定、
最終build未実行」、#220へ「Debian開発供給とBuildroot候補の差分・security/ABI/権限の残判断を参照」とする。
新Issue作成、既存Issueのclose、PR ready/mergeは行わない。

## 7. 検証限界

今回確認したのは固定source/recipe/report・最新Issue/PRと小規模な公式metadataの読取り比較。
patch実適用、Kconfig解決、configure、compile/link/install、ELF実体検査、image生成は未実施。
PiでのDRM/TV、CD-ROM/ALSA音声、CEC、namespace/seccomp/権限、メモリ余裕、
初回boot・継続運転・更新復旧はすべて今回未確認である。

## 参照

[T-report]: https://github.com/udonchan/picdplayer/blob/a2c669983f8a86cc8370cc434df6f14fa6dba7b8/docs/development/reports/2026-10-06-buildroot-wpe-probe/README.md
[T-config]: https://github.com/udonchan/picdplayer/blob/a2c669983f8a86cc8370cc434df6f14fa6dba7b8/docs/development/reports/2026-10-06-buildroot-wpe-probe/trial-package/Config.in
[T-mk]: https://github.com/udonchan/picdplayer/blob/a2c669983f8a86cc8370cc434df6f14fa6dba7b8/docs/development/reports/2026-10-06-buildroot-wpe-probe/trial-package/wpewebkit.mk
[T-hash]: https://github.com/udonchan/picdplayer/blob/a2c669983f8a86cc8370cc434df6f14fa6dba7b8/docs/development/reports/2026-10-06-buildroot-wpe-probe/trial-package/wpewebkit.hash
[D-recipe]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/scripts/wpe-source-probe.Dockerfile
[D-probe]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/scripts/wpe-source-probe.sh
[D-export]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/scripts/build-wpe-runtime.sh
[D-video]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/scripts/wpe-source-video-disabled.patch
[D-sandbox]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/scripts/wpe-source-sandbox-link.patch
[D-cmake]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/CMakeLists.txt
[D-launcher]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/src/wpe_kiosk.c
[B-old]: https://github.com/buildroot/buildroot/blob/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b/package/wpewebkit/wpewebkit.mk
[B-stable]: https://github.com/buildroot/buildroot/blob/d5180309b1b66ef3b8eaccca70ad69be8e0729a1/package/wpewebkit/wpewebkit.mk
[B-current]: https://github.com/buildroot/buildroot/blob/2efd7262dff4bc43d70d785d44eab12ca1603aed/package/wpewebkit/wpewebkit.mk
[B-config]: https://github.com/buildroot/buildroot/blob/2efd7262dff4bc43d70d785d44eab12ca1603aed/package/wpewebkit/Config.in
[B-hash]: https://github.com/buildroot/buildroot/blob/2efd7262dff4bc43d70d785d44eab12ca1603aed/package/wpewebkit/wpewebkit.hash
[B-mesa]: https://github.com/buildroot/buildroot/blob/2efd7262dff4bc43d70d785d44eab12ca1603aed/package/mesa3d/mesa3d.mk
[B-input]: https://github.com/buildroot/buildroot/blob/2efd7262dff4bc43d70d785d44eab12ca1603aed/package/libinput/libinput.mk
[B-libwpe]: https://github.com/buildroot/buildroot/blob/2efd7262dff4bc43d70d785d44eab12ca1603aed/package/libwpe/libwpe.mk
[B-cmake-patch]: https://github.com/buildroot/buildroot/blob/2efd7262dff4bc43d70d785d44eab12ca1603aed/package/wpewebkit/0001-CMake-4.4-WPE-_WEBKIT_TARGET_LINK_FRAMEWORK-reports-.patch
[W-sums]: https://wpewebkit.org/releases/wpewebkit-2.54.0.tar.xz.sums
[W-security]: https://wpewebkit.org/security/WSA-2026-0006.html
