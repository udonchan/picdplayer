# 残課題とIssue一覧

## 監査修正（2026-10-22）

[#257](https://github.com/udonchan/picdplayer/issues/257)で保存・WPE依存・統合状態の記述を
`c61faca`の実装へ合わせた。[#259](https://github.com/udonchan/picdplayer/issues/259)は
設定FIFOをopen待ちせず拒否する修正と回帰試験を追加した。両修正の統合・終了状態は対応PRとIssueを正本とする。
[#258](https://github.com/udonchan/picdplayer/issues/258)は旧`13485ee`上の未commit候補だけを対象とする
別branchの修正で、現行WPEや統合済みPR #244を再実装しない。今回の確認は
[検証状況](verification.md)を参照し、実機受入・配備の成功とは区別する。
PR #260でmaster `05a1a3e`へ統合された#257/#259は、PR #256にも取込済み。
2026-10-26の利用者指示でPR #256をmasterへ反映する。統合状態は対応PRを正本とし、
開始/取消/解放の限定確認と、受理PCM・指紋生成・音声等の未確認を分けて保持する。#238/#239はOpenを維持する。

2026-09-26の一覧を基に、2026-10-17にIssue・merge済み実装・設計・検証範囲を再照合した作業一覧。
実装の契約は[設計書](../README.md)、実測・確認範囲は[検証状況](verification.md)、
着手・進捗・完了条件は各GitHub Issueで管理する。Issueを閉じる際は仕様と検証記録も更新する。
各分野の一覧の行順は優先順位や実装順ではない。直近の順序は次の作業列だけで示す。実装済みと実機確認済み、
測定した条件と一般的な保証を区別する。

親Issueは子の未完了を一覧するだけなら維持せず、同じ残作業の所有先を一つにする。
Completedは限定scopeの完了、Not planned/supersededは未完了条件を移管したtracking終了として区別する。
方式が未決定の項目を採用済みの仕様として扱わない。
実機への.debデプロイは`scripts/deploy.sh`を使用し、設定済みのsudoersの範囲でSSH越しのサービス停止・起動・再起動を自動実行できる。
再生・停止等のdaemon操作は、SSH経由でPiのloopback HTTP APIへ送信できる。
ディスクの挿入・取り出しなどの物理操作、電源断を伴うcold boot、TVの実表示確認・試聴は手順を提示してユーザーに依頼する。
ビルドと自動試験はDockerで行い、遠隔操作の成功と実表示・音声の確認は区別する。

local委譲の規約補強と限定試行は[#210](https://github.com/udonchan/picdplayer/issues/210)で追跡する。
AGENTS.md §11に英語の追加規約を記載し、限定試行は初回と修正1回で終了した。
試行成果はfixture復元の条件未達であり、未完成テストは本流への反映対象外。
試行の事実と限界は[付録](reports/2026-10-08-local-agent-trials.md)を参照する。

## 当面の作業列と保留

2026-10-17のIssue整理単位では文書だけを変更し、実装・試験・Pi操作を行わなかった。
後続依頼による#121の修正・配備は別単位で記録し、本branchでは整理済み文書と配備済みコードを照合した。
当時のIssue整理は`docs/issue-cleanup-a1-a2`、診断修正の保存先はそのcommitを土台にする`fix/audio-first-diagnostics`だった。
2026-10-17のcommit/push承認はmaster直接更新・PR作成を含まず、作業中の未commit等は検証記録に履歴として残す。
元の未commit候補は`feature/audio-first-diagnostics`、配備用候補は`fix/audio-first-review`のworktreeに保持し、
Issue整理とコードのcommitを別単位にした。これらは作業時点の記録であり、整理と限定診断修正は
2026-10-17のPR #243（`1a44aaf`）でmasterへ統合済み。残る受入は下表のとおり。

| 順位・区分 | Issueと所有範囲 |
|---|---|
| 直近1 | #121: PR #243で限定診断修正は統合済み。固定packageでの限定受入と過去control/CEC観測の分類を残し、一括解決扱いしない |
| 直近2 | #184: 固定構成・時間・操作列での通常系受入。短時間成功を長期/全操作成功にしない |
| 独立した小修正の対応記録 | [#226](https://github.com/udonchan/picdplayer/issues/226): wrapper正本の待機契約を修正し、Linux Debug/Release各70/70・通常Debug配備・実待機45.840秒・設定復元を確認。統合/closeの現在状態はIssueと対応PRを参照 |
| 完了したv1表示 | #164: policy/strategy・長文表示と代表確認を完了してCompleted。追加hardeningは#250でv2以降へ延期 |
| 実装済み・限定受入待ち | #10: 補正試聴/TV端表示、#88: 観測可能な物理hotplug、#51: 実写真と音声/TV。物理リモコンは#252 |
| 取得・資源安全性 | #49: encoded上限とdecoded寸法/memory、memory-only/cache部品の保証範囲。TV表示は#51 |
| 後順位の現行課題 | #3: WPE起動時間、#14: CEC消失/claim、#83: 性能。#124: scrollbarはv1向けUI改修後の確認までPending |
| 完了したv1 cursor対応 | #28: WPE移行時に解消したという2026-10-19の利用者実機報告に基づき完了。Chromium/Cageはv1対象外 |
| 必要な条件が揃うまで保留 | #146: 傷/別drive、#147: 専用安全環境のpackage中断試験。失敗未観測を異常系合格にしない |
| v1実装・代表確認を完了した統合単位 | #177/#178: 保存/実service復元とSettings表示/API回帰を完了し、承認済みPR/必須CI経由の統合で終了。物理受入は#252/#51、通常運転は#184に残す |
| 利用者の実機操作待ち | #252: Player/Settingsの物理リモコン代表受入を一本化。#178/#51は同じrunを参照し重複試験しない |
| V2以降・非blocking | #254: WPE失敗表示/復帰・DRM/child cleanup。#219のv1移行は完了し、独自の未完了復旧だけを移管。#250のIntegrity hardeningとは別所有 |
| 機能開発の現単位 | #238/#239: local SHA-256と実session/API/Settings接続をPR #256でmaster反映対象とする。10月26日に実start/cancel受付・drive FD保持/終了・設定復元を限定確認したが、受理PCMは0で指紋生成/音声/再生復帰は未確認。#238/#239はOpenを維持し、配備・開始と取得完了を区別する。外部照合ではない |
| 将来の機能・設計 | #19: quiet boot、#20: read-only root、#42: Custom UI更新。外部provider互換profile/通信は未採用 |
| 正式供給・配布枠 | #45/#66/#67: release/license/package inventory、#220: v1.0供給精査。#46方式選定後の#47/#68と#69は保留 |
| 追加研究保留 | #198/#199/#200: Buildroot。#74の調査は完了、採用は未決定 |

写真切替の限定受入は#51、通常継続運転は#184、定量性能は#83、異常媒体/別driveは#146、
physical hotplugは#88、WPE固有の追加復旧はV2の#254に一本化する。同じrunを参照できても合格条件は混同しない。
#219はv1のWPE移行完了で終了し、未実施の失敗復旧・DRM/child受入は成功へ変更せず#254へ移管した。
#254は非blockingで現在着手せず、#184/#51/#252等の独立したv1受入をV2へ移したものではない。
背景保存要望は#177だけで追跡し、重複Issueは作らない。2026-10-19の#164終了では、既存Issueと重複しない
Integrity Monitor固有の追加hardeningだけを#250としてv2以降へ移管した。

## 実機評価・改善

| Issue | 主な範囲 |
|---|---|
| [#3 Investigate and reduce kiosk startup latency](https://github.com/udonchan/picdplayer/issues/3) | cold boot後のTV表示とtelemetryは確認済みだが、起動短縮は未解決。2026-09-25の一回の測定ではservice→wrapper約7.8秒、Cage exec→UI script受信約22.3秒、ui_ready受信はkernel起動後47.411秒だった。支配要因とTVのfirst pixelは未確定。 |
| [#4 Complete appliance runtime and endurance validation](https://github.com/udonchan/picdplayer/issues/4) | クローズ済み。通常CDの短時間API操作とdaemon再起動後の復帰は[実機記録](reports/2026-09-28-normal-runtime-api/README.md)に保存した。重複した総合チェックリストを廃止し、通常系の継続運転を#184、異常媒体・別driveを#146、hotplugを#88、性能測定を#83へ明示して引き継いだ。 |
| [#184 Validate normal kiosk lifecycle and extended playback on Raspberry Pi](https://github.com/udonchan/picdplayer/issues/184) | 最新構成の通常CDで、標準Playerと代表Custom UIの状態遷移、通常cold boot、TV表示・試聴、API操作、継続運転、journal・memory・温度を条件と時間を定めて実機評価する。物理リモコン入力の代表結果は#252を参照し同じ試験を重複要求しない。#177の設定保存後daemon再起動成功をcold boot/長期の代替にしない。#4の通常系に加え、#8の速度要求あり/なしの継続再生・聴感を引き継いだ。物理read faultや詳細性能比較は担当しない。 |
| [#27 Investigate and reduce kiosk CPU and thermal load](https://github.com/udonchan/picdplayer/issues/27) | クローズ済みの親Issue。[#52](https://github.com/udonchan/picdplayer/issues/52)の基線測定・完了判定の根拠は[測定記録](reports/2026-09-25-kiosk-baseline/README.md)へ反映済み。[#53](https://github.com/udonchan/picdplayer/issues/53)の描画改善、[#61](https://github.com/udonchan/picdplayer/issues/61)の残余負荷測定、[#62](https://github.com/udonchan/picdplayer/issues/62)のCustom UI向け注意事項は完了済み。最新構成の追加測定は[#83](https://github.com/udonchan/picdplayer/issues/83)、通常系の継続運転は[#184](https://github.com/udonchan/picdplayer/issues/184)へ移管した。条件と限界は[検証状況](verification.md)を参照する。 |
| [#83 Complete outstanding kiosk performance measurements](https://github.com/udonchan/picdplayer/issues/83) | Open。旧Chromiumの[5分記録](reports/2026-09-28-post-websocket-dedup-playing/README.md)は保全するが、現行WPEの比較値ではない。固定したWPE/背景/診断構成でSTOPPED/PLAYING・計測器負荷の反復基線を残す。Cage単独/旧CDP追加測定は比較需要がある時だけ、速度要求A/Bは本Issueの保留中の拡張比較。#25 blocked-byは解除、Closed #150は充足済み前提。通常運転・TV/聴感は#184。 |
| [#121 Investigate player main-loop stalls across stages](https://github.com/udonchan/picdplayer/issues/121) | Open、音声/負荷受入は未完了。低電圧flag 0でもapi_service約155 ms直後のunderrunを観測、先読み50 blocksは残っていた。診断owner分離・有界送信・503 retry修正はPR #243（`1a44aaf`、実装`e8889bf`を含む）で統合済み。保存branchを再cherry-pickしない。[最新統合記録](https://github.com/udonchan/picdplayer/issues/121#issuecomment-6243145479)と[当時の配備記録](verification.md#retry修正版debugの配備と読み取り確認2026-10-17-utc)参照。`0333ffbb…`は当時の旧packageで、今回#238の候補とは別。過去control/CEC原因、音声/CEC/長期/全操作を全面解決したとは扱わず、private rawは公開しない。 |
| [#122 Prevent duplicate WebSocket state delivery](https://github.com/udonchan/picdplayer/issues/122) | STOPPED中に同一snapshotを繰り返し送るAPI serverの不具合を#83のCDP測定で検出した。PR #123で接続ごとの送信済みgeneration追跡を追加し、当時のDocker 38/38とPiのSTOPPED 0 frame / PLAYING 40 frame確認を記録した。 |
| [#134 Eliminate timing flake in diagnostic isolation test](https://github.com/udonchan/picdplayer/issues/134) | クローズ済み。client切断後のPCM progressを固定20 tickではなくdeadline内で待つ試験へ修正し、Dockerで10回反復と全CTestを確認した。productionの再生・diagnosticsは変更していない。 |
| [#5 Evaluate read stalls and bound playback recovery](https://github.com/udonchan/picdplayer/issues/5) | クローズ済みの旧tracking Issue。子Issue #33はdeterministicな確認と物理試験の#146への移管を記録して終了し、#34は有界なread stall復旧を実装して終了した。傷・劣化媒体や別driveでの効果・音声影響は[#146](https://github.com/udonchan/picdplayer/issues/146)で未確認。未解決PCMの追加fallback方針は[#156](https://github.com/udonchan/picdplayer/issues/156)で検討する。旧親Issueの終了を実機異常系の検証完了とは扱わない。 |
| [#6 Benchmark CD-DA backends and read policies](https://github.com/udonchan/picdplayer/issues/6) | クローズ済み（採用見送り）。`PICDPLAYER_ENABLE_PARANOIA=ON`でのDocker/aarch64 package生成とCTest 39件は確認済みだが、Pi上の同条件backend比較・試聴は未実施。ライセンス・再配布条件の確認が必要なためparanoiaは公式配布へ採用せず、標準運用はdirectを維持する。依存監査は#66、現行構成の速度要求に関する定量比較は#83、継続運転は#184で追跡する。 |

## integrity仕様の実装

| Issue | 主な範囲 |
|---|---|
| [#7 Extend drive capabilities and validate C2 evidence](https://github.com/udonchan/picdplayer/issues/7) | Close済み。起動時のread-only capability probe、`YES / NO / UNKNOWN`とprobe failureの区別、C2 pointer observationの`c2_status`伝搬、通常CDでのdirect C2 pointer確認を完了した。capability/backendに応じたstrategy選択は[#155](https://github.com/udonchan/picdplayer/issues/155)、傷disc・別drive・C2 command failureの物理検証は[#146](https://github.com/udonchan/picdplayer/issues/146)、hotplug lifecycleは#88へ移管した。 |
| [#8 Add bounded drive speed control with fallback](https://github.com/udonchan/picdplayer/issues/8) | Close済み。`--drive-speed-x 1..255`の有界な要求経路、失敗記録、API/UI投影を実装・Docker確認済み。Piでは1x要求のioctl受理と20秒API再生を確認したが、実速度・騒音・効果は未確認。通常CDの継続運転・聴感・CECは#184、同条件throughput・CPU等の比較は#83、非対応drive/ioctl失敗は#146へ移管した。 |
| [#144 Stop optical drive spin after a bounded stopped idle period](https://github.com/udonchan/picdplayer/issues/144) | STOPPED/PAUSED中の15秒ごとの`CDROMSTART`回転維持要求を廃止し、TOC取得後にSTOPPEDが5分続いた時だけ`CDROMSTOP`を一回要求する。Docker回帰とPiでのrequest受理、TVでの自然停止、API再生復帰を確認済み。当時はeject・再挿入後のAUDIO_READY/STOPPED復帰も確認したが、現行の自動再生仕様は[#172](https://github.com/udonchan/picdplayer/issues/172)を参照。物理回転は観測しない。通常経路の完了によりCloseし、傷・劣化媒体、pause、unsupported/error、別USB drive/bridgeは [#146](https://github.com/udonchan/picdplayer/issues/146) へ集約する。 |
| [#9 Add overlap verification and cache independence evidence](https://github.com/udonchan/picdplayer/issues/9) | repeat verifierは直前に採用した15 CD frameの先行overlapを再読してPCM連続性を確認し、overlapを出力から除いて重複再生を防ぐ。不一致はfail-closed。反復・overlap一致でもdrive cacheを排除できないため`CACHE_POSSIBLE`を公開する。Docker自動試験と通常CDのPi repeat再生を確認し、[実機記録](reports/2026-09-28-repeat-overlap-normal-cd/README.md)を保存した。cache軽減手順、物理再読込の保証、傷discを含む異常媒体での検証は本Issueの保証に含めず、#146で追跡する。 |
| [#10 Track and apply CD read offsets with explicit coverage](https://github.com/udonchan/picdplayer/issues/10) | Open、実装済み・限定受入待ち。PR #233で明示CLI・根拠・正負補正・D window/端coverageを統合。[採用仕様](../design/integrity-design.md#10の採用仕様と実装)、[wire契約](../design/message-contract.md#read-offset)、[当時の検証](verification.md#10-read-offsetの実装と限定実機確認2026-10-16)参照。Linux標準66/66・最小38/38、最終同package/開始buffer45のPi6条件成功。途中underrun/USB異常も保持。本Issueは補正条件での試聴/TV端表示を残す。物理校正/別driveは#146の将来別承認枠、通常完走/継続は#184、stallは#121。一般再生報告を補正受入へ代用しない。 |
| [#162 Define explicit CD read-offset calibration inputs](https://github.com/udonchan/picdplayer/issues/162) | PR #161で設計完了・Closed。`logical p ← physical p + o`のstereo sample frame符号規約、明示入力、根拠、将来校正の境界を定義した。CLIと補正コードは含まない。MMC probeはoffset値を取得せず、vendor/model推定や無許可databaseを使わない。#41をHard dependencyにせず、実装・試験は#10、物理校正は#146へ分ける。 |
| [#11 Implement capability-aware read modes and fallback policies](https://github.com/udonchan/picdplayer/issues/11) | Close済み。single/repeatの未解決PCMはqueueへ入れずSTOPする初期方針を確定した。capability-based strategy選択・降格理由の公開は[#155](https://github.com/udonchan/picdplayer/issues/155)、任意fallbackの採否は[#156](https://github.com/udonchan/picdplayer/issues/156)へ移管した。#156ではSTOPを継続し、追加3候補を現時点では採用しない設計判断を記録した。read stall復旧は#34、物理異常試験は#146。 |
| [#156 Define optional fallback handling for unresolved PCM](https://github.com/udonchan/picdplayer/issues/156) | 設計完了・Closed。PR #234でSTOP継続とBEST_AVAILABLE/SILENCE/WAIT_WITH_BUDGETの現時点での不採用、由来・有限予算・最終STOP・再検討条件を統合済み。[設計判断](../design/integrity-design.md#unresolved-pcm-policy)と[公開契約との境界](../design/message-contract.md#unresolved-pcm-policy)、[当時の検証](verification.md#156-unresolved-pcmの設計判断2026-10-16)を根拠に、2026-10-17の承認済み整理で終了した。採用候補がないため追加実装Issueは作らず、代替PCM機能の実装・実機確認完了とは扱わない。 |
| [#155 Expose capability-based read strategy selection and downgrade reasons](https://github.com/udonchan/picdplayer/issues/155) | direct C2 pointer opt-inについて、requested/effective strategy、pending、machine-readable downgrade reasonをsnapshotへ公開する。probe前、capability UNKNOWN/NO、PLAYING/PAUSED中のreader再生成待ちを区別する。追加mode、未解決PCMの代替、read stall復旧、傷disc・別driveの実機検証は含めない。 |
| [#34 Bound playback recovery after read stalls](https://github.com/udonchan/picdplayer/issues/34) | effective ReadPolicyの`time_budget_ms`を一回のreader callのmain-loop監視上限にも適用する。超過時はioctlを強制中断せず、streamをSTOPPEDにしてworker threadで後からreaderを破棄する。inflight/timeoutはdiagnostic APIへ公開し、ALSA underrun復旧・RECOVERED・代替PCMと混同しない。傷disc・別driveでの評価は#146。 |
| [#109 Provide deterministic Integrity scenarios for Player UI review](https://github.com/udonchan/picdplayer/issues/109) | クローズ済みの親Issue。#110のtest-only `ScriptedCddaReader`と#111のlocal harnessにより、clean、retry、RECOVERED、UNCERTAIN、mixed map、read-ahead、状態遷移を通常のworker/API/Player asset経路で確認できる。production daemon/CLI/packageと実drive挙動は変更せず、実機異常mediaは#146で追跡する。 |
| [#12 Add bounded provenance and diagnostic event recovery](https://github.com/udonchan/picdplayer/issues/12) | #35/#36の実装と#89/#90の追加検証をPR #86/#87/#94/#95で整備。Docker36/36成功。全子Issue完了によりClose済み。実機異常系は#146、通常系の継続運転は#184、性能測定は#83へ分離する。 |
| [#13 Add optional external PCM verification](https://github.com/udonchan/picdplayer/issues/13) | Closedだがprovider/checksum/transportは未実装。PR #161は制約整理で、本文・未完了条件を機能完成とは読まない。MusicBrainz metadataはPCM照合ではなく、AccurateRipへの無許可アクセスは行わない。CTDBの全disc前提を部分再生で代用しない。#160の入力契約と実装・profile適合が全域照合の前提。再openやprovider採用は今回の対象外。 |
| [#160 Define complete-disc PCM capture for external verification](https://github.com/udonchan/picdplayer/issues/160) | 設計・作業分解完了としてClosed。PR #235で[入力契約](../design/integrity-design.md#complete-pcm-input)を統合し、A=#236/B=#237/C=#238/D=#239へ分割済み。子のproduction完成まで待つtracking Issueにはしない。[公開契約との境界](../design/message-contract.md#complete-pcm-input)と[当時の設計検証](verification.md#160-完全取得pcmの設計整理2026-10-16)参照。A/Bの限定test単位を経て、C/Dはlocal開発差分でsession/APIへ接続。外部provider・自動取得・PCM保存・実機全域取得の確認済みを意味しない。 |
| [#236 Implement complete PCM coverage and canonical input core](https://github.com/udonchan/picdplayer/issues/236) | A単位完了・Closed。PR #240でpure input coreとtest-only CMake対象を統合し、canonical全sample/channel、連続coverage、世代/取消/期限・consumer例外・二段階完了gateを自動検証済み。ASan/UBSan 71ケース、Linux標準67/67・最小39/39成功。[実装境界](../design/integrity-design.md#pure-core-implementation)と[当時の検証](verification.md#236-完全取得入力core2026-10-16)参照。当時はdaemon/reader/APIへ未接続だったが後続D開発差分でreuseする。実drive全域取得や外部照合確認済みとはしない。 |
| [#237 Implement an exclusive complete PCM acquisition worker](https://github.com/udonchan/picdplayer/issues/237) | B単位完了・Closed。PR #241で注入readerの専用owner・coordinator排他・発行抑止・call/job予算・取消/解放待ち・旧結果拒否をtest-only workerへ統合。ASan/UBSan・TSan各57ケース、Linux標準68/68・最小40/40成功。[内部契約](../design/integrity-design.md#acquisition-worker-implementation)と[当時の検証](verification.md#237-排他的取得worker2026-10-17-jst)参照。当時はdaemon/APIへ未接続で、後続D開発差分から利用する。実drive/Pi・外部provider未確認と#88の物理受入を成功へ変えない。 |
| [#238 Define and implement an approved incremental PCM checksum profile](https://github.com/udonchan/picdplayer/issues/238) | Open、C単位。network不要local SHA-256の固定state/disc-track digest/有界JSONとcanonical/core接続を実装し、known vector等を検証。実装経路はPR #256、統合状態は同PRを参照。実機の短いjobではPCM受理0のまま取消したため、指紋生成/全域完了は未確認でOpenを維持する。[採用profile](../design/integrity-design.md#local-pcm-sha-256-profile2026-10-19)参照。外部provider互換性/通信、#13のClosedを外部照合成功にしない。 |
| [#239 Integrate explicit complete PCM jobs with runtime validity and presentation](https://github.com/udonchan/picdplayer/issues/239) | Open、D単位。実session/raw factory・API/Settingsへ接続し、明示要求優先・media_activity・判定logを追加した。PR #256のmaster反映と実機受入は区別する。10月24日のUI限定代表確認、26日のstart/cancel受理・owner解放と同PIDのdrive FD0・offset未指定復元/一時許可撤去を記録した。ただし最初のdrive call内訳やraw read完了は未特定、受理PCM0、指紋生成/取消後再生復帰/音声は未確認。配色も残課題。[最新runと境界](verification.md#pr-256-初回start受理取消解放2026-10-26)を参照し、Issueはcloseしない。自動取得・外部照合・PCM保存は追加しない。 |
| [#92 Document the current playback and diagnostic message contracts](https://github.com/udonchan/picdplayer/issues/92) | 現行state/WS/詳細履歴のfield・型・単位・世代・順序・欠落・互換性を[メッセージ契約](../design/message-contract.md)へ整理済み。#89/#90は完了。#24正式化時に公開実装と再照合済み。 |
| [#146 Validate physical optical-media faults and USB drive compatibility](https://github.com/udonchan/picdplayer/issues/146) | 傷・劣化媒体、read stall、別USB optical drive/bridgeの実機評価を集約する。音声/操作影響とAPI・警告・復元の整合を同じrun記録で確認する。physical hotplugは#88、特殊TOCは#39/#40が担当する。 |
| [#97 Correct diagnostic buffer capacity and missing counter displays](https://github.com/udonchan/picdplayer/issues/97) | 診断画面の端数付きblock容量と欠損counterの0表示を修正。PR #95で回帰試験を追加しDockerで検証（Pi未再確認）。 |
| [#24 Integrate CD read integrity into the player UI](https://github.com/udonchan/picdplayer/issues/24) | #98/#99と監査修正#103〜#106はマージ済み。Phase 1ではscope付きsummary・有界disc read map・全read/drive値を常時表示し、実使用で情報量を評価する。primary playbackを優先し、daemon接続をIntegrity headerへ統合する。PR #108をマージしPhase 1はClose済み。通常Pi CDP確認済み。deterministicな異常scenarioの開発確認は#109で完了し、実機異常系は#146、通常系の継続運転は#184、性能測定は#83で追跡する。 |
| [#150 Reorganize Integrity Monitor information hierarchy and responsive layout](https://github.com/udonchan/picdplayer/issues/150) | PR #163でCompleted。通常CDのPi CDPとmixed fixtureの[比較記録](reports/2026-09-30-disc-read-map/README.md)を保全。policy/strategy選択表示と背景共存の状態別評価は#164。#83/#124の#150依存は充足済みで、旧親#25は残作業移管により終了した。 |
| [#164 Finish Integrity Monitor state presentation after artist backgrounds](https://github.com/udonchan/picdplayer/issues/164) | Completed（v1）。PR #245のpolicy/strategy、PR #248の長文名の縮小/末尾省略、代表自動試験・通常実WPE/実背景1枚の限定確認を完了。2026-10-19の利用者方針により、深い組合せと検証器hardeningは#250へ延期して終了した。[終了判断](verification.md#164-v1完了とv2以降への移管)参照。旧matrixはFAIL・未評価のまま、未確認を成功へ変更していない。実写真/TVは#51、通常運転は#184等の独立受入を維持し、それら全体をv2や完了扱いにしない。 |
| [#250 V2: Harden Integrity Monitor state combinations and fixture diagnostics](https://github.com/udonchan/picdplayer/issues/250) | Open、v2以降・非blocking・現時点では着手しない。#164固有の追加状態/背景/platform組合せ、非典型font/長文条件、fixture/network分類のhardeningだけを所有する。必要性とユーザー影響から小単位を選び、全直積・全OS/fontの保証を完了条件にしない。実写真#51、資源#49、通常運転#184、Settings#178、scrollbar#124、性能#83、保存#177の責務を重複させない。 |

2026-10-25の#239離席中監査では、`media_busy`の内訳を示すread-only `media_activity`と境界回帰を追加した。
標準WPE Release/Debug各72/72とprivate負例を検証後、同日の継続指示でdirty source snapshotを保全して診断版を配備した。
offset未指定の10秒/83標本でIDLE59/RUNNING23/RESULT_PENDING1を観測し、恒常的busyではないことを確認したが、
その後、利用者承認の一時offset0・単発startをownerへ渡したがHTTP409/capture_not_readyで受理されず、job0/cancel0のまま終了した。
元設定/offset未指定へ復元し専用helper/ruleを撤去した。通常再生は204で受理されたが観測scriptの誤りで予定5秒を約53秒に逸脱し、
STOP一度で停止を回復した。API進行測定/音声/取消後復帰は未検証で、製品側再生不具合とは断定しない。source/記録は未commitのまま、
[監査](verification.md#pr-256-media_busy監査と診断差分2026-10-25)と[診断配備run](verification.md#pr-256-診断版配備とmedia-activity観測2026-10-25)を参照。

その後の利用者指示で、明示APIを新しい定期observeより先に処理し、受理後の可否を再評価する調停を追加した。
標準WPE Release/Debug各72/72、API無効smoke、順序/古い可否のprivate負例を検証したが、
この優先順は同日23時台にsource snapshotを固定して通常配備し、設定/UI保持とread-only状態を確認した。
その後、別承認の一時offset0・単発startもHTTP409で拒否され、前後のstatusはRUNNING/media_busy・job0だった。
start1回/受理0・cancel0・play0で終了し、元設定/offset未指定へ復元、専用helper/ruleを撤去した。
修正版の実取得/競合率改善は未確認。start予約/自動再試行/gate緩和は追加していない。
[調停差分](verification.md#pr-256-明示要求優先の調停2026-10-25)と[配備境界](verification.md#pr-256-調停修正版の通常配備2026-10-25)を参照し、実取得成功とは区別する。

## 障害対応・機能改善

#239の2026-10-26診断では、判定時点log付き候補を配備し、offset未指定で6回の有限拒否専用入力を行った。
4POSTはmedia IDLE/offset_unknown、2POSTはRUNNING/media_busyで拒否され、恒常的POST拒否競合ではないことを確認した。
その後のoffset0単発試験では初めてstartが受理され、job1/COLLECTING/drive FD保持、1.326秒でcancel、
取消応答後約4.24秒でowner解放と同PIDのFD0を確認した。ただし受理PCMは0でfingerprint生成/再生復帰は未確認。
元offset未指定への復元と専用helper/rule撤去は完了した。[初回受理run](verification.md#pr-256-初回start受理取消解放2026-10-26)を参照。
[判定logと因果証拠の限界](verification.md#pr-256-判定時点logと拒否専用http診断2026-10-26)を参照する。

| Issue | 主な範囲 |
|---|---|
| [#14 Recover CEC device loss and bound address claiming](https://github.com/udonchan/picdplayer/issues/14) | 通常のCEC登録・操作・ARC復帰は実機確認済み。device消失後の再open、claim timeout、専有制御は未実装/検討中。 |
| [#15 Add explicit metadata release selection](https://github.com/udonchan/picdplayer/issues/15) | PR #170をmergeしてClose済み。同じDisc IDの複数候補をAMBIGUOUSで保持し、候補公開、世代付き選択API、選択後の非同期画像取得を実装した。Piの『The Slip』で実候補の公開と選択後metadata/cover URL反映も確認した。標準UIのCEC候補pickerは[#166](https://github.com/udonchan/picdplayer/issues/166)で実装・Close済み。現在discを越える選択の永続化は対象外。 |
| [#166 Select ambiguous metadata candidates from the Player with CEC](https://github.com/udonchan/picdplayer/issues/166) | #15の候補契約と#55のsemantic CEC入力を用いた標準UIの候補pickerをPR #171で実装した。曖昧候補は初回に自動表示し、back後は`Choose album`から開き直せる。Docker自動試験、Mac Chromeの合成fixture表示、Piの『The Slip』実候補選択・TV表示・初回自動表示とBack後の非再表示を確認した。2026-10-03にユーザーは候補選択後のcover画像をTVで確認した。後続の[PR #183](https://github.com/udonchan/picdplayer/pull/183)で選択後の再選択と`None of these`を追加した。PiでCECによる再選択とCDPによる辞退・Audio CD復帰を確認し、CECによる辞退と音声は未確認。異常系の実機確認は未実施。 |
| [#172 Start playback when an audio CD is inserted](https://github.com/udonchan/picdplayer/issues/172) | 候補picker #166とは別責務。daemon稼働中に明示的なdisc不在/eject後のAudio CDをTOC受理したら自動再生し、metadata候補選択を待たない。daemon起動時に既に挿入済みのCDやSTOP後のpollでは自動再生しない。Docker 48/48とPiで同一PIDのeject→再挿入→PLAYING、TV音声を再確認し、APIのSTOP後5秒もSTOPPEDを維持した。異常媒体や別driveは未検証。 |
| [#16 Harden metadata caching and HTTP input handling](https://github.com/udonchan/picdplayer/issues/16) | クローズ済み親Issue。#37でTTL・容量・atomic更新・破損cache無効化/再取得・offline fallback、#38でJSON入力上限・有界Retry-After・CAA redirect先host/IP制限を実装した。外部依存のHTTP統合試験とPi network確認は[#135](https://github.com/udonchan/picdplayer/issues/135)へ分離した。 |
| [#135 Validate metadata HTTP failure handling with deterministic mocks](https://github.com/udonchan/picdplayer/issues/135) | #38の実装後検証。test-only `MetadataOptions::http_get`で429/Retry-After、503再試行、CAA routing、cancel、connection failureからworker ERRORへの変換を確認。loopback TLS fixtureで実`HttpClient`のRetry-After、redirect、本文上限、timeout、private peer拒否をDockerで確認した。古いdisc世代のERRORをsessionが採用しないこと、worker待機中もhardware非依存の再生commandが進むことも確認済み。Piでは『The Slip』の一時cacheによるfresh MusicBrainz lookup、隔離daemonのUS候補によるfresh CAA cover公開、到達不能なHTTPS proxyでのenrichment ERRORと再生位置進行を確認。JP候補のCAAは404で画像なし、元のsystemd daemonとJP候補・再生位置は復元済み。実network断・timeout中の音声無欠落は未確認だが、#135の完了条件に長期安定試験は含めない。長期運転は#184、物理異常媒体・別driveは#146で保留する。 |
| [#17 Define metadata mapping for unusual TOCs and disc identity](https://github.com/udonchan/picdplayer/issues/17) | Not planned/supersededで親tracking終了。#39のTOC順mapping/fakeは完了、#40の観測限界設計も完了。非1始まりTOC/同一TOC別discの物理試験は未実施のまま。親の終了を全物理識別成功にしない。 |
| [#40 Define disc identity behavior for indistinguishable TOCs](https://github.com/udonchan/picdplayer/issues/40) | 設計Completed。詳細設計の同一TOC非識別/LOADING遷移時失効・反復では非増世代、MetadataSessionの世代拒否と同一TOC再要求fakeを受入条件へ対応付けた。識別不能な物理discを識別したと装わず、対象Piでの同一TOC試験は未実施。必要時の観測は#88、通常再挿入は#184を参照する。 |
| [#25 Add artist backgrounds as progressive player enrichment](https://github.com/udonchan/picdplayer/issues/25) | Not planned/supersededで親tracking終了。#48/#50は完了。取得/資源安全性#49、写真受入#51、保存#177、Settings#178、Integrity表示#164、性能#83、通常継続#184へ一本化。機能全体完成ではなく未実施を保持。Custom UIへの自動注入・写真同梱は行わない。 |
| [#229 Validate fanart.tv artist backgrounds with local developer keys](https://github.com/udonchan/picdplayer/issues/229) | Not planned/superseded。PR #230の実API認証/JPEG3枚/OFF失効/WPE写真snapshot/基本再生報告と制約を保全。未確認の実写真切替・TV可読性・音声/代表CECは#51、長期は#184、性能は#83、decoded安全性は#49へ移管。全PoC受入の合格ではない。[PoC記録と残所有先](fanart-tv-poc.md)参照。key/画像/private証跡を公開しない。 |
| [#49 Finish artist background acquisition safety and bounded resource guarantees](https://github.com/udonchan/picdplayer/issues/49) | Open、取得経路はPR #176/#223/#230で統合済み。encoded上限とdecoded寸法/memory、memory-only runtimeとcache部品の保証範囲を仕上げる。TV/写真切替は#51。画像利用判断は利用者に委ね、包括的許諾を製品が保証しない（[検証ノート](verification.md#fanarttv画像の権利条件2026-10-02再確認)）。runtime disk cache・Personal key・追加providerを必須実装に積み増さない。 |
| [#177 Persist user settings for read policy and artist backgrounds](https://github.com/udonchan/picdplayer/issues/177) | PR #253（`75df663`）でmaster統合済み、v1 Completed。一組保存・旧schema読込・利用不能時ON復元・OFF保存失敗でも停止を実装。当時の標準全CTest70/70、最小41/41、metadata非compile対象3/3、package lifecycle、Pi実serviceの一回再起動による両設定復元は[記録](verification.md#177178-背景選択の保存とoff未保存契約2026-10-19)参照。即時終了assert失敗と後続GETの復元確認を区別する。通常cold boot/継続運転は#184、物理リモコンは#252。電源断耐久性は未保証。 |
| [#50 Support progressive delivery of visual enrichment](https://github.com/udonchan/picdplayer/issues/50) | Completed。PR #175/#223/#230のmetadata先行・artwork/背景の独立status・世代照合と1→2→3枚の実HTTP/WS回帰を根拠に限定配信scopeを終了。通常全network条件での音声無欠落や長期保証ではない。取得#49、写真受入#51、通常継続#184へ残す。 |
| [#51 Complete real-photo acceptance of Player backgrounds and slideshow](https://github.com/udonchan/picdplayer/issues/51) | Open、PR #223/#230で実装済み・実写真受入待ち。fallback、0/1/複数、shuffle、45秒/4秒、reduced motionとDOM回帰は統合済み。#229の残受入を継承し、実写真切替/時間条件、TV可読性、実CD音声を固定条件で確認する。物理リモコン入力は利用者指示で#252に集約し、同じrunを参照して重複要求しない。長期CPU/memoryは#83/#184に一本化し、取得安全性#49全体のblocked-byは解除。Custom UIへ自動注入しない。 |
| [#214](https://github.com/udonchan/picdplayer/issues/214)、[#215](https://github.com/udonchan/picdplayer/issues/215)、[#216](https://github.com/udonchan/picdplayer/issues/216) | 各限定scopeでCompleted。隔離Pi Chromium合成decode/fallback/失効とcleanup、metadata labelのDOM mutation 1→0修正、実HTTP/WS合成component fixture。旧#217からPR #223への成果継承を確認。元の固定SHA・失敗・artifact記録を保持し、WPE実写真/光学/ALSA/音声/CEC/長期受入の証拠へ変更しない。 |
| [#48 Expose selected artist identity for enrichment](https://github.com/udonchan/picdplayer/issues/48) | PR #174をmergeしてClose済み。選択済みreleaseのartist-creditから、単一の有効なArtist MBIDだけをEnrichment内部へ渡す。複数IDはAMBIGUOUS、欠損・不正・Various ArtistsはUNAVAILABLE。外部背景provider、Viewへの画像契約は#49/#51へ分離する。 |
| [#28 Hide unwanted cursor in the current kiosk while preserving useful input](https://github.com/udonchan/picdplayer/issues/28) | v1完了。2026-10-19、利用者がWPE移行時の解消を報告しcloseを指示。Chromium/Cageはv1対応・受入対象外。旧Cage調査とWPE PoCのcursor残存観測は履歴として保全し、全状態・cold boot等の旧matrix成功へ読み替えない。[終了根拠](verification.md#28-wpe移行によるv1完了2026-10-19)参照。 |
| [#124 Investigate unintended kiosk scrollbar display](https://github.com/udonchan/picdplayer/issues/124) | Open / Pending。2026-10-19の利用者方針により、まだIssue化していないv1向けUI改修後の再確認まで保留。旧TV目視/DOM overflow非検出を保全し、修正済み・解消済みとはしない。再開時は固定WPEのTV/PNG/DOMを照合する。#25全体のblocked-byは解除済み。 |
| [#31 Add CEC-driven controls to the Player view](https://github.com/udonchan/picdplayer/issues/31) | Close済みの親Issue。[#54 loopback操作契約](https://github.com/udonchan/picdplayer/issues/54)、[#55 CEC navigation配信](https://github.com/udonchan/picdplayer/issues/55)、[#56 Player操作UI](https://github.com/udonchan/picdplayer/issues/56)に分割済み。#54はPR #168、#55はPR #167、#56はPR #169をmergeしてClose済み。Piで上下左右・決定・Back、再生/音声・一時停止・停止、右方向キー約2秒長押し後の選択停止を確認した。CECとChromium keydownの二重入力を#56で抑制した。別TV・入力切替時の挙動は未確認。metadata候補pickerは[#166](https://github.com/udonchan/picdplayer/issues/166)で別途扱う。 |
| [#178 Add a CEC-accessible Player settings screen](https://github.com/udonchan/picdplayer/issues/178) | PR #253（`75df663`）でmaster統合済み、v1 Completed。PR #180/#223の画面へ背景自身の保存/未保存案内とpolicy保存後の再GETを追加。当時のsemantic入力だけのSettings→ON/OFF→Back/focus試験、実WebSocket6 actionとPi配備確認は物理CEC→Settingsの自動E2Eではない。v1物理受入は未完了で#252へ集約し、#256のSettings限定代表確認で全体完了にはしない。保存は#177、写真/TV/試聴は#51。 |
| [#252 Complete v1 physical-remote acceptance for Player and Settings](https://github.com/udonchan/picdplayer/issues/252) | Open、利用者が実機近くで操作できる時に着手。2026-10-19の利用者指示で物理リモコンの唯一の代表受入記録先とする。Settings開閉/選択/Back/focus、短い再生/停止とpolicy pending、利用可能時ON/OFFを基本10分・初回＋修正一組で確認し、STOPPED/SINGLE/背景OFFへ戻す。#51同時runのCEC部分もここを参照。#256の2026-10-24 Settings限定代表確認は[同じ記録](verification.md#pr-256保存点2026-10-25)を参照し、本Issueの全scopeや音声の合格に拡張しない。開始時に時間・操作・復元を再確認し、新自動化基盤や全TV matrixを追加しない。未実施を合格扱いにしない。 |

### 過去の統合・配備経緯（2026-10-13時点）

以下は当時のOpen/未統合/未確認を保持する履歴。現在の状態は上の各行と2026-10-17の整理記録を参照する。
2026-10-13の統合準備では、Draft #217の既存画像worker・共有上限・fixture試験と候補workerの
thread宣言順修正をWPE worktreeの開発差分として再利用した。CMakeへの最小登録だけを
行い、PR全体の競合解消・mergeやruntime ONは行わない。前回の背景Settingsは保持する。
詳細と今回の検証範囲は[検証状況](verification.md#背景画像workerの統合準備2026-10-13-utc)を参照する。

その後、利用者指示によりSettings・worker部品・保持済みWPE文書をlocal commit `699f627`へ保存した。
今回の開発branchでは、Draft #217の単一画像runtime・API・optional snapshot参照・標準fallbackを
再利用し、前回のSettingsと接続する。GitHubのmaster統合とは区別する。#25/#49/#50/#51/#177/#178は
Openを維持し、slideshow・背景保存・full decode memory上限・通常daemon/WPE/Pi確認は残る。
記録は[検証状況](verification.md#単一画像runtime接続の開発差分2026-10-13-utc)を参照する。

利用者指示でこの試験packageをPiへ配備し、package・稼働service・WPE 2.54.0の同一性を観測した。
利用者が実機CEC操作・再生→停止・backgroundのON不可を確認した。背景APIの`key_missing`と
整合するが、写真の取得/表示・取得中の再生継続・OFF時の消去は未確認。鍵設定と明示ONを含む
写真試験へ残す。[配備記録](verification.md#単一画像runtime試験packageのpi配備2026-10-13-utc)参照。
利用者の方針によりAPI key設定は後続の残課題とし、今回のcommit/pushの前提条件にはしない。
今回の公開単位は単一背景runtimeとSettings接続であり、#25全体や写真の実機受入完了とは扱わない。
旧Draft #217/#192/#194は単一の後継PRとmerge済み#222で実装提案を置換し、固有の履歴・試験toolは
[固定commit参照](verification.md#旧draft-prの資料保全と統合経路2026-10-13-utc)で保存する。
PRのcloseで関連Issueの残条件を消さず、元branch/worktreeとSDK cacheも自動削除しない。

## 設計を先に確定する作業

| Issue | 主な範囲 |
|---|---|
| [#18 Specify persistent settings and custom UI updates](https://github.com/udonchan/picdplayer/issues/18) | Not planned/supersededで親tracking終了。#41の設計はCompleted、更新設計#42は需要待ち保留、保存#177・画面#178へ一本化。現行Custom UIの起動時静的検証/fallbackを未実装へ戻さない。 |
| [#41 Specify persistent configuration and apply boundaries](https://github.com/udonchan/picdplayer/issues/41) | #179で先行実装したRead Policyの優先順位、保存失敗時、起動時fallback、停止境界を機能・詳細設計へ整理した。Artist Backgroundの既定OFFと運用者設定との境界も定義した。保存/API/画面の完成は#177/#178、Custom UI更新は#42が担当する。 |
| [#21 Plan reproducible releases and appliance images](https://github.com/udonchan/picdplayer/issues/21) | Not planned/supersededで親tracking終了。#43/#44の開発.deb/通常lifecycleはCompleted、正式公開#45、方式#46、製品image#47、compliance#66〜#68、中断復旧#147へ残条件を一本化。正式配布/imageの完成ではなく、最終imageに開発.debを使うかも未決定。 |

## 未実装の製品機能

| Issue | 主な範囲 |
|---|---|
| [#19 Add quiet boot with a diagnosable failure path](https://github.com/udonchan/picdplayer/issues/19) | 未実装・保留。quiet boot/splashと失敗診断の必要性を決めてから着手し、#3の起動短縮や音声安定性の必須依存にしない。 |
| [#20 Design and validate a read-only root deployment](https://github.com/udonchan/picdplayer/issues/20) | 未実装・保留。#46で必要性・filesystem/更新復旧方式を選定してから、cache/profile/journal/設定/Custom UIの書込先を設計する。 |

## OSSライセンスと配布物の追跡

最終目標はソース公開だけでなく、実際に配布するbootable imageのOSS構成を継続的に追跡すること。
現在は`stage/`と開発用`.deb`をCMake install規則から生成し、Piへdpkgで導入する。
専用bootable imageと公開release workflowは未実装である。

#45の限定実装として、既存CIのbuild・CTest・package lifecycle成功後に版・commit・checksum・stage一致を
検査した候補を短期保存する経路を追加した。公開release・署名・ライセンス条件の完了ではなく、#45は未完了を維持する。
手順と検査の限界は[Mac + Docker開発手順](../manual/mac-docker-development.md#版付き候補artifactの検査と保存)を参照する。

| Issue | 段階と残る作業 |
|---|---|
| [#66 Establish project licensing and audit direct dependencies](https://github.com/udonchan/picdplayer/issues/66) | Phase 1。本体のApache-2.0案、直接依存、optionalなlibcdio-paranoiaの配布条件を監査する。対応containerでのlibcdio-paranoiaはGPL-3-or-laterと確認し、`ENABLE_PARANOIA=ON` configure時に再配布前の確認を促す警告を追加した。本体ライセンスと依存監査文書は未確定。 |
| [#67 Track Debian package contents and distribution metadata](https://github.com/udonchan/picdplayer/issues/67) | Phase 2。#66を入力に、`.deb`の内容・runtime依存・copyrightを追跡可能にする。独立control inventoryは[PR #212](https://github.com/udonchan/picdplayer/pull/212)でSolが補修し、11個別試験・全CTestを確認した。既存manifestへの接続・配布metadataは残り、Issue全体は未完了。Qwen初版の不合格記録は付録とコミット履歴に保持する。 |
| [#68 Generate compliance artifacts from bootable release images](https://github.com/udonchan/picdplayer/issues/68) | Phase 3。#67と検査可能な#47のimageを入力に、最終image実体のinventory・SBOM・notice等を生成する。 |
| [#69 Integrate compliance metadata with an embedded build system](https://github.com/udonchan/picdplayer/issues/69) | Phase 4。Buildroot/Yocto等への移行が決まった場合のみ着手する将来候補。 |

## 採用条件が整うまで保留する候補

次は現在の不具合や実装必須項目ではない。必要性と対象が決まった時点で独立Issueを作る。

| 候補 | 保留理由・着手条件 |
|---|---|
| S/PDIF・外部I²S・複数出力profile | [出力拡張案](digital-audio-output.md)。対象hardwareと利用目的が未決定。現行HDMIのbit-perfectも保証しない |
| AsyncLoggerのlibrary置換 | 現状の要件を満たす。追加sink・runtime level・rotation等が必要になった時にサイズ・依存・queue/flushを比較 |
| mixed-mode・負LBA・隠しtrack・CD-TEXT | 現行は音声のみのCDが対象。対応discと用途が決まった時にTOC契約・試験を追加 |
| 外部公開APIの認証・TLS | 現行はloopback操作と信頼する開発LANの診断用途。公開範囲を拡張する要件が決まった時に設計 |

ジャケット画像のbinary cacheとsame-origin配信は[#23](https://github.com/udonchan/picdplayer/issues/23)で
実装済み。破損・容量・offline運用等の未検証事項は[検証状況](verification.md)を参照し、
未実装候補として重複掲載しない。

## 以前の整合性確認（2026-09-25）

README、Guide、Design、Manual、Developmentと、History/旧パスの案内・リンクを確認した。
履歴の当時の数値・判断は現行仕様に合わせて書き換えない。

- integrity拡張案を[設計仕様](../design/integrity-design.md)へ統合し、現行型・未実装要求・暫定値を区別した。
- 通常の開発をMac + Docker、Piをruntime検証に統一し、Linux単体ビルドは補助手順として残した。
- CMakeの試験登録条件とCIを照合し、26件だった過去の結果とPython追加後の29件を区別した。
- main loopのengine tick頻度、LOADING後のmetadata再要求、能力probeの待ちと再取得の限界をコードに合わせた。
- UI telemetryとCustom UIのAPI記述、cold boot・日本語表示・journald・underrun復旧の確認範囲を更新した。

この整理で新たな実機試験を行ったことにはしない。未確認事項は各Issueの完了条件として残す。

## WPE主runtimeへの移行

WPEを主runtimeとする標準build/package/deployをPR #222でmasterへ取り込んだ
（`f0d44c1`）。Chromiumの明示選択・fallback実装は保持し、実機復旧の確認済みを意味しない。
標準Linux/aarch64 build・CTest 58/58・package lifecycleとPR CIの成功を確認した。
利用者が標準packageをPiへdeployし、TVリモコンによるアルバム選択・再生・停止を確認した。
sudoなしのprocess観測でもWPEがMainPIDで、Chromium fallbackではないことを確認した。
詳細と未検証範囲は[検証状況](verification.md#wpe標準packageのpi更新cec操作確認2026-10-13-utc)を参照する。

| Issue | 主な範囲 |
|---|---|
| [#219](https://github.com/udonchan/picdplayer/issues/219) | Completed（v1のWPE移行範囲）。WPE主系/標準packageはPR #222、Inspector PNGは#231、Debug/Release分離は#232で統合済み。PR #253でも標準WPEの配備を確認。2026-10-19の利用者指示で、独自の失敗表示/復帰・異常終了時DRM/child cleanupと旧#188からの未完了条件をV2・非blockingの#254へ移管してcloseした。旧matrixの未確認を全件成功へ変更しない。通常cold bootは#184、物理リモコンは#252、写真/TV/試聴は#51、正式供給は#220のまま。[終了判断](verification.md#219-v1移行完了とv2復旧への移管2026-10-19)参照。 |
| [#254 V2: Harden WPE failure recovery and DRM cleanup](https://github.com/udonchan/picdplayer/issues/254) | Open、V2以降・非blocking。初回document取得失敗と表示後API/WS切断を区別した説明可能な表示/有界復帰、異常終了/再起動時のDRM解放・排他/child cleanup/音声非阻害だけを#219から継承。現在は実装・試験を開始しない。既存のTCP待機やsystemd設定・fixtureを全実機受入へ読み替えず、再開時に代表scope・時間・復元を合意する。Chromium/Cage受入や新CEC基盤を暗黙の必須条件にしない。#226の待機修正は完了済みで再実装しない。 |
| [#188 Prevent Chromium error pages from remaining on the kiosk display](https://github.com/udonchan/picdplayer/issues/188) | Not planned/superseded。未実装/未受入の失敗表示・復帰要求は#219を経てV2の#254へ移管。WPE採用だけで全失敗復旧を解決したとは扱わない。Chromium/Cageはv1受入対象外だが既存選択/fallbackコードは削除しない。 |
| [#220](https://github.com/udonchan/picdplayer/issues/220) | Open。source/version/2patchの開発供給と標準package/SDK cacheは成立済み。v1.0へ保留した依存固定・ABI・sandbox実隔離/権限・更新/正式供給を旧#189/#191/#193から集約。sid rootfsをhost混在/製品供給へ転用しない。 |
| [#221](https://github.com/udonchan/picdplayer/issues/221) | 成立性調査としてCompletedでClose。SHA256固定2.54.0＋VIDEO guard/seccomp linkの2patch付きTrixie/arm64構成でconfigure/build/stage、pkg-config、launcher/processのlink/依存検査を記録し、#222〜#225の後続CIで成果を保全・検証。無改変upstream・完全再現性・正式供給とは区別する。移行#219は完了、追加復旧はV2の#254、正式供給は#220を参照する。 |
| [#226](https://github.com/udonchan/picdplayer/issues/226) | 限定scopeの実装・検証記録。master `1a44aaf`から分離し予算照会とmonotonic期限を実装。mock・negative control・実GNU timeout、Linux/aarch64 WPE Debug/Release各70/70・package回帰・レビュー修正確認済み。通常Debug配備に加え、2026-10-18の利用者の対話実行で一時TCP待機45.840秒・実verifier exit 0・実WPEと設定復元を確認。先行sudo認証中断は履歴として保持。統合/closeの現在状態はIssueと対応PRを参照。実APIを停止する遅延、実機の上限/失敗matrixや試聴・長期受入までは含めない。 |

2026-10-15のprofileレビューではarchiveと実binaryのidentity照合、生成wrapper試験、実multi-config拒否試験を補修した。
既存のAPI無効daemon fixtureもAPI専用部分だけ分離し、最小構成37/37を確認した。
4 build typeは各65/65とpackage lifecycleを隔離containerで確認済み。後続の承認済み実機deployでは
Debug→Release切替、loopback Inspector接続/PNG、Releaseのport閉鎖・INFO抑制、API/WSとCEC登録を確認した。
この2026-10-15単位では実音声・物理入力・背景ON実表示・長期再生は未検証であり、当時#219はOpenを維持した。
詳細なcommand・exit code・混在条件は[検証記録](verification.md#debugrelease-profile分離2026-10-15)を参照する。

2026-10-14の整理では、#189は採用評価・意思決定、#191は最小direct DRM PoC成立と残条件移管の
CompletedとしてCloseした。旧#190のCage＋Cog比較と#193のopt-in sid trialはNot planned/supersededで
Closeし、未完了checkboxを一括成功へ変更していない。固有の観測・失敗・toolは元コメントと
[固定commit保全表](verification.md#旧draft-prの資料保全と統合経路2026-10-13-utc)から追跡する。
性能/長期測定は#83、背景写真・key/decode memory/保存/slideshowは#25関連、Buildrootは保留のまま。
この整理結果のrepository文書への統合は[#227](https://github.com/udonchan/picdplayer/issues/227)で追跡し、
統合完了を#219/#220/#226などの残タスクの完了とは扱わない。

Buildroot採用は前提としない。sudoが必要な実機操作は利用者が行い、診断toolからserviceの
切り替えやsudoers変更を行わない。既存PoCの確認と新しい本番serviceの確認は区別する。
開発基準は今回のWPE 2.54.0・2patch・設定とする。主要依存36packageはPiに導入済みで、
追加の依存/ABI精査、clean rebuild、最終供給環境の完全固定はv1.0確定段階まで保留する。
最終固定はBuildroot検討再開時に評価し、現時点の開発・初回実機試験を待たせない。
具体的不具合が出た場合だけ必要な範囲を調査する。これは#220の正式供給完了ではない。
標準build/packageはWPEを有効化し、runtimeを専用prefixへ同梱する開発用供給経路を実装した。
deployはversionとactive kioskのMainPIDを検査する。通常deployのsudoers追加は不要。
SDK cache・package lifecycleはcontainer検証済み。2026-10-13 UTC、利用者が標準packageを
Piへdeployし、TVリモコンでアルバム選択→再生→停止を確認した。WPE稼働とpackage所有も
sudoなしで確認した。cold boot・DRM解放・異常終了fallback等は引き続き未確認。
2026-10-13 UTC、利用者の方針により、開発中はWPEを主runtimeとして進め、復旧の追加検討は
必要になった時点へ保留する。既存の明示Chromium選択/fallback実装は保持するが、追加の実機
復旧試験を現段階の取り込み条件にはしない。未確認を確認済みとは扱わない。
当時のレビューでdeployのruntime待機上限35秒とkioskのAPI待機設定上限300秒の不一致を確認した。
長い待機設定では正常起動前にdeployが失敗を報告し得る。当時はコード未修正で、背景機能とは別の#226へ移管した。
後続作業の現在状態は上の#226の行と検証記録を参照する。

## Buildrootの検討成果保全と採用保留

2026-10-12、利用者の判断によりBuildroot採用・追加実装を保留した。成果は破棄せず、
文書をmasterへ統合し、試作コードは元PR/remote branchの固定headに保持する。
現行Debian/container/.deb開発経路は変更しない。親[#74](https://github.com/udonchan/picdplayer/issues/74)は
当時Openの再開索引としていたが、2026-10-17に調査・判断材料・作業分解の限定scopeでCompletedとした。
再開索引はこの節・[適合性report](reports/2026-10-05-buildroot-roadmap/README.md)・#198〜#200へ保持する。
#75〜#78は調査結果の保全で、方式採用とは区別する。追加3研究はOpen保留のまま。

| 成果 | 元PRと保存head | 現在の境界 |
|---|---|---|
| [公式構成・Pi image経路](reports/2026-10-05-buildroot-official/README.md) | #195、`483b81c`、`docs/75-buildroot-official` | 文書のみ。image/実機未検証 |
| [OSS事例比較](reports/2026-10-05-buildroot-examples/README.md) | #196、`0c8ce59`、`docs/76-buildroot-examples` | 文書のみ。Batocera/OpenMikoを採用したことにはしない |
| [runtime要件](reports/2026-10-05-buildroot-runtime/README.md) | #197、`14700cf`、`docs/77-buildroot-runtime` | 当時のコード要件。再開時に現行実装と再照合 |
| [適合性・作業分解](reports/2026-10-05-buildroot-roadmap/README.md) | #201、`97d834b`、`docs/78-buildroot-roadmap` | #198〜#200を作成済み。採用判断は保留 |
| [Pi 3 image生成](reports/2026-10-06-buildroot-pi3-probe/README.md) | #202、`4129757`、`research/198-buildroot-pi3` | image生成は過去に確認、実機boot/CD-ROM/ALSA/CEC/DRM未検証。#198はOpen保留 |
| [package build/install](reports/2026-10-06-buildroot-package-probe/README.md) | #202、`4129757`。別branch `research/199-buildroot-package`の`173e088`も同headに含む | local source/UNKNOWN license/BusyBoxとunitの不一致が残る。#199は製品供給完了とせずOpen保留 |
| [WPE供給probe](reports/2026-10-06-buildroot-wpe-probe/README.md) | #203、`a2c6699`、`research/200-buildroot-wpe` | 依存build/configureまで。最終build/実機未確認、#200はOpen保留 |

試作Dockerfileは隔離hostに有用だがapt版が固定されていない。package recipeは`/src`のlocal checkoutと
`LICENSE=UNKNOWN`を使い、最小BusyBox imageではsystemd unitが起動しない。WPE recipeはupstreamの大きな
試作更新で、最終build・sandbox・font/cursor・service統合が未確認である。いずれも現在の正式build/CI/runtimeへ
採用する根拠が不足するためmasterには取り込まず、READMEから固定SHAの元コードへリンクする。
生成image/volumeの存在を今回確認しておらず、missing/prunableな旧worktree登録だけを成果保管とは扱わない。
remote headとlocal commitを照合し、元branchにない未push commitは確認されなかった。local worktree/branchは削除しない。

再開条件は、#46でimageの対象・filesystem・更新/rollback・永続領域とbrowser runtime要件が具体化し、
現行方式との比較が必要になった時点である。Buildrootの版・source固定・ライセンス・再現環境を再評価し、
#198〜#200の未検証部分から再開する。#47の製品image、#66〜#69のcomplianceをこの調査で完了扱いにしない。
WPE runtimeの#192/#194は別の検討で、今回の保留・PR整理の対象には含めない。

## 監査で確認した不整合・回帰

WPEのCI cacheはmaster側の共通SDK専用workflowを開発し、実GitHub PoCを進める。PR #223のCIは2時間31分29秒を要し、
既存cacheは旧feature/PR merge refに限られていて、新しいPRから利用できるmaster cacheはなかった。
SDKだけをtrusted masterで準備・保存し、通常のアプリbuild/test・既存SDK recipeは維持する。
workflowは#224でmasterへ統合し、master scopeの初回seed（2時間21分12秒）は成功した。
新規PR #225の最初の通常CIでmaster cacheを復元し、62/62 CTest・package lifecycle・候補検証を省略せず
jobを3分30秒で完走した。cold #223の2時間31分29秒から97.69%短縮した単回PoCとして記録する。
cache消失/recipe変更時のcold fallback、正式供給や#219/#220の
全条件を完了扱いにはしない。[運用手順](../manual/mac-docker-development.md#master側の共通wpe-sdk-cache)参照。
PoCは導入CI→master seed→別branch/新規PRの通常CIの順とし、全試験を省略せず、cache復元と時間短縮を
実測する。初回seedとwarm CIの時間を混同せず、SDK cache成功を実機動作や再現性の保証にしない。
[実測証拠と限界](verification.md#github初回seedと別pr-poc2026-10-14-utc)を参照する。

[#79 文書整合](https://github.com/udonchan/picdplayer/issues/79)、
[#80 CDP接続・trace集計](https://github.com/udonchan/picdplayer/issues/80)、
[#81 診断API・status表示](https://github.com/udonchan/picdplayer/issues/81)を追跡する。
修正の検証範囲は[検証状況](verification.md)を参照する。

## 物理drive hotplug（観測実装・物理受入保留）

[#88](https://github.com/udonchan/picdplayer/issues/88)で物理交換/reset検出・能力失効・再取得を追跡する。
#7の物理lifecycle部分を分離した項目。PR #242で[観測世代/失効/再probe/旧操作拒否](../design/integrity-design.md#device-lifecycle-observation)をmaster統合済み。
#35のreader-open counterと別に扱い、#24の追加Hard dependencyにはしない。native ASan/UBSan/TSan、Linux標準69/69・最小41/41を確認した。
新Debugの通常再生/API/WS/DOMの機能assertは成功したが、再生中undervoltageを後解析で発見し追加負荷を停止した。
電源安定性、node/sysfs samplingの検出不能ケース、物理抜き差し/reset・異常媒体/別driveの実機受入は未完了。
[検証記録](verification.md#88-device-lifecycle2026-10-17-jst)参照。
Issue状態はOpenのまま、C/D/provider・物理操作・Issue closeは今回の対象外。

### 全ディスクread mapの前提

- [#98](https://github.com/udonchan/picdplayer/issues/98): TOC座標とdisc観測世代のdisc.layout公開はPR #101でマージ済み。Docker36/36、通常PiのTOC/REST/WS照合済み。
- [#99](https://github.com/udonchan/picdplayer/issues/99): 最大256区間のDISC集計を実装。PR #102でマージ済み。Docker36/36、Piで140 read超・stop/resume保持・daemon restart resetを確認。

#98/#99は完了済みの前提。#24の標準Player consumerはPR #108で完了済み。相互はRelatedで、#12の完了を取り消さず追加機能として管理する。

## #24着手前の監査修正（PR #102）

- [#103](https://github.com/udonchan/picdplayer/issues/103): stream終了時の旧再生根拠・欠落counter破棄。
- [#104](https://github.com/udonchan/picdplayer/issues/104): disc/session変更時の同URL artwork再取得。
- [#105](https://github.com/udonchan/picdplayer/issues/105): 最終throttled sample欠損時の集計。
- [#106](https://github.com/udonchan/picdplayer/issues/106): 終端underrunの再seek防止。

いずれもPR #102でマージ済み。Docker37/37成功。
実機では修正前の終端試験が失敗したため、その後の通常再生だけを確認した。修正版で13秒間の
通常再生・API stop・エラーなしと、TVの表示・音声再生は確認済みだが、終端drainは未確認。
[監査記録](reports/2026-09-26-pre-integrity-audit/README.md)を参照する。

## 2026-09-27変更の監査

- [#115](https://github.com/udonchan/picdplayer/issues/115): 未比較overlapの不一致誤表示、拒否候補の採用識別子をPR #117で修正・Docker確認済み（マージ待ち）。#9の実機検証は別途継続。
- [#116](https://github.com/udonchan/picdplayer/issues/116): #24完了、速度要求、世代・履歴公開、READMEとCI検証範囲の文書反映漏れをPR #117で修正（マージ待ち）。
