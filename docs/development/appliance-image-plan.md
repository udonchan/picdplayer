# 製品imageのv1.0方針と実施計画

本書は[#46](https://github.com/udonchan/picdplayer/issues/46)の要件・計画資料。
2026-10-22の利用者指示により、v1.0は低リスク・実現性を優先してRaspberry Pi OSベースへ
検討を限定する。全方式の比較・検証をv1.0の前提にはしない。
imageの詳細仕様・生成手段・実機適合の確定や正式releaseの承認とは区別する。
repository文書とGitHub Issueの本文/ラベルを整理した後、続行指示を受けB0の固定base取得・
選定範囲の静的検査を実施した。製品imageのbuild/生成、SD書込み、PiへのSSH・配備・再起動・公開は行っていない。

## 0. 現在の範囲

この節と5〜8節を現在の作業方針とする。1節の証拠、4節の方式比較、別reportは判断経緯として保持する。

| 区分 | v1.0での扱い |
|---|---|
| 方式の絞り込み | Raspberry Pi OSベースのimageに限定。現行WPE直接DRM、systemd、CMake installとDebian package供給を再利用する方向で最小工程を調べる |
| 理由 | 現行経路にはbuild/packageと限定Pi確認の証拠がある。BuildrootはWPE最終供給、init/provider、hardware等の未確認が増えるため、v1.0で同時に解決しない |
| 基本成果 | Imagerでローカル選択する`.img`。`.img.xz`は配布候補。Pi 3 Model B/aarch64を初期対象候補とする |
| 維持する経路 | Mac + Docker + Debian/.deb開発経路。旧branch/worktree/試作/cacheは保持する |
| 追加しない仕組み | v1.0の基準案はbase標準のpartition/書込み可能FSと既存systemdを維持。read-only/overlay/A-B/独自OTA・独自initの新規設計は追加しない。詳細と再image復旧手順は#46で詰める |
| v2.0以降へ延期 | Buildroot等の代替OS方式、#198〜#200の追加probe/recipe修正/build、移行前提の#69。将来の再開は必要性・範囲・予算を改めて判断する |
| 検討範囲外 | 公式Imager一覧掲載、公開Imager repository運営。v2.0の予定にも自動追加しない |
| 家電UX | 既存の物理CD・TVリモコン中心の体験を優先する。導入時のImager書込みと通常利用を分け、初回もkeyboard・Linux account設定・SSH・手動service操作を一般利用の必須条件にしない |
| 初期設定 | 二boot file/SSHによる利用者セットアップ案は撤回。image側で必要な初期化とPlayer自動起動を成立させる責務を調べる。独自wizard/Imagerカスタマイズを無断追加しない |
| 残す公開条件 | #66/#67/#68のlicense・inventory・notice/source、#220の採用runtime供給/ABI/sandbox/security、実機受入と復旧確認。方式を絞っても省略しない |

これはv1.0の検討基線を一つにした判断であり、Buildrootの恒久的な不採用・試作の失敗認定ではない。
Raspberry Pi OSでも新imageのboot/音声/CEC/DRM・初回設定・更新復旧は未確認。
具体的な阻害要因が見つかればまずその内容を報告し、別方式の調査を無断再開しない。

## 1. 基準と保存済み証拠

`git fetch origin`後の`refs/remotes/origin/master`は
`05a1a3e8cadc851e2ccb5f664586a55c431cab68`。
PR #260はMERGED（2026-10-21 21:57:42 UTC）。開いているPRはDraft
[#256](https://github.com/udonchan/picdplayer/pull/256)、head
`ec9fe964a9dcc31ac8cd26e1ced593c3d92f77e8`で、master未統合である。
#238/#239はOpen。本書の対象機能へ候補PCM実装を混入させない。

2026-10-22の調査場所glitter-lycheeのHEAD `9ac4dcb`は旧候補保存branchであり、image開発の基準にしない。
同名local branch `origin/master`ではなく、上記remote-tracking refを明示して実装を調べた。
この文書を実装へ引き継ぐ際は最新masterを再取得し、別の作業場所の使用を含め基準を確認する。
旧branchの文書全体を現行仕様として上書き・再利用しない。

先行調査は以下の固定資料を入力とする。古い調査をやり直すのではなく、現行との差分と未検証部分を調べる。
以下のrepository資料は、特記がなければmaster `05a1a3e`時点を参照する。

| 入力 | 証拠と限界 |
|---|---|
| [backlog](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/docs/development/backlog.md) | #21/#74/#75〜#78は終了済み調査・移管資料。再openや調査の重複を要求しない |
| [Buildrootロードマップ](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/docs/development/reports/2026-10-05-buildroot-roadmap/README.md) | 適合性と#198〜#200への分解。採用決定ではない |
| [Pi 3 probe](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/docs/development/reports/2026-10-06-buildroot-pi3-probe/README.md) | 最小`sdcard.img`生成の過去の成功。boot/CD-ROM/ALSA/CEC/DRM未確認 |
| [package probe](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/docs/development/reports/2026-10-06-buildroot-package-probe/README.md) | daemon/libdiscid build/installの過去の成功。local source、`LICENSE=UNKNOWN`、BusyBox initとsystemd unitの不一致が残る |
| [WPE probe](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/docs/development/reports/2026-10-06-buildroot-wpe-probe/README.md) | 依存build・configureまで。本体最終build・image統合・実機未確認 |
| [build手順](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/docs/manual/build.md)、[Mac + Docker手順](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/docs/manual/mac-docker-development.md) | 現行Debian/aarch64・CMake install・開発.deb経路を維持。これをimage完成の証拠にはしない |

保存refはlocalとremoteを照合し、`research/198-buildroot-pi3`が
`4129757554773959edeb9b7a017efe8f57e16465`、`research/200-buildroot-wpe`が
`a2c669983f8a86cc8370cc434df6f14fa6dba7b8`で一致した。
旧Buildroot revisionは`c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b`。
これらは再開用資料であり、旧recipeを正式採用する宣言ではない。
今回、過去の生成imageやDocker volumeの内容・存続・hashは確認していない。
一時directory、cache、volumeの存在を成果保存の保証にしない。削除・pruneも行わない。

### 保存reportの測定値とその範囲

| 過去の観測 | 意味・外挿しない範囲 |
|---|---|
| #198は153 MiBのMBR image、32 MiB FAT + 120 MiB ext4、output約8.1 GiB | app追加前の最小image。製品partition/SD容量やWPE込みサイズの要件にはしない |
| 当時のprobe containerは4 CPU / 6 GiB、Docker側は約7.7 GiB / 16 vCPU | 今回の利用可能資源ではない。実行時に再確認する |
| bind mount失敗後のvolume上の再開buildが約14分 | clean build所要時間ではない。失敗・コピー・再利用を含む履歴を保持する |
| #200はNinja自動17 jobsで6 GiBを圧迫、4 jobsでも余裕が少なく2 jobsへ低減。JSCの個別compilerが約1.3〜1.9 GiB | 最終完走の証拠ではない。CPU割当数だけで並列数を決めず、明示的に制限する |
| #199の時間/容量差、Buildroot WPE完走時間/最終容量は未測定 | Debianのcold/warm CI時間や最小imageから推定して保証しない |

当時のhost Dockerfileはbase digestがあってもapt版が未固定で、完全再現環境ではなかった。
#199は旧sourceに対するdaemon/UI/unitのinstall確認で、package入りimageの再生成は未確認。
現masterの標準WPE/kiosk供給を含む製品package検証とは分ける。

### 現master実装から引き継ぐ条件

配置の正本は[CMakeLists.txt](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/CMakeLists.txt)、
起動条件は[systemd運用手順](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/docs/manual/systemd.md)
とunit/wrapperである。image側に別の手書きinstall一覧を作るのではなく、生成物から確認する。
以下は現行の配置・挙動であって、製品layoutの確定ではない。

| 現行の証拠 | image検討へ引き継ぐ不足・判断 |
|---|---|
| daemon `/usr/local/bin/cdplayerd`、launcher/wrapper `/usr/local/libexec/`、unit `/usr/local/lib/systemd/system/`、UI等 `/usr/local/share/picdplayer/` | 現行CMake install/.debの配置を固定Raspberry Pi OS baseへの導入結果と最終image実体で照合する |
| WPE WebKit 2.54.0+2patchを専用`/opt/picdplayer-wpe/2.54.0`へ同梱。private library pathを使用 | SDK header/pkg-configはruntimeに含めない。host glibc/ldconfigを置換せず、target loader/依存との整合を確認 |
| `/etc/default/picdplayer`、`picdplayer-kiosk`は運用者設定。packageは設定生成・user作成・初回service enable/startを行わない | image provisioningが専用user、group、loopback API、metadata、settings-file、enableを明示する。unitを置くだけで製品機能が有効になるとしない |
| 非login `picdplayer`を両serviceが使用。daemonは`video cdrom audio`、kioskは`video render input`、PAM/tty1条件あり | 既存systemdとPAM/seat/logind/device権限が固定baseで成立するか確認。Bash/coreutils `timeout`もwrapper依存として扱う |
| `/var/lib/picdplayer`はsettings指定時の保存先候補、kiosk HOME/state。`/var/cache/picdplayer`はmetadata/coverとkiosk cache。`/run/picdplayer-kiosk`はruntime/marker | 永続・揮発・quotaを分離。両unitの共有`StateDirectory=picdplayer`はdaemon 0700 / kiosk 0750で指定が異なり、mode整合も要検討 |
| settingsは`--settings-file`指定時のみ保存。schema v2、旧binaryとのdowngrade互換に注意 | schema移行/rollbackはimage更新仕様へ。file sync/renameの実装だけで電源断耐久性を保証しない |
| metadata/cover cacheは64 MiB、30日経過後の再取得と通信失敗時fallback。背景画像はdisk cacheではなくmemory保持 | 現行上限を入力にし、browser/journal等を含むimage全体の容量を別に見積もる |
| WPE主系だがChromium fallbackの実装・既定値が残る。Chromium/Cageはv1受入対象外 | 非同梱なら明示`fallback=none`相当の製品設定が必要か判断。無断削除・切替や受入対象追加をしない |
| APIはloopback既定、Release Inspectorは既定OFF。外向きHTTPSはDNS/CA/時刻が必要。日本語fontも必要 | networkなしでの起動と外部取得を区別し、Debug設定・developer key・写真の非同梱を検査する |
| CIは版/commit/hash/stage/package/profileを検査した候補を短期保持。公開未承認・hardware未確認として扱う | 既存候補経路を未実装へ戻さず、永続release/image供給は#45で別設計する |

WPEの実装根拠は[SDK供給script](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/scripts/build-wpe-runtime.sh)、
[source recipe](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/scripts/wpe-source-probe.Dockerfile)、
[configure/verify script](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/scripts/wpe-source-probe.sh)。
標準SDK exportのconfigure/build/stageと独立verify actionを混同せず、
cache復元だけをABI検査済みにしない。
初回処理の境界は[postinst](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/packaging/debian/postinst.in)、
候補検査は[release-artifact.py](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/scripts/release-artifact.py)
と[CI](https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/.github/workflows/ci.yml)を参照する。
Mac + Docker手順冒頭には旧Cage/Chromiumの図が残るが、現runtimeは上記CMake/unit/wrapperのWPE主系を基準にする。
この文書調査では不整合の実装修正やmanual全体の改稿は行わない。

## 2. #46のimage要件表

方式は0節のRaspberry Pi OSベースに限定した。以下の「候補」「未決」は、その方式内の詳細を指す。
v1.0の範囲を広げる代替方式・高度な更新機構を、この表の未決事項から自動的に追加しない。
現行実装の観測値を、未承認の製品要件へ自動昇格させない。

| ID / 項目 | 確定している目標・現状 | 候補・未決事項 | 決定・検証の条件 / 所有Issue |
|---|---|---|---|
| R01 対象 | Raspberry Pi用bootableディスクimage | 初期候補Pi 3 Model B / aarch64。Pi 3 B+・4・5、armhf、USB bootは未約束 | 対応機種、RAM、USB drive、HDMI/音声の代表構成を承認。#46/#47 |
| R02 配布形式 | ローカル選択できる`.img`を基本とし、`.img.xz`は配布候補 | 圧縮設定、最小SD容量、展開後bytes、download上限は未決 | 圧縮前後hash/sizeを別管理し展開結果一致を検査。#47/#45 |
| R03 Imager版 | ローカル書込みと初期設定を区別。公式一覧/公開repositoryは範囲外 | 調査基準v2.0.12、初回受入候補macOS版。最低対応版と他host OSは未決 | 対応版ごとの選択/書込み/verify記録。v1.xを無検証で保証しない。#46/#47 |
| R04 partition / FS | パーティションを含むdisk imageが必要。rootfs単体や.debでは代用不可 | 固定baseの標準layout/書込み可能FSを維持する基準案。実bytes、拡張、mount/UUID等を確認。A/B・独自layoutは追加しない | boot firmware/kernel/DTBとroot指定整合、境界・重複・容量を静的検査後、実boot。#46/#47 |
| R05 容量 | image実体と使用可能な媒体容量を区別する | boot/root/dataのbytes、空きmargin、最小SD実bytes、初回拡張、swap/tmpfs上限は未決 | WPE込み実測から予算化。満杯・拡張失敗時の復旧条件を決める。#46/#47 |
| R06 初回起動 | 再配布imageに個人設定・秘密情報を残さず、利用者のOS設定なしでPlayerへ至る | machine-id、鍵/seed、FS拡張等の自動初期化、汎用user対話/gettyとの分離、再実行/失敗時処理は未設計 | keyboard/SSH・credential/file編集・手動service切替なしの初回と再起動、networkなしの基本再生を受入へ。#46/#47 |
| R07 user / service | 既存systemd/unitと権限モデルを再利用する方向 | サービス専用userと運用userの分離、UID/GID、DRM/seat/input/audio/cdrom権限、sudo範囲を固定baseで確認 | root実行を自動採用せず、最小権限とWPE sandboxを確認。#46/#220 |
| R08 SSH | 通常利用の必須経路にしない。共通credential/開発接続設定は配布しない | 安全な開発/保守用の明示的有効化・account/鍵・実効policy・復旧経路は未決 | SSHが設定できないことを基本CD再生の開始不可に結び付けない。利用者UXと技術者診断を分離。#46/#47/#220 |
| R09 network | 再生本体と外部metadata取得を区別。秘密を公開artifactに混入させない | 有線DHCPを初期候補、Wi-Fi投入方法・国設定・DNS/NTP/TLS・proxy・hostnameは未決 | offline時の起動、時刻未確定時のHTTPS、再接続と診断を定義。既存loopback APIを無断LAN公開しない。#46/#220/#47 |
| R10 設定永続 | 現行はRead Policy/背景ON-OFFの保存契約あり。背景既定OFF | image内の保存先、schema移行、backup/import、factory resetの保持対象・atomic性・容量上限は未決 | 保存失敗と実効状態を区別。通常再起動成功を電源断耐久性にしない。#46/#47 |
| R11 cache | metadata cacheと画像runtimeのmemory保持は別 | base標準の書込先と既存cache方針を入力に、容量/削除とbrowserの書込み先を確認 | cache欠損・破損・満杯が再生を阻害しない。背景disk cacheを新たな必須実装にしない。#46/#47 |
| R12 log / runtime | 診断と全書込み先の棚卸しが必要 | journal永続/volatile、rotation、core dump、`/run`/`/tmp`/WPE cacheの容量、ログexport・個人情報除去 | Pi 3のRAM/disk内に有界化し、失敗後に必要な証拠を取得できる。#46/#47/#220 |
| R13 鍵 / 個人情報 | 開発者API key、Wi-Fi credential、写真、private UIをimageへ流用しない | 利用者key投入、所有権/mode、更新時保持、reset時消去の方法は未決 | image実体のsecret混入検査。背景明示ONと写真利用判断は既存契約を維持。#46/#66/#47 |
| R14 Custom UI | 現行の起動時検証・標準UI fallbackを維持する入力 | 永続配置、所有権、移行、更新/rollbackの採否は未決 | 代表UIの通常受入は#184。安全な更新設計は#42。image計画で#42全体を無条件の前提にしない |
| R15 read-only | v1.0では採用せず、書込み可能なbase FSを維持する基準案 | 新規overlay/immutable構成を避ける。cache/log上限と設定保全は引き続き確認 | 既存起動/更新/診断との変更を抑えるため見送る。電源断耐久性は未保証。#20の追加検討はv2.0以降 |
| R16 更新 | Mac + Docker + Debian/.deb開発経路を維持。独自OTA/A-Bを追加しない | v1.0は再image+必要設定の退避/復元を復旧の基準案とし、通常package更新の対応範囲と手順を限定する | 認証/整合検査、停止境界、空き容量、設定移行、失敗時状態を定義。#46/#220/#47。中断復旧は#147 |
| R17 復旧 | 元の運用mediaを破壊しない。復旧を通常更新と分ける | 別SD再image+設定復元を基本候補、in-place修復/rollback、data破損・factory resetは未決 | 故障段階別の復旧手順・データ損失許容を承認。Debianのpost-unpack中断は#147、image復旧は#47 |
| R18 安全性 | 今回Pi操作なし。sandbox設定ONだけを実隔離としない | 外部port、権限、脆弱性対応窓口/頻度、診断時のみInspectorを許すか等は未決 | Release候補のDebug機能・開発設定非混入、source/ABI/権限/更新を#220と確認。#19/#254全体は先行条件にしない |
| R19 再生成 / 保管 | source/config/toolchain/patchの識別が必要。cache hitは再現性の証明でない | 機能再現かbyte-identicalか、保存先・世代数・容量/期間は未決 | input/output hash、log、recipe差分、inventoryを永続保管。cacheなし再構築の範囲を合意。#46/#220/#45 |
| R20 公開条件 | 正式公開は別承認。本体licenseも未確定。公式一覧/公開repositoryは範囲外 | image配布先、署名、notice/source提供、supportは未決 | 最終image inventory/firmware/非package fileまで対応付け、不明license/secret/未受入を公開停止条件へ。#66/#67/#68/#45。#69はv2.0以降 |

未測定の容量等を保証値で埋めない。v1.0ではbaseからの変更を抑え、read-onlyを見送る理由と、
電源断時の損失・再imageによる復旧範囲を明記する。高度な耐障害更新の保証は追加しない。

## 3. v1.0のImager対応

公式一次資料の調査基準は[v2.0.12](https://github.com/raspberrypi/rpi-imager/releases/tag/v2.0.12)
（公開2026-10-08、commit `84b03d96cd4c390ebe475a4b5db5735e5535e9a6`）。
最低対応版の決定でも、この版での実動作確認でもない。

| 要件 | 必要なもの | 現在の扱い |
|---|---|---|
| I1 ローカルimage書込み | `Use custom`で選べるdisk `.img`、候補`.img.xz`、サイズ/hash、媒体選択・書込み後verify | v1.0の対象。選択・build成功だけでは書込み/boot成功でない |
| I2 初期設定カスタマイズ | 対応metadataとimage内の実適用機構が必要 | v1.0の必須目標に積み増さない。まず直接選択imageの初回設定導線を決める。systemd採用だけで対応済みにしない |
| I3 公開Imager repository運営 | I1とは別の配布・運営機能 | 利用者指示で検討範囲外。予定・実施計画を作らない |
| I4 公式OS一覧掲載 | I1とは別の掲載手続き | 利用者指示で検討範囲外。予定・実施計画を作らない |

[customisation仕様](https://github.com/raspberrypi/rpi-imager/blob/84b03d96cd4c390ebe475a4b5db5735e5535e9a6/doc/os_customisation_formats.md)では、
Imager 2.xのローカル`.img`/`.img.xz`直接選択は形式情報がないため`init_format=none`。
カスタマイズしたい場合は適切なlocal manifestを用意する。
1.xは未知/欠落の形式をsystemdと仮定する既知の問題があり、設定欄が出ても適用成功とは限らない。
初回候補は2.xでのI1・カスタマイズなしとし、network/user設定の代替導線は#46で選ぶ。

base版ごとに初回設定機構が異なるため、Raspberry Pi OS派生だから旧systemd方式が使えるとは限らない。
初期設定にlocal manifestを使う案は公開repository運営とは別だが、現時点で採用しない。
imageの圧縮前後hash/sizeは別管理し、展開結果一致を検査する。公開OS-list schema/掲載申請の追加調査は行わない。

## 4. 判断に使った方式比較（保存資料）

現在のv1.0作業列はRaspberry Pi OSベースのみ。以下は絞り込み前の比較を保存したもので、
Buildroot側の不足証拠をv1.0までに埋めるTODOではない。

<details>
<summary>方式比較の経緯とv2.0以降に再評価する論点</summary>

Raspberry Pi OS案も「Piの稼働SDをcloneして配布」ではなく、固定したbase imageまたは
公式生成手段に再現可能な組込み工程を追加する案とする。pi-gen等の具体的toolは未選定。
以下は同じR01〜R20を満たすための比較で、点数による採用確定は行わない。

| 観点 | Buildroot案 | Raspberry Pi OSベース案 | 判断に不足する証拠 |
|---|---|---|---|
| WPE供給 | 旧公式recipeとWPEPlatform直接DRMの差が大きい。#200はconfigureまで | Trixie arm64で2.54.0+2patchの開発供給成立。ただし正式供給#220は未完 | Buildroot最終link/stage、両案のABI・sandbox・更新経路。Debian成功で#200を代替しない |
| 現行との差 | libdiscid外部package、現行CMake option/install再照合、init/user/device設定の移植が必要 | systemd/.deb/CMake installと開発recipeを再利用しやすい。image初期化は未実装 | #199の現master package再検証、.deb maintainer処理とimage provisioningの責務 |
| 保守 | 小さい構成を制御できる反面、kernel/firmware/WebKit/toolchainの更新統合を自分で負う | OS/packageの更新基盤を使える反面、base版・apt供給・独自WPE prefixの整合を管理 | 更新責任者、周期、support期間、patch追従工数 |
| security | 不要package削減は可能。少数packageでも安全性は自動保証されない | Debian/Raspberry Pi OSの更新を利用可能。ただし更新適用でABIや挙動が変わる | CVE監視と修正反映期限、信頼できるartifact、最小権限・実sandbox検査 |
| build時間 / 容量 | 最小imageとWPE込み製品imageは別。toolchain/WebKit cold buildが支配し得る | baseを利用してapp組込みは軽減可能だがWPE cold buildが残る | 同等機能でcold/warmの時間・peak RAM・disk・download量。現時点で優位を断定不可 |
| FS / 更新・復旧 | read-onlyやimage一括更新を設計しやすいがA/B・rollbackは追加実装 | writable ext4 + package更新を再利用しやすい。read-onlyやatomic更新は別設計 | 再imageで許容するデータ損失、オンライン更新必須か、電源断時の受入 |
| inventory | build metadata/legal-infoを使えるが追加file/firmware・最終rootfsとの照合が必要 | dpkg inventoryと#67を使えるが`/opt`等同梱物・非package fileは補完が必要 | 最終image起点の#68。build host package一覧を混ぜない |
| license | 旧`UNKNOWN`は配布承認を止める未解決項目。Buildroot legal-infoは法的保証ではない | package copyrightが手掛かりだが本体license/WPE/firmwareの監査は残る | #66とsource/notice義務。Buildroot採用時だけ#69で情報源を接続 |
| 実機リスク | Pi 3最小imageすらboot/CD-ROM/ALSA/CEC/DRM未確認 | 現行Debian経路の限定Pi記録を再利用できるが、新しい配布imageのboot/受入は未確認 | 別SDで同じ受入項目を実施。過去の開発機成功を新image合格としない |
| 開発経路 | 別製品build経路として追加し、Mac/Docker/.debを残す | 現行経路を維持してimage生成を追加しやすい | CI責務、二系統の差分、維持可能な作業量 |

将来Buildrootを再評価する場合も、WPE/metadata/sandboxを落として同等比較を装わない。
v1.0では既存.deb供給を第一候補に、最終imageへの導入時の境界を確認する。
比較表中のBuildroot採用時の責務・依存見直しはv2.0以降の論点とする。

</details>

## 5. v1.0の最小作業列

Buildrootの追加調査と全方式の同等条件buildを、以下の前提にしない。
現行のWPE開発供給を入力にするが、正式供給#220や新imageの受入を完了済みとはしない。

| 段階 / 対応Issue | 入力 | 成果物・完了条件 | 保留条件・検証限界 |
|---|---|---|---|
| V0 最小組込み仕様 / #46、#220 | 現master、現行Debian WPE/.deb、固定base候補 | Raspberry Pi OS Lite 64-bit/Trixieを第一候補に、base版/hash、package導入、user/service初期化、初回設定、書込先・復旧を一構成に絞った仕様 | 具体的base・生成tool・初回導線は未確定。選んだbaseとWPE依存が不整合なら停止して報告。代替OS調査を自動再開しない |
| V1 host生成 / #47 | V0仕様、承認済みの資源/保存先、固定source/package | base標準のboot/kernel/DTB/partitionを維持し、現行CMake/.debを用いて隔離環境で`.img`を生成。input/output hashと生成logを保存 | 今回未実行。build/downloadは必要量・時間上限を先に決める。生成成功はboot成功ではない |
| V2 静的検査 / #47、#67/#68 | V1のimage実体、#66の監査入力 | partition/FS/boot設定、user/unit、依存/loader、書込先、secret非混入、inventory、圧縮展開/hashを検査 | image実体がない間は未実施。license/供給不明、依存欠落は公開停止条件。runtime/hardwareは証明しない |
| V3 Imager書込み / #47 | V2合格image、承認済みImager版と別媒体 | ローカル`.img`、採用する場合`.img.xz`の書込み/verify記録 | 現在SD書込み禁止のため保留。設定カスタマイズやboot成功と混同しない |
| V4 Pi受入・復旧 / #47、#184/#252/#51、必要範囲で#147 | 固定image/実機、元SDと復元手順、操作・時間・停止条件 | 初回もOS設定/keyboard/SSHなしでPlayerへ到達、CD音声/CEC/設定保存、networkなし再生、通常運転と選定した更新/媒体復元を確認 | 実機不在で保留。通常運転#184、物理入力#252、写真#51を同runで参照。電源断/中断試験は別安全承認 |
| V5 正式配布 / #45、#66/#67/#68/#220 | V2/V4証拠、採用構成のlicense/source/notice・安全更新・復旧条件 | image/checksum/manifest/SBOM等を同一版へ結び、公開停止条件を照合し別途公開承認を得る | 短期CI artifactは正式供給でない。公式一覧掲載/公開Imager repositoryはこのgateにも含めない |

### 次に調べる範囲

V0の対象は「固定base候補と既存packageの最小組込み手順」を文書で一つに絞ること。
baseの版・初回user/SSH/network処理、既存.debの導入と依存・WPE prefix、service enable、
必要設定と鍵の非混入、再image復旧の保持対象を確認する。
pi-gen等のtool比較を広く始めず、base標準の仕組みと現行toolingで足りる最小工程を先に確認する。
base image download・build・recipe実装・Pi操作は、この文書整理から自動的には開始しない。

2026-10-22のV0机上調査と、利用者の続行指示によるB0取得・読取り検査を
[最小組込み仕様案と検査記録](reports/2026-10-22-raspios-image-v1/README.md)に記録した。
公式2026-10-06 Lite64/Trixieを一回取得し、SHA-256/xz/MBR・FAT/ext4ヘッダ・47+1 fileを検査した。
公開一覧のraspi-config版表記は実dpkg/changelogと照合できた。root空きは約434 MiBで、
WPE等の導入量を未測定のまま収容可能とはしない。署名/全FS整合/boot/実機適合の検証ではない。
V0は部分完了。家電UXという既存方針を再確認し、keyboardをSSHへ置き換えるだけの一般利用案は撤回した。
入力生成toolの実装は現在の次作業ではない。次はbase初期化・汎用user対話/gettyと製品service自動起動の責務、
unit依存・account/権限の静的棚卸しへ絞る。通常利用にLinux設定を持ち込まないための不足を特定する。
自動起動や保守経路の実装・安全性を先取りしない。追加容量/state mode等も未確認で、mount/chroot/package導入・製品image生成は未実施。
独自wizard、全OS再生成、Buildroot追加検証へ広げない。local image/receiptの所在・hashと資源監視の限界もreportへ記録した。

詳細な容量・初期設定・更新手順はV0で確定するまで未決とし、#46はOpenを維持する。
初期設定が成立しないまま「Imagerに渡せるので利用可能」としない。
低リスクという選択理由は実機検証済みを意味せず、実機再開後のV3/V4を省略しない。

## 6. Issueの分担と延期

2026-10-22、利用者指示により既存Issue本文へ現在の区分・保留理由・再開条件を追記し、
`Pending`ラベルを新設した。ラベルはIssue全体の明示的な延期を表し、依存待ち・実機受入待ちを
すべてv2.0へ延期する意味には使わない。既存本文/checkbox/証拠は原文のまま折り畳みに保存し、
既存ラベルとOpen状態を維持した。新Issue作成・close・PR操作は行っていない。

`Pending`反映済みは#19/#20/#42/#69/#124/#198/#199/#200/#250/#254。
このうち#124はUI改修後、#42は具体的更新需要待ちであり、v2.0実施を自動確約しない。
v1.0方針を追記し`Pending`を付けなかったのは#46/#47/#220/#66/#67/#68/#45。
その他の関連Issueは下記の責務を確認しただけで、本文・ラベルを今回変更していない。

| Issue / 項目 | 現在の方針・反映状況 |
|---|---|
| #46 | v1.0の検討をRaspberry Pi OSベースに限定。選定理由、base標準FSを維持しread-onlyを見送る理由、未決の初期設定・保持・更新/復旧・容量を記録。詳細未確定のため完了にはしない |
| #47 | #46の詳細仕様を入力にV1〜V4。Buildroot試作や#198〜#200の完了を先行条件にしない |
| #198/#199/#200 | v2.0以降へ延期。追加probe・recipe修正・buildはv1.0作業列から外す。保存成果と未確認を維持し、完成/失敗/恒久不採用としてcloseしない |
| #220 | v1.0で使うDebian系WPEの供給・依存固定・ABI/sandbox・security更新の必要範囲を確認。Buildroot向け移植は延期。既存2patchを含むruntimeの新image適合は未確認で、現行Debianの成功を新imageの成功にしない |
| #66/#67/#68 | 現行package情報を最終Raspberry Pi OS image実体のinventory/notice/sourceへ接続。hostだけのpackageや旧試作を混ぜない。方式縮小で公開前の義務を削らない |
| #69 | embedded build system採用時の候補としてv2.0以降へ延期。v1.0の#68/#45をblockしない |
| #20 / 高度な更新方式 | read-only/overlay/A-B/独自OTAはv1.0では追加しない基準案。新規方式の設計・実装はv2.0以降へ。既存FSの書込先上限・設定保全・復旧確認はv1.0に残す |
| #147 | v1.0で対応を表明するDebian package更新の中断復旧に限定して連携。独自OTAや全電源断条件の保証へ広げない。安全な専用環境待ちは維持 |
| #184/#252/#51 | 新imageの通常運転、物理リモコン、写真/TV/試聴を担当。既存runの参照は可能だが、新imageの未実施を合格へ変えない |
| #19/#254 | 既存の保留/V2・非blockingを維持。v1.0 imageの前提へ無条件に追加しない |
| #42/#124/#250 | 既存の明示的保留にPendingを反映。#42は具体的Custom UI更新需要待ち、#124はUI改修後の再確認待ち、#250は既存V2 hardening。#124をv2.0延期や修正済みとしない |
| #45 | V5の正式公開を所有。commit/push/PR/公開はそれぞれ別途指示待ち |
| 公式一覧/公開Imager repository | 検討範囲外として旧計画から除外。v2.0の予定にも入れない。通常のimage artifact配布先の決定とは別 |

## 7. 現在の到達点

実装の基準は再取得済みmaster `05a1a3e`。GitHub #46/#47にはRaspberry Pi OSベースへの
v1.0絞り込みと詳細/実行条件待ちを追記し、Openを維持した。上記17件の本文更新と
10件のPending付与は再取得して確認した。旧本文内の「Issue未変更」「方式未決定」は当時の記録であり、
現在のIssue先頭区分を優先する。#238/#239とDraft PR #256は変更していない。
利用者指定「初回もUSB keyboardなし」は維持するが、家電UXという既存方針に反するSSH必須案は撤回する。
一般利用者へcredential生成/IP探索/service操作を求めず、自動Player起動とTVリモコン操作を受入の中心とする。
技術的なheadless調査は保存し、現在の一般利用導線・入力tool着手の根拠にはしない。
この訂正は#46/#47の先頭へ反映し、以前のSSH案を含む本文・既存ラベル・Open状態を保持して確認した。
計画整理に加えB0で公式base一つの取得・hash/xz確認・選定範囲の静的検査をmacOS上で完了した。
これは新しい製品imageの生成/検査ではない。host/container build、package導入、Imager書込み、
Pi boot・CD-ROM/ALSA/CEC/DRM・sandbox実隔離・更新復旧・製品受入は新たに検証していない。
2026-10-22の調査では既存branch/worktree/試作/cacheを破棄せず、commit/push/PR/正式公開も行わなかった。
2026-10-24の文書統合は、利用者の別指示により下記の範囲で行う。製品releaseの公開ではない。

## 8. 検討の区切りと実機確認への引継ぎ（2026-10-24）

このsessionのimage検討は、v1.0方針・家電UX要件・Issue区分とB0静的検査の記録で区切る。
base初期化/unit/account設計の続きや入力生成toolの実装を、今回のmaster統合の前提にしない。

| 項目 | 引継ぎ時の扱い |
|---|---|
| 統合範囲 | 現master `05a1a3e`からの別branchへ文書とB0要約JSONだけを移す。製品source/CMake/systemd/package/CIの変更はない。旧glitter-lycheeの実装全体をmergeしない |
| 残る状態 | #46/V0は未完、#47の製品imageは未生成。baseの静的検査をboot/音声/CEC/DRM・家電UX受入の成功へ読み替えない |
| 保存 | [B0要約](reports/2026-10-22-raspios-image-v1/b0-summary.json)と調査reportをversion管理する。大きなimage・全receipt・期限付き補助scriptは調査元worktreeのlocal保管。存在・保持期間の保証とは区別し、破棄しない |
| 実機確認の再開 | 現行Debian/.deb・WPE標準経路で既存#184/#252/#51へ戻れる。image完成やBuildroot追加調査は、それらの既存受入の先行条件ではない |
| 最初の確認 | 次sessionで実際のcommit/package hash・profile・WPE/runtime・service状態、電源/冷却・TV/drive/通常CDを識別し、操作列・時間・中断・復元条件を再確認してから始める。今回Piへ接続/配備/再起動していない |
| 記録先 | 通常cold boot/継続運転は#184、物理リモコンは#252、実写真/TV可読性/試聴は#51。同runを相互参照し、全matrixや新自動化基盤を追加しない |
| 変更しない候補 | #238/#239はOpen、PR #256はDraft/未統合を確認。今回ready/merge/closeしない。Pending10件と既存hardware-test-deferredも維持 |

文書のみの統合を理由に実機の再配備・rebootや新たなbuildを強制しない。
手元のpackageとsourceの同一性が確認できない場合だけ、既存toolingで必要な範囲を再準備する。
新しい実機操作の承認はこの引継ぎ記述では代用しない。

<details>
<summary>付録: 絞り込み前の再開計画（旧節番号を保持、v1.0では実施しない）</summary>

以下は2026-10-22の絞り込み前に作成した比較・probe計画の保存資料。
「次」「再開」「承認後」等は当時の記述であり、現在の着手指示ではない。
現在のv1.0計画は上の0節と5〜8節を優先する。Buildroot作業はv2.0以降に再判断し、
旧記述中の公式一覧/公開repositoryの後続可能性は撤回して検討範囲外とする。

### 旧5. 段階別計画とgate

初回はP0の資料作成まで。P1以降のbuild実行承認や資源確保を含まない。
後続のWPE机上比較は下記5.3節へ記録し、初回時点の未実施と分ける。
各gateは独立した証拠であり、image生成、静的妥当性、Imager書込み、実機boot、製品受入を
一つの「成功」にまとめない。

| 段階 / 対応Issue | 入力 | 成果物 | 完了条件 | 保留・停止条件 / 検証限界 |
|---|---|---|---|---|
| P0 要件・再開条件 / #46 | 現master、Issue、保存report、Imager公式資料 | 本書、未決事項、Issue修正案 | 要件・候補・証拠・責務・段階を分離してレビュー可能 | #46自体は方式とread-only採否未決でOpen継続。今回の到達点 |
| P1 host再開準備 / #198/#199/#200、並行#66/#220 | P0、保存commit、現master、資源棚卸し | 固定input一覧、旧との差分、実行予算、保存先 | 下記小単位ごとの範囲・cap・停止/保存手順を承認 | host能力/空き不足、source/権利不明、上限未承認ならbuild/downloadせず停止。Pi不要 |
| P2 bounded probe / #198非実機・#199・#200 | P1で承認したconfig/source/container | 最小image生成・静的記録、package/WPE build結果（失敗含む）、時間/資源 | 各probeの成否と#46に必要な制約が識別可能 | 予算到達なら途中結果を保存し再計画。#198の実機完了をhost作業の必須条件にしない |
| P3 方式判断 / #46、#20採否、#66/#220 | 比較表、P2の証拠、製品運用要求 | 選定理由・代替案・read-only理由、partition/容量/更新/初期設定仕様 | 利用者が採用方式と残リスク・実機待ち条件を明示承認 | hostのみならhardware適合は未決のまま。条件付き判断と最終実機適合を区別 |
| P4 image生成 / #47 | P3の選定仕様、固定sourceと供給recipe | version付disk `.img`、候補`.img.xz`、log、input/output hash | 隔離clean環境から生成し、入力と出力を識別・再実行可能 | ここでは「生成済み」であってbootable実証済みではない。正式公開不可 |
| P5 image静的妥当性 / #47、#68、採用時#69 | P4のimage実体、選定仕様、#66の監査 | partition/FS/boot設定/ELF依存/権限/secret検査、image inventoryとnotice/source対応 | 領域境界、FS、kernel/DTB/root指定、user/unit、動的loaderと同梱library、圧縮展開/hashが仕様に合う | offline検査でdevice・sandbox実隔離・boot・音声は不明。不明license/混入secret/依存欠落は次gate停止 |
| P6 Imager書込み / #47 | P5合格artifact、承認済みImager版・host・別媒体 | `.img`と候補`.img.xz`の選択/書込み/verify記録 | 対象媒体を確認し、書込み・verify成功。I2採用時は注入内容も別検査 | 現在はSD操作禁止のため保留。書込み成功はboot/設定適用の証明ではない |
| P7 Pi boot / #47、Buildrootなら#198実機部分 | P6媒体、安全な電源/冷却/TV/drive、元SDと復元手順 | 初回boot、再起動、固有鍵/初期設定、device・service観測 | 選定機種でbootしUSB CD-ROM/ALSA/CEC/DRMと初回処理を確認 | 実機再開時に操作・時間・復元を再承認。device存在だけでCD再生・TV/音声成功としない |
| P8 製品受入 / #47、#184/#252/#51、必要時#147/#20 | 固定imageとhardware、承認されたrun条件 | 再生/kiosk/設定永続/更新復旧、TV/試聴/操作、失敗・未確認一覧 | 選定仕様の受入を満たす。通常運転#184、物理入力#252、写真#51を同runで相互参照 | 新imageの証拠が必要。中断・電源断試験は別安全承認。#19/#254を無条件で先行依存にしない |
| P9 正式配布 / #45、#68、採用時#69、#66/#220 | P5 inventory、P7/P8受入、license/source/notice、供給・復旧手順 | 同一版image/checksum/manifest/SBOM/notice/source情報、公開手順 | 公開停止条件を満たし利用者が別途release承認 | 現在の短期CI候補artifactは正式releaseでない。I3/I4は別要件として後続可能 |

P5のinventory抽出は実機を待たず進められるが、#47全体と製品受入は実機なしでは完了しない。
再生成できることとbyte-identicalであることも別判定とし、後者を要求する場合はP3で追加する。

### 5.1 bounded probeの実行案（未承認）

以下の数値は将来の実行上限案で、所要時間の実測値や完走保証ではない。
開始前にhost CPU/RAM/空き容量、Docker割当とfilesystem、native/emulationの別を記録する。
全buildは同時に一つだけ、既存volume/cacheを読めても変更・pruneしない新規outputへ分離する。
各単位は初回一回まで。失敗後に自動的な全面clean/retry・recipe大量更新をしない。
旧bind mount失敗を踏まえ、Linux側の試験専用filesystemと安定したcontainer内output pathを使う案を
開始前に確認する。H4は最初は明示2 compile jobsとし、Ninjaの自動並列を使わない。
後述の8 vCPU / 16 GiBは新しい上限案で、旧6 GiB環境での完走実績ではない。
それが確保できない場合は低資源の別計画を再承認し、無断で長時間実行しない。

| 単位 | 入力・作業・成果物 / 完了条件 | 提案上限（壁時計 / CPU・RAM / 追加disk / 新規network転送） | 停止と限界 |
|---|---|---|---|
| H0 再開manifest / #46/#198〜#200 | 保存ref、現master、旧config/recipeを読み、再利用可能な成果のhash・不足source・実行案を記録。package option/install/init差分を先に特定 | 60分 / buildなし / 文書・小log 100 MiB / metadata 100 MiBまで、source archive取得なし | 差分一覧を成果とする。古いvolumeがなくても消失を隠さずcold予算へ |
| H1 最小image host / #198 | 旧revisionを比較基準にし、継続使用/新revisionは変更理由を先に記録。defconfig/kernel/firmware/genimage入力を固定。`sdcard.img`生成とpartition/FS/kernel config・DTB静的検査 | 4時間 / 4 vCPU・8 GiB / 追加30 GiB / 5 GiB | cap内生成または失敗箇所の特定まで。実boot/driver動作未確認。rootfs生成を#198全体完了にしない |
| H2 package再検証 / #199 | H1のtoolchain/stagingが使える場合に現master固定sourceを投入。libdiscidのversion/hash/license根拠、CMake標準機能、install実体・依存・unit/user作成との境界を確認 | 2時間 / 4 vCPU・8 GiB / 追加10 GiB / 2 GiB | toolchain再構築が必要ならH1へ戻して再予算化。local sourceは開発検証の限界として表示し正式供給は固定archiveへ。license不明を偽のSPDXで埋めない |
| H3 WPE recipe差分 / #200/#220 | 保存probeと現行2.54.0+2patch、選定Buildroot recipeを比較。DRM/GBM/EGL/input/font/cursor/bubblewrap/DBus等を列挙し、WPEPlatform API/ABI・sandbox条件とpatch範囲を記録 | 90分 / buildなし / 100 MiB / metadata 100 MiB、archive取得なし | recipe更新範囲が広ければbuild前にレビューへ戻す。旧試作recipeをそのまま製品へ入れない |
| H4 WPE最終build / #200 | H3承認済みの一構成だけを依存build→configure→本体compile/link→staging→launcher link/target installの順にcheckpoint化。output ELF/loader/DT_NEEDEDとrootfs library、runtime資産を記録 | 12時間 / 8 vCPU・16 GiB / 追加80 GiB / 10 GiB | 開始時に追加100 GiB以上の空きを確保できなければ延期/再見積り。OOM/残容量不足/上限到達時は停止・log保存。Debian SDK binaryを混ぜない。表示・音声・sandbox実隔離は未確認 |
| H5 Pi OS比較仕様 / #46/#220 | 固定base候補の版/hash/初期設定機構、WPE/.deb投入工程、source取得/apt固定、inventoryと復旧を机上定義 | 90分 / buildなし / 100 MiB / metadata 100 MiB、base image取得なし | H4失敗でも比較を継続できる。実image生成はP3後で別予算 |

H2とH3はH0後の独立調査が可能で、H3/H4を#198実機完了待ちにしない。
H2がdaemon/UIだけを検証する場合はWPE/kiosk供給を#200待ちと明記し、
現行標準package全体を検証済みにしない。metadata/APIを維持した結果と最小構成を分ける。
H4がH1と同じtoolchainを使う場合はその生成物・hash/configが技術的な入力であり、
Issue全体の完了を依存にするのではない。
H1→H2→H4の追加disk上限は合計120 GiB、network上限は合計17 GiB
（小規模metadata取得枠は別）。capは実行前に確定し、超過は自動延長しない。
networkは全転送量、diskは割当・実使用双方を観測し、compressed source容量だけで見積もらない。
監視と停止を実装・確認できない環境では長時間buildを開始しない。

各checkpointで開始/終了時刻・exit code・使用量・失敗log・source/patch/config hashを保存する。
containerの停止はその試験対象だけに限定する。失敗時のoutputも削除せず、次回再開前に整合確認する。
成果は一時directoryから承認済みの永続保存先へ移し、manifestとhashを照合して初めて保全済みとする。
保存先・容量・保持期間が未決なら長時間buildを始めない。

### 5.2 並行検討と依存の見直し

```text
#46 要件・比較 ── H0/H1/H2/H3/H4/H5（成否を判断材料へ）
      │             #198実機部分は独立して実機再開待ち
      ├── #66 本体license・直接依存/WPE配布条件（並行）
      ├── #220 正式WPE供給・ABI・sandbox・security更新（並行）
      ↓ 利用者による方式判断
#47 image生成 → 静的検査 → Imager書込み → Pi boot → 製品受入
      └── image実体 → #68 inventory/SBOM/notice/source
                       └── Buildroot採用時のみ#69の情報源へ接続
      #67はDebian package供給を使う範囲で情報を引き継ぐ
      ↓ 受入・compliance・正式供給と復旧条件を照合
#45 正式release（別承認）
```

#66/#220の未決事項を方式判断に並行して渡し、全監査完了まで机上比較を止める必要はない。
ただし正式配布は、採用構成の権利・source/notice・安全更新・供給条件を解決するまで停止する。
Buildroot採用時は#67を「Debianの抽出toolをBuildrootへ移植する必須工程」にしない。
#67は現行.deb開発経路の責務を維持し、package/source/license情報の共通入力を
#66から#68/#69へ引き継ぐよう依存を修正する。
#69は採用決定時だけ着手し、#68の要件・image実体照合を省略した代替完了にしない。
Raspberry Pi OS案で.debを最終供給に使う場合は#67の情報をそのまま#68へ接続する。

### 5.3 WPE供給の机上検討を先行（2026-10-22）

[保存試作・現Debian・公式Buildrootの差分調査](reports/2026-10-22-buildroot-wpe-restart/README.md)を追加した。
H0/H3のsource/recipe/runtime差分を先行し、最小image再buildは行っていない。
公式2026.08/調査時点masterはWPE 2.50.5と旧backend構成のままで、現launcher向け供給を
選択だけでは満たさない。保存2.54.0試作にも現行2patch、sandbox、udev provider、
launcher/target installの差分がある。

次の小単位は一構成のrecipe修正仕様（source/patch/feature/provider/prefix/検証入力）の確定。
H4は未承認・未実行で、資源上限と成果保存先を確認するまで開始しない。
H0の旧artifact/cache実在確認・host資源監査も未完であり、H0全体の完了とはしない。
#198全体の実機完了をこの机上検討や将来のlauncher link検証の前提にしない。

## 6. 次の小単位と利用者判断

初回提案の小単位はH0（buildなしの再開manifest）。2026-10-22の利用者指示により、
H0/H3のWPE机上比較を先行した。次は5.3節のrecipe修正仕様へ絞る。
以下はbuild・実機操作・方式判断へ進む際の判断項目として維持する。
方式採用を今すぐ求めるものではなく、次回の調査範囲と費用の承認項目である。

| 判断タイミング | 利用者に必要な判断 | 未決時の扱い |
|---|---|---|
| H0の残作業前 | 現master基準の作業場所、旧artifact確認の範囲、host資源監査・成果の保存先。Pi 3 Model B/aarch64は比較の初期候補として机上調査済み | 実装・branch切替は行わず、確認できない旧成果を再利用可能としない |
| H1/H2/H4前 | 各probeの必要性、host資源・時間/disk/download cap、成果の永続保存先/保持期間 | 長時間build・大量downloadを開始しない。H3/H5の文書差分は別判断で進められる |
| P3前 | SSH/ネットワーク初期設定、I2の必要性、更新を再image中心にできるか、保持すべき設定/鍵/UI、read-only、最小SD容量の方針 | R04〜R19は未決として比較を維持。方式を無断確定しない |
| P6/P7/P8前 | 利用者が操作可能な実機・別SD・電源/冷却、操作列・時間・停止/復元条件 | 実機不在をhost成功で埋めない。SSH/SD書込み/配備/再起動はしない |
| P9前 | license・公開先・署名/保管・support/更新責任と正式release承認 | commit/push/PR/公開はそれぞれ指示待ち。公式一覧申請も別判断 |

H0の成果は「保存probeとの差分一覧 + source/license/initの不足 + 一構成分の実行カード」。
今回のWPE差分を入力にrecipe修正仕様を絞り、未確定の子Issueを大量作成しない。

## 7. 既存Issueへの追記・修正案（未投稿）

GitHubのIssue本文・状態・依存関係は今回変更していない。以下は次回承認後の編集案。
履歴・旧checkboxを削除せず、最新の位置付けを先頭へ追記する。

| Issue | 追記・修正する内容 |
|---|---|
| #46 | 本書R01〜R20を検討入力とし「要件検討再開、方式採用とread-only採否は未決」。I1〜I4を別要件化。完了条件は選定理由・代替案・採否・検証可能な数値/受入の承認まで維持 |
| #198 | 「hostの生成/静的検査は実機なしで限定再開可能」。hardware全体は未確認・Openのまま。現在の「安全な別SD準備まで再開保留」をhost部分と実機部分に分ける |
| #199 | 現masterの機能/option/install、固定source、license根拠、init/user処理を再検証。過去のdaemon成功と製品package供給完了を区別 |
| #200 | configure後のcompile/link/stageとimage統合を別checkpoint化。資源上限到達は失敗/未完として記録し、Debian成功を代用しない |
| #220/#66 | 両案の比較に必要な供給/ABI/sandbox/securityとlicense論点を並行入力とする。現行開発を監査完了待ちに戻さない |
| #47 | P4〜P8を分離した受入表へ接続。#46承認前に製品実装しない。Imager書込みとboot、通常受入を別gate化 |
| #67/#68/#69 | 方式決定後、#67必須の依存をDebian package使用時へ条件化。#68の旧Cage/Chromium固定的列挙を「選定image実体（WPEを含む）」へ更新。#69はembedded方式採用時のみ |
| #45 | 既存版付き候補生成を維持し、imageの公開はP9の受入/compliance/復旧・正式承認と接続。短期CI artifactを永続供給へ読み替えない |

#19、V2の#254は関連する保留要求であり、host調査・image生成の無条件の先行依存にしない。
#184/#252/#51の既存runを参照できても、新imageの未実施を合格へ変更しない。
#147はDebian package中断復旧の所有先で、Buildrootの更新方式をdpkg前提にする理由にはしない。

## 8. 今回の到達点と未検証

今回確認したのはremote master/PR/Issue状態、固定保存ref、文書・現master実装の調査、
Imager公式schema/customisation仕様である。Issue状態は確認時点のsnapshotで、後日再取得する。
文書上の要件・比較・再開計画が成果であり、#46の選定完了でもない。

host/containerでの新規build、image生成・静的検査、Imager実行・書込みは今回未実施。
Piのboot、CD-ROM、ALSA、CEC、DRM/KMS、sandbox実隔離、TV表示・試聴、
初回設定・更新復旧・電源断耐久性、製品受入も未実施。
既存branch/worktree/試作/cacheは破棄せず、commit/push/PR作成・Issue編集・公開も行っていない。

</details>
