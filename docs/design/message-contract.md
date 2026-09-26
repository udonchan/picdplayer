# 現行の再生・診断メッセージ契約

照合対象はmaster `0f8e12f`、公開`schema_version=1`。現行実装を記述するもので、将来不変のAPIを宣言しない。
公開メッセージのfield表は本書を正本とし、[機能設計](functional-design.md#api)は操作・動作の正本とする。
実装と検証は別であり、#89/#90の自動検証は完了。実機異常系は#96、未実装のPlayer統合は#24で扱う。

## 公開経路と生成元

| 経路 | 内容・時点 |
|---|---|
| GET /api/state | 最後に公開したPresentation Model全体 |
| WS /api/events | 接続時と状態変更時の同じ全体snapshot。差分event protocolではない |
| GET /api/read-history | 要求時の有界履歴。stateとは独立した取得 |
| GET /api/read-policy | requested/effective/pending。設定操作の契約は機能設計参照 |

`PlayerSession` → `make_presentation_model` → `serialize_presentation_model`が公開経路。
`diagnostic_fields`を共有してdrive/read/recent_eventsを投影する。
`serialize_daemon_snapshot`のplayer.track/position_lba、media、metadata等はこの公開経路ではない。
内部型やそのserializerのテストだけから公開fieldを推定しない。

状態はmain loopで250 msごとに変化を検査し、revision以外が変わった場合にrevisionを増加する。
250 msは検査間隔であって配信期限の保証ではない。GETは更新待ちの状態を返し得る。
WSは最新snapshotを保持し、各中間revisionや全readの受信を保証しない。受信messageによる再生操作は提供しない。
既定はlistenなし。有効時の既定listenは127.0.0.1。読み取りGET/WSは外部listen時にも利用可能で、
操作POST/telemetryのloopback制限とは区別する。認証/TLSは実装されていない。
stateサイズ上限は1 MiB。read-historyのprovider未提供は503。コマンドの204/202受理は音声/物理操作完了の通知ではない。

## 表記・互換性

intはJSON整数、uintは非負JSON整数、numberはJSON数値、`?`はnull可能。
frameは特記しない限りCD frame（1/75秒、2352 PCM bytes）。LBAはCD frameの絶対アドレス。
read offsetのsampleとCD frameを混同しない。文字列は表示データでありHTMLとして解釈しない。

現行serializerは表のfieldを出力するが、旧payloadの欠損は未取得として扱う。
未知fieldは無視し、未知enumは未対応表示にする。null/欠損を0、false、正常と置き換えない。
空配列はその窓内の項目なしを示し、全期間に事象がなかった証拠ではない。
能力のUNKNOWN、未測定のNOT_CHECKED、mediaのUNSUPPORTEDは異なるenum。
N/A等のUIラベルを新しいwire enumとみなさない。
64bit counterはJSON数値であり、JavaScriptの安全整数範囲を越えた厳密比較は保証されない。
破壊的変更時はschema/移行方針を別途決定し、文書・#24・テストを同時に更新する。

## Snapshot本体

| JSON path | 型・値 | 生成元・意味 |
|---|---|---|
| schema_version | int: 1 | 公開serializer |
| revision | uint | daemon session内の公開更新番号。別sessionと比較しない |
| player.state | string enum | NO_DISC / STOPPED / PLAYING / PAUSED、controller |
| player.track_number | int? | 現在の曲番号 |
| player.position_frames | int? | 現在曲の先頭からの進捗。内部LBA−TOC startを0以上に制限。HDMI可聴位置ではない |
| player.track_duration_frames | int? | 現在曲のTOC長。曲/位置をTOCに対応できない場合null |
| disc.state | string enum | NO_DISC / LOADING / AUDIO_READY / UNSUPPORTED / EJECTING / EJECT_ERROR |
| disc.title, disc.artist | string? | 選択metadata。空のmetadata文字列はnull |
| tracks | array | TOC順の曲。discなしでは空 |
| tracks[].number, duration_frames | int | TOCの番号と長さ |
| tracks[].title, artist | string? | 曲番号で対応したmetadata |
| enrichment.status | string enum | NOT_REQUESTED / LOADING / AVAILABLE / NOT_FOUND / UNAVAILABLE / ERROR。内部AMBIGUOUSはUNAVAILABLEへ投影 |
| artwork.cover | object? | 利用可能なlocal assetがなければnull |
| artwork.cover.url | string | /api/presentation/artwork/cover。provider外部URLではない |
| artwork.cover.mime_type, width, height | null | 現行JSONでは常にnull。取得画像のHTTP Content-Typeとは別 |
| drive, read | object | 以下の診断情報 |
| recent_events | array | 以下の有界event窓 |

TOC start LBA、disc ID、provider候補/ID、metadata内部エラー、media errorは現行公開snapshotにない。
残り時間は曲長と位置の表示上の差から計算できるが、音声出力遅延の実測ではない。
metadata/artworkの到着は再生開始とは独立し、再生状態だけから取得完了を推定しない。

## Drive

| drive配下 | 型 | 意味 |
|---|---|---|
| device, vendor, model, firmware, probe_error | string | probe結果。未取得文字列は空になり得る |
| digital_audio_extraction, c2_supported, c2_trustworthy, read_cache, accurate_stream, speed_control | object | capability flag |
| 上記flag.value | string | UNKNOWN / NO / YES |
| 上記flag.source | string | NONE / KERNEL_REPORTED / DRIVE_REPORTED / TESTED / DATABASE / USER_CONFIGURED / INFERRED |
| 上記flag.detail | string | 根拠の説明。空になり得る |
| current_speed_x | number? | drive速度の観測値。継続実測は未実装 |
| read_offset_samples | int? | sample単位offset。現行は未取得 |

能力はdrive直下。`drive.capabilities`という階層はない。No Discでも利用可能なprobe情報を表示できる。
kernelがspeed制御対応と報告したことは、速度変更成功や実測速度の証明ではない。

## Read

| read配下 | 型 | 意味・生成元 |
|---|---|---|
| session_id | string | daemon API起動時に生成する不透明ID。時間やdevice IDではない |
| stream_generation, policy_revision | uint | workerのstream、方針世代。下記参照 |
| activity | string | IDLE / READING / BUFFERING / COMPLETE / FAILED |
| requested_mode, effective_strategy | string | backend構成の要求/実効識別文字列。固定enumとして推測しない |
| queued_blocks | uint | workerの待機PCM block数。可聴buffer秒数ではない |
| buffer_capacity_frames, startup_buffer_frames, read_block_frames, prebuffer_target_frames | uint | 容量・開始閾値・read量・実効prebuffer閾値のCD frame数。充填frame数ではない |
| last_prebuffer_wait_ms | int? | 直近prebuffer待ち時間ms |
| policy | object | requested/effectiveのpolicy objectとpending bool |
| dropped_events | uint | worker event queueから破棄した件数 |
| latest, current_playback, active_warning | evidence? | 最新read、出力へ提出したPCMの根拠、現在streamの最後のUNCERTAIN |
| stats | object | stream集計。下記参照 |
| coverage, history, event_window | object | 観測範囲と保持窓。下記参照 |

policy.requested/effectiveのfieldはmode（SINGLE/REPEAT）、region_frames、required_matches、
maximum_attempts、time_budget_ms（いずれもuint）。pendingは要求が実効方針に未反映であることを示す。
POSTの検証範囲・適用境界は機能設計を参照する。requested_modeとは別の概念である。

block容量はworkerと同じく`floor(buffer_capacity_frames / read_block_frames)`。
正のread量と非負の容量を検証して算出し、欠損/不正値では未取得表示にする。
queued blocksの比率は可聴秒数やqueued framesではない。容量0では比率を算出しない。

statsの全fieldはuint。read_calls、frames_requested、frames_accepted、direct_retries、
backend_reads、backend_verifies、backend_fixups、backend_skips、backend_read_errors、backend_cache_errors、
backend_other、verification_attempts、verification_mismatches、verified_calls、verification_failures、failed_calls。
workerが観測したread結果を加算する。frames_acceptedは完全成功したreadのframe数で、coverageの一意区間数とは異なる。
verified_callsはmatching_reads>=2、verification_failuresはattemptsありかつ完全成功しなかった呼び出し。
backend countはbackend callbackの観測で、C1/C2エラー数ではない。

## Evidenceと試行

latest/current_playback/active_warning/history.regions[]は同一形式。

| evidence配下 | 型 | 意味 |
|---|---|---|
| device_generation, disc_generation, stream_generation, policy_revision, read_sequence | uint | 根拠の観測世代とstream内read順序 |
| detail_available | bool | 公開したevidenceではtrue。eviction済み領域の存在をfalseの架空recordで表現しない |
| start_lba | int | 読み取り開始LBA |
| frames_requested, frames_read | uint | 要求/取得CD frame数。区間は[start_lba, start_lba+frames_read) |
| status | string | UNKNOWN / CLEAN / RECOVERED / UNCERTAIN |
| local_verification | string | NONE / SINGLE_READ / BACKEND_REPORTED / MULTIPLE_MATCH |
| c2_status | string | UNKNOWN / NOT_AVAILABLE / NOT_CHECKED / CLEAN / REPORTED。現行readerはNOT_CHECKED |
| offset_status | string | UNKNOWN / UNCORRECTED / CORRECTED。現行readerはUNKNOWN |
| direct_retries | uint | 当該readの直接retry回数 |
| backend_events | object | reads/verifies/fixups/skips/read_errors/cache_errors/other、すべてuint |
| verification | object | 以下 |

verification: attempts、complete_reads、matching_reads、mismatchesはuint、time_budget_exhaustedはbool。
detail_capacityは8。accepted_candidate/accepted_attemptはuint?、attempt_detailsは最大8件の配列。
各attemptはattempt（1始まりuint）、frames_read（uint）、complete（bool）、native_error（int）、
direct_retries（uint）、candidate（uint?）。candidateは同じread内のみ比較する。
accepted_attemptは一致閾値を満たした試行。single/backend内部の未観測試行は空配列/nullとなる。

不完全readはUNCERTAIN。完全readでも直接retry/skips/read_errors/cache_errorsはUNCERTAINの根拠となる。
backend fixup（skipなし）や一致確認に至るmismatchはRECOVEREDの根拠となり、この判定が前記anomalyより優先する。
CLEANはこのreadに観測された異常がない分類であり、原盤一致・cache独立性・全disc健全性を保証しない。

## 保持窓と世代

| object | fieldと型 | 意味 |
|---|---|---|
| coverage | scope:string=STREAM, accepted_unique_frames:uint, observations_complete:bool, region_capacity:uint=128 | 完全成功した採用区間の和集合。retry/overlapを二重加算しない |
| history | scope:string=STREAM, capacity:uint=128, storage_bytes:uint, evicted:uint, included:bool, last_read_sequence:uint | 直近128 read。bytesはnative固定配列サイズでJSON長やprocess総メモリではない |
| history.regions | evidence array | included=trueの詳細応答だけに存在。通常state/WSではfield自体を省略 |
| event_window | scope:string=STREAM, first_sequence:uint?, last_sequence:uint?, worker_dropped:uint, replay_available:bool=false | 現在snapshotにあるeventのread_sequence範囲。空窓はnull |

coverageは区間容量超過/不正範囲時にobservations_complete=falseとして次streamまで下限値を固定する。
trueも全disc読取済みを意味せず、空streamのtrue/0をdisc健全と表示しない。
start/cancel/discardでstreamを更新し、coverage・統計・履歴・警告をresetする。
policy revisionはworker初期構成1、reconfigureごとに増加。
device generationはreader open成功のincarnation、disc generationはTOC再受理。未観測の交換は検出できない。
各世代をdaemon再起動間で比較せず、session_idを先に照合する。物理hotplug完全検出は#88の別課題。
current_playbackの根拠はPCM側に保持し、履歴evictionと独立する。
engineは提出済みstereo frame数からoutput.delay()を差し引いた位置に対応する根拠を選ぶ。
その位置に対応する提出済み区間がなければnull。単に最後に提出したblockではなく、
HDMI/ARCの実可聴位置を測定した値でもない。
active_warningは正常readやevent消費で解除せず、stream終了で消す。停止後の永続障害台帳ではない。

詳細履歴応答のrootはschema_version:int=1、session_id:string、stream_generation:uint、history:object。
revisionは持たない。取得中にsession/streamが変わった結果をstateへ併合せず、必要なら再取得する。
詳細HTTPとWSは同時点保証なし。常時詳細pollingを前提としない。

## Eventsと復元

recent_events[]のfieldはsequence:uint（daemon内配信順）、read_sequence:uint（stream内read順）、
stream_generation:uint、type:string=READ_OBSERVED、severity:string（DEBUG/INFO/WARNING/ERROR）、
presentation_priority:string（BACKGROUND/NORMAL/ACTIVITY/IMPORTANT/STICKY）、
region:{start_lba:int,end_lba:int}（半開区間）、read_status:string（evidence.statusと同じenum）。

worker queueは256件、mainのrecent windowは64件。公開時に現在stream以外を除外する。
event_windowのfirst/lastはevent.sequenceではなくread_sequenceである。
worker→main転送とsnapshotは同時点でなく、history.last_read_sequenceがwindow末尾より先行し得る。
revisionの欠番だけでread欠落を確定しない。初回first>1、前回末尾+1より先の窓、worker_dropped増加は
未観測範囲としてUNKNOWNにする。worker_droppedはmainの64件窓の全eviction数ではない。

以下は診断consumerの復元ルールであり、technical statusで実装済み。標準Playerへの統合は#24の対象。
初回REST後にWSへ接続し、再接続時はsnapshotを取り直す。snapshotを正として警告を置換し、
過去eventの再生で警告を再構成しない。同sessionの古い/同revisionは無視する。
session/stream変更で古い警告・gap状態を捨て、同streamのgapは保持する。
technical statusは現在のWebSocketを識別し、退役した接続のmessage/close callbackを無視する。
遅延した旧session応答との競合、実drop、slow client/ログ障害時のaudio非干渉の統合検証は#90で扱う。

## 照合例と検証境界

以下は全payloadではなく、既存テストからのfield投影例。実機測定値ではない。

| 条件 | 照合先 | 期待する投影 |
|---|---|---|
| STOPPED、TOC曲1 start=150/length=750、内部位置150 | tests/presentation_model_test.cpp | player.track_number=1、position_frames=0、track_duration_frames=750、cover=null |
| 15 frame採用の合成例 | tests/presentation_model_test.cpp | coverage.accepted_unique_frames=15、通常historyにregionsなし |
| repeat 2回一致の合成例 | tests/presentation_model_test.cpp | accepted_candidate=1、accepted_attempt=2、detail_capacity=8 |
| single read | tests/presentation_model_test.cpp | attempt_details=[]、accepted_candidate=null |
| 古いrevision/stream/session変更とgap | tests/ui_status_test.js | 警告の旧revisionによる解除を拒否、gap=UNKNOWN、新streamで解除 |
| STOPPED/PLAYING、restart | development/reports/2026-09-26-read-provenance | 記録された条件のみ実機根拠。異常readの実機誘発は未確認 |

No DiscではTOCなしならtracks=[]、位置/曲長=nullとなる。PLAYINGに変わってもfield構造は同じ。
metadata未取得はtitle/artist=null、画像なしはcover=null。null evidenceをCLEANへ変換しない。
2026-09-26に公開serializer・生成元・API route・technical statusと再照合した。
#89/#90はPR #94/#95で完了し、実機異常系は#96へ分離。#24正式化は実装着手条件の整理であり、
Player UI実装済み・実機異常系検証済みという意味ではない。
