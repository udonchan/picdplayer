# 現行の再生・診断メッセージ契約

基礎契約はmaster `0f8e12f`、disc.layoutはPR #101、disc_mapはPR #102の実装を対象とする。
公開`schema_version=1`。現行実装を記述するもので、将来不変のAPIを宣言しない。
公開メッセージのfield表は本書を正本とし、[機能設計](functional-design.md#api)は操作・動作の正本とする。
実装と検証は別であり、#89/#90の自動検証は完了。実機異常系は#146、標準PlayerのIntegrity統合とその通常系検証は#24で扱う。

## 公開経路と生成元

| 経路 | 内容・時点 |
|---|---|
| GET /api/state | 最後に公開したPresentation Model全体 |
| WS /api/events | 接続時と状態変更時の同じ全体snapshot。差分event protocolではない |
| WS /api/navigation | CECの短命なView入力。snapshotとは独立し、接続前・切断中の入力は復元しない |
| GET /api/read-history | 非同期に生成した有界STREAM履歴とDISC集計。取得時点はsnapshotで明示し、stateとは独立 |
| GET /api/disc-map | 詳細履歴を含まないDISC集計。標準Playerの周期更新用 |
| GET /api/read-policy | requested/effective/pending、`requested_source`/`effective_source`（`startup`/`restored`/`saved`/`session`）、`persistence_configured`（boolean、保存先指定の有無）。出所は要求と実効値それぞれに対応し、pending中は異なり得る。保存先指定だけでは保存成功を保証しない。設定操作の契約は機能設計参照 |
| POST /api/metadata-selection | loopback限定。候補番号を選択、またはnullで全候補を明示的に採用しない。受理時204、古い世代・対象なしは409。表示反映は次のsnapshotで確認 |
| GET /api/artist-background | main ownerのrequested/effective/available（bool）とunavailable_reason（stringまたはnull）。利用可能なら理由はnull、metadata無効・非compileはmetadata_disabled、key不在・不正はkey_missing、runtime非対応providerはruntime_unavailable。`persistence_configured`（bool）、`persistence_state`（session_only/saved/unsaved）、`requested_source`（startup/restored/saved/session）を追加。provider未提供の場合は503 |
| POST /api/artist-background | loopback限定。512 bytes以下の`{"enabled":bool}`のみ。OFFは保存失敗でも取得・表示を止めて204、保存成否はGETで確認。ONは利用可能かつ保存先指定時は保存成功で204、利用不能・保存失敗なら状態を変えず409。No Discや画像の通信失敗は設定受理と区別する。型違い・未知field・不正JSONは400、上限超過413、他method405、外部mutation403 |
| GET /api/presentation/artwork/background/{opaque-reference} | 現行の取得済み背景画像だけを最大8 MiBで配信。OFF・disc/artist変更・辞退・再取得による旧参照は404。他method405。no-storeで、provider URLや内部IDをpathへ埋め込まない |

背景状態にはkey、内部path、provider URL、Artist MBIDや保存エラーの生文字列を含めない。
背景の保存情報は自身の要求値に対応し、Read Policyのpending/source/保存情報を流用しない。
`session_only`は保存先なし、`saved`は現在の要求値を含むsnapshotの復元または保存成功、
`unsaved`は保存先指定済みだが現在の要求値の保存を確認できていない状態を表す。
保存先を指定しただけ、またはOFFが204だっただけでは保存成功を意味しない。
保存失敗のOFFはrequested/effective=false、requested_source=session、persistence_state=unsavedを返す。
同じOFFの再要求またはRead Policy保存の成功で未保存状態を解消できる。savedは電源断耐久性の保証ではない。
復元したONは利用不能でもrequested=trueを保持し、effective=falseと理由を返す。新規ON拒否の契約は変えない。
key指定だけでは候補JSONも写真も取得しない。
標準SettingsはこのAPIをRead Policyと独立して読み、利用不能理由と要求値・実効値を表示する。
利用不能時のONを無効にし、既知の状態でOFFを要求できる。背景画像の配信、snapshotへの参照、
session ONはPR #223でmasterへ統合済み。背景保存は設定ファイルのversion 2を用い、
公開snapshotのschema_versionとは別とする。候補JSON・画像取得は空cache pathの一時的memory経路であり、
設定選択の保存は画像のディスク保存を意味しない。

`PlayerSession` → `make_presentation_model` → `serialize_presentation_model`が公開経路。
`diagnostic_fields`を共有してdrive/read/recent_eventsを投影する。
`serialize_daemon_snapshot`のplayer.track/position_lba、media、metadata等はこの公開経路ではない。
内部型やそのserializerのテストだけから公開fieldを推定しない。

状態はmain loopで250 msごとに変化を検査し、revision以外が変わった場合にrevisionを増加する。
250 msは検査間隔であって配信期限の保証ではない。GETは更新待ちの状態を返し得る。
WSは最新snapshotを保持し、各中間revisionや全readの受信を保証しない。受信messageによる再生操作は提供しない。
`WS /api/navigation`は`{"action":"up"}`の形で`up/down/left/right/select/back`を配信する。
各接続の未送信入力は最大8件で、超過時は古い入力を捨てる。接続時の初期messageや永続queueはない。
このchannelは操作commandではない。Viewが入力を解釈し、状態変更が必要ならloopback POSTを使う。
既定はlistenなし。有効時の既定listenは127.0.0.1。読み取りGET/WSは外部listen時にも利用可能で、
操作POST/telemetryのloopback制限とは区別する。認証/TLSは実装されていない。
stateサイズ上限は1 MiB。read-history/disc-mapのprovider未提供・生成待ち・診断送信枠満杯は503。
コマンドの204/202受理は音声/物理操作完了の通知ではない。

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
| enrichment.selection | object? | 候補が取得済みで現行AUDIO_READYのときの明示選択情報。それ以外はnull |
| enrichment.selection.state | string enum | AMBIGUOUS / SELECTED / DECLINED。DECLINEDは現行discで全候補を明示的に採用しない状態。enrichment.status=UNAVAILABLEでも候補状態を識別できる |
| enrichment.selection.session_id, disc_generation, metadata_generation | string, uint, uint | 現行daemon session、disc、metadata取得の世代。選択POSTへそのまま渡す |
| enrichment.selection.selected_index | uint? | 未選択またはDECLINEDではnull。候補配列の0起点index |
| enrichment.selection.candidates[] | array | 0〜100件の表示用候補。index、title、artist、country、date、medium_position、medium_title、track_count。欠損文字列はnull、provider IDは含まない |
| artwork.cover | object? | 利用可能なlocal assetがなければnull |
| artwork.cover.url | string | /api/presentation/artwork/cover。provider外部URLではない |
| artwork.cover.mime_type, width, height | null | 現行JSONでは常にnull。取得画像のHTTP Content-Typeとは別 |
| artwork.background | object? | OFF・画像準備前・取得失敗・現行audio discなしはnull。ON受理はこのfieldの存在を保証しない |
| artwork.background.url | string | 取得結果の世代に対応する同一originの不透明画像参照。旧参照は失効し、clientは新snapshotから再構築する |
| artwork.background.urls | array of string | 同世代の取得済み不透明画像参照、最大3枚。段階的に追加され、先頭はurlと同じ。旧client用urlは維持し、旧payloadのurls欠落時はurlを単一画像として使える |
| artwork.background.dwell_ms, crossfade_ms | uint | 標準View用の滞在45000 ms・crossfade4000 ms。再生時計とは独立。reduced motion時はViewがfadeを即時化する |
| artwork.background.mime_type, width, height | null | 実HTTPのContent-Typeと別。full decodeや寸法検証済みとは扱わない |
| drive, read | object | 以下の診断情報 |
| recent_events | array | 以下の有界event窓 |

disc ID、provider固有ID、metadata内部エラー、media errorは現行公開snapshotにない。
候補表示文字列は最大256 bytesのUTF-8境界で切り詰める。候補の`title`/`artist`等はproviderから得た
識別の手掛かりであり、物理盤との一致保証ではない。複数候補でも未選択ならAudio CD fallbackを維持する。
選択POSTのJSONは`session_id`、`disc_generation`、`metadata_generation`、`candidate_index`の4項目のみ。
`candidate_index:null`は全候補を明示的に採用せずAudio CD表示へ戻す。候補一覧は残り、後から再選択できる。
本文上限512 bytes、型・範囲を検査する。選択とDECLINEDは現在のdisc世代に限り、再挿入時に持ち越さない。
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
| requested_speed_x | number? | ioctlが成功した要求CD倍速。実測値ではない |
| speed_request_error | string | 明示された速度要求を適用しなかった最後の理由（ioctl失敗または能力非対応/不明）。未指定・成功時は空 |
| current_speed_x | number? | legacyの観測倍率。要求/最大速度やraw reportから合成しない |
| current_read_speed_kbytes_per_second | int? | last capability probeのdrive-reported current read speed。未返却はnull、実zero reportは0として区別 |
| current_speed_source | string | raw report取得時DRIVE_REPORTED、未取得NONE |
| current_speed_detail | string | 失敗/未返却/zero reportや回転・throughput実測ではないことの説明 |
| read_offset_samples | int? | stereo sample frame単位の明示起動値。未指定はnull、明示0とは区別。source/detailは[Read Offset](#read-offset)参照 |
| observation | object? | [device観測context](#device-lifecycle-observation)。null/旧payloadで欠損は未取得でありABSENTではない |

能力はdrive直下。`drive.capabilities`という階層はない。No Discでも利用可能なprobe情報を表示できる。
`c2_supported`はMMC CD Read Featureが不在、またはC2 Flagsが取得できた場合だけDRIVE_REPORTEDのYES/NOとなる。
これはC2 error pointerを報告できるというdrive宣言で、読み取り時のC2観測、reportの正確さ、または
bit-perfect再生の証明ではない。command failureまたは応答不正時はUNKNOWNで`probe_error`に理由を残す。
kernelがspeed制御対応と報告したことは、速度変更成功や実測速度の証明ではない。
`digital_audio_extraction`/`accurate_stream`は、MODE SENSE(10) page 0x2aのCDDA_CS/SAを
DRIVE_REPORTEDのYES/NOへ投影する。抽出・連続性を実測したTESTEDとは区別する。
追加queryの失敗/欠損/不正時はUNKNOWNで各flag.detailへ理由を残す。probe_error空は全項目の成功を意味しない。
current read speedは同pageの14-15をraw kB/sとして公開する。max8-9や要求値を代用せず、CD/DVD共通x換算もしない。
標準UIはDRIVE_REPORTEDの正整数1..65535だけをkB/s REPORTED表示し、0/欠損/不正値はNOT AVAILABLEとする。
`read_cache`はcurrent Caching mode page0x08のreported enabled policyを表す。RCD=0はYES、RCD=1はNOで
sourceはDRIVE_REPORTED。これはphysical cache存在/不存在、CD-DAへの適用範囲、媒体再読の独立性の証明ではない。
未返却/短い/不正/取得失敗はUNKNOWN+detailとし、NOへ変換しない。UI labelはRead cache enabledとする。

### Device Lifecycle Observation

`drive.observation`は#88の実装から公開する追加objectである。完全な契約と限界は
[device lifecycle設計](integrity-design.md#device-lifecycle-observation)を参照する。schema_version=1の旧clientは未知fieldを無視できる。

| field | 型 | 意味 |
|---|---|---|
| generation | uint | 同じread.session_id内の観測context。初回待ちは0、変化/reader failure失効でadvance。物理IDでもreader-open counterでもない |
| presence | string | UNKNOWN / ABSENT / PRESENT。device node/metadataの観測でありdisc/tray状態ではない |
| reason | string | PENDING / INITIAL / STATE_CHANGED / SIGNATURE_CHANGED / READER_FAILURE。reset/交換の確定判定を表す値ではない |
| sampled | bool | ownerがsampleを適用したこと。trueでもUNKNOWN/取得失敗があり得る |
| method | string | 現行はNODE_SYSFS_POLL。signatureとmajor/minor/inode等の内部tokenは公開しない |
| error | string | 有界な観測エラー。PRESENTや空errorからdrive health/校正/原盤一致を推測しない |
| revalidating | bool | mainが新contextのmedia/TOC適用を確認中。source不明とcapability NOは別である |
| retiring_io | bool | 旧reader/media ownerのI/O/closeがまだquiescentでない。取消受付は解放完了ではない |

既存read evidenceのdevice_generationはreader open counterのまま維持する。disc/stream/policy世代も同値と仮定しない。
観測不可の場合、現在contextへbindできないcapability reportはUNKNOWNにし、requested configを保持する。
C2_PROBE_PENDINGは有効なcurrent-context reportの前提未充足で、必ずしもcallが実行中という意味ではない。
old read/probe/TOC/speed/diagnosticの結果を新contextへ適用せず、204もcontext変更後の旧device操作の実行保証にしない。
通常のdisc挿入autoplayとdevice再接続による失効を区別し、再接続だけで旧streamを再開しない。
probe_errorの空だけで観測済み/probe成功と推定せず、generation・presenceとcapabilityの根拠を照合する。

## Read

| read配下 | 型 | 意味・生成元 |
|---|---|---|
| session_id | string | daemon API起動時に生成する不透明ID。時間やdevice IDではない |
| stream_generation, policy_revision | uint | workerのstream、方針世代。下記参照 |
| activity | string | IDLE / READING / BUFFERING / COMPLETE / FAILED |
| requested_mode | string | 現行はLEGACY。将来のPlaybackMode用の要求識別子で、backendやstrategyではない |
| effective_strategy | string | 現在のreaderへ適用済みのstrategy識別文字列。`strategy.effective`と同じ互換field |
| strategy | object | requested/effective strategy、downgrade_reason、pending。下記参照 |
| queued_blocks | uint | workerの待機PCM block数。可聴buffer秒数ではない |
| buffer_capacity_frames, startup_buffer_frames, read_block_frames, prebuffer_target_frames | uint | 容量・開始閾値・read量・実効prebuffer閾値のCD frame数。充填frame数ではない |
| last_prebuffer_wait_ms | int? | 直近prebuffer待ち時間ms |
| read_stall | object | in-flight reader callの時間と停止境界。下記参照 |
| policy | object | requested/effectiveのpolicy objectとpending bool |
| dropped_events | uint | worker event queueから破棄した件数 |
| latest, current_playback, active_warning | evidence? | 最新read、出力へ提出したPCMの根拠、現在streamの最後のUNCERTAIN |
| stats | object | stream集計。下記参照 |
| coverage, history, event_window | object | 観測範囲と保持窓。下記参照 |

policy.requested/effectiveのfieldはmode（SINGLE/REPEAT）、region_frames、required_matches、
maximum_attempts、time_budget_ms（いずれもuint）。pendingは要求が実効方針に未反映であることを示す。
POSTの検証範囲・適用境界は機能設計を参照する。requested_modeとは別の概念である。

`strategy.requested`はoperatorが要求したreader構成、`strategy.effective`は現在streamへ適用済みの
構成である。reader構成はstream途中で変更しない。`pending=true`はprobeまたはpolicy変更による要求が
次のSTOPPED境界まで実効readerへ反映されないことを示す。`downgrade_reason`は`NONE`、
`POLICY_RESTART_REQUIRED`、`C2_PROBE_PENDING`、`C2_CAPABILITY_UNKNOWN`、`C2_UNSUPPORTED`、
`C2_BACKEND_UNSUPPORTED`、`C2_STREAM_RESTART_REQUIRED`のいずれかである。これはoptionalな
mechanismを選択できなかった理由であり、PCM品質・C2 trust・bit-perfect性の評価ではない。

`--direct-c2-pointers`を要求したdirect backendでは、probe完了前は通常direct readを
`C2_PROBE_PENDING`として使用する。support=YESなら次に生成するreaderでC2 pointer readを有効にする。
support=NOは`C2_UNSUPPORTED`、UNKNOWNは`C2_CAPABILITY_UNKNOWN`で通常readを維持する。probe結果が
PLAYING/PAUSED中に届いた場合、既存readerは切り替えず`C2_STREAM_RESTART_REQUIRED`としてSTOPPED後に
適用する。C2が実際に取得されなかった理由は各readの`c2_status`であり、strategy理由と混同しない。

`read_stall.inflight_ms`は現在進行中のreader callの経過msで、callがない場合はnullである。
`timeout_ms`はeffective ReadPolicyの`time_budget_ms`、`last_timeout_ms`は当該streamでmain loopが
停止を選んだ最後の観測値で、未発生ならnullである。timeoutはioctlを中断したことを意味しない。timeout時は
PCMを代替せずSTOPPEDへ遷移し、readerはworker threadでcall復帰後に破棄する。ALSA underrun recovery、
`RECOVERED` read evidence、C2 statusとは別の状態である。

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
| read_independence | string | UNKNOWN / CACHE_POSSIBLE / CACHE_MITIGATED。現行repeatはCACHE_POSSIBLEで、物理再読込は保証しない |
| c2_status | string | UNKNOWN / NOT_AVAILABLE / NOT_CHECKED / CLEAN / REPORTED。既定はNOT_CHECKED。`--direct-c2-pointers`を指定し、probeがC2 support=YESならC2 pointerを要求する。support=NOはNOT_AVAILABLE、packet failure後の通常read fallbackはUNKNOWN。 |
| offset_status | string | UNKNOWN / UNCORRECTED / CORRECTED。未指定はUNKNOWN。適用と非採用の区別は[Read Offset](#read-offset)参照 |
| direct_retries | uint | 当該readの直接retry回数 |
| backend_events | object | reads/verifies/fixups/skips/read_errors/cache_errors/other、すべてuint |
| verification | object | 以下 |

verification: attempts、complete_reads、matching_reads、mismatches、overlap_frames_requested、overlap_frames_comparedはuint、
overlapはNOT_REQUESTED / NOT_CHECKED / STREAM_BOUNDARY / MATCHED / MISMATCHED、time_budget_exhaustedはbool。
detail_capacityは8。accepted_candidate/accepted_attemptはuint?、attempt_detailsは最大8件の配列。
各attemptはattempt（1始まりuint）、frames_read（uint）、complete（bool）、native_error（int）、
direct_retries（uint）、candidate（uint?）、physical_start_lba（int）、observed_start_lba（int）、physical_frames_requested（uint）。
candidateは同じread内のみ比較する。top-levelのstart_lba/frames_requested/frames_readは出力論理区間、
attemptのphysical値は先行overlapを含むbackend要求範囲である。
accepted_attemptは一致閾値とoverlap条件を満たし採用された試行。single/backend内部の未観測試行は空配列/nullとなる。

不完全readはUNCERTAIN。完全readでも直接retry/skips/read_errors/cache_errorsはUNCERTAINの根拠となる。
backend fixup（skipなし）や一致確認に至るmismatchはRECOVEREDの根拠となり、この判定が前記anomalyより優先する。
CLEANはこのreadに観測された異常がない分類であり、原盤一致・cache独立性・全disc健全性を保証しない。

## Unresolved PCM Policy

#156でSTOPを唯一の実装済み方針として維持し、BEST_AVAILABLE/SILENCE/WAIT_WITH_BUDGETは
現時点で採用しないと判断した。[比較・見送り理由・再検討条件](integrity-design.md#unresolved-pcm-policy)を参照する。
この判断に伴うfield、enum、event、CLI/APIの選択肢の追加はない。
`unresolved_data_policy`と`sample_origin`は現行ReadPolicy/JSONのfieldではなく、
将来の要求モデルにある方針・由来の概念である。production payloadやfixtureへ先取りしない。

現行の表示・復元は次の区別を維持する。

- UNCERTAINはreadの根拠分類であり、代替出力の発生や不採用そのものを示す値ではない。
  完全readがretry等でUNCERTAINでも現行policyに採用されることがある。逆にrepeatの一致回数だけで採用を判断しない。
- `latest`/historyやREAD_OBSERVEDは観測したreadを示す。PCMのqueue採用・出力提出・消費は別であり、
  `current_playback`は既存の提出区間とALSA delayに対応する根拠である。
- 未解決readはPCMをqueueへ入れずSTOPする。workerのFAILEDはmainでの停止/cancelによりresetされ得る。
  STREAMのhistory/active_warningは永続的な停止理由台帳ではなく、STOP後の空窓を正常だった証拠にしない。
- `read_stall`、ALSA underrun復旧、backendのC2 fallback、offsetのwindow制限を、
  未実装の追加PCM方針やSILENCE出力として表示しない。

将来採用する場合には、実動作・理由・区間・sample origin・欠損/採用量・期限を表す契約と、
旧client/欠損値の扱いを別途決定する。既存statusやcoverageへ新しい意味を黙って追加しない。
Custom UIが新fieldを仮定する必要はなく、現在の公開契約の互換性は変えない。

## Complete PCM Input

#160の[完全取得入力契約](integrity-design.md#complete-pcm-input)はA/B/Cを経て、#239の開発差分で
実session/APIへ接続する。以下はlocal SHA-256指紋専用の別契約であり、既存Player snapshotのschemaを拡張しない。
coreのC++型・receipt・result spanをそのまま公開せず、ownerが有効条件と実reader解放を確認したmanifestだけを公開する。
`local_pcm_fingerprint`/`NOT_CHECKED`は外部MATCHではない。起動・挿入・通常再生・GETから取得を開始しない。
`read.activity=COMPLETE`はstreamのread終了、`coverage.observations_complete`やdisc mapのcompleteは
観測台帳の欠落有無であり、専用取得jobの全域完了を意味しない。
既存の`read.offset_window`は再生可能範囲の計算であり、全disc PCMの取得・保持・checksumの完成証明ではない。

### Explicit Local PCM Jobs（開発差分）

| method/path | 契約 |
| --- | --- |
| GET /api/complete-pcm | read-onlyのowner状態。schema_version=1、session_id、現在disc_generation、job_id/duration_ms（未開始0）、state、failure/core_failure、native_error、busy、reader_released、call_inflight、waiting_for_access、accepted_frames/bytes、uncertain/recovered_reads、reserved_bytes、can_start/start_rejection、media_activity、profile、external_verificationを返す。GETでpoll/confirmやdrive I/Oを実行しない |
| POST /api/complete-pcm | loopback限定。startは下記5 field、cancelは下記3 fieldのみ。受理202は開始/取消要求の受理でありCOMPLETE/reader解放ではない。拒否は409で実行しない。無効body/profileは400、4096 bytes超は413、他methodは405、handlerなしは403 |
| GET /api/complete-pcm/result | 現在の有効条件を満たすconfirmed COMPLETEだけをmanifestとして返す。なければ404。provider未設定は503。結果は64 KiB以内、送信は既存diagnostic経路の4 KiB分割・同時8 response上限を再利用 |

start bodyは`action="start"`、`session_id`、`disc_generation`（正uint64）、`duration_ms`（1〜10,800,000）、
`profile="local-pcm-sha256-v1"`。cancel bodyは`action="cancel"`、`session_id`、`job_id`（正uint64）。
session_idは英数字/`-`/`_`、1〜64文字。未知/重複field、型・範囲違いはworkerへ渡さない。
operatorが有限予算を明示し、最大3時間は既存workerの上限と一致させる。既定UIの明示開始は30分上限を表示する。

一般の既存409は`{"error":"command_rejected"}`のまま。capture固有の拒否は
`{"error":"capture_releasing","executed":false,"retry":"manual"}`等とし、操作は予約/自動再実行しない。
固有errorは`capture_busy`、`capture_releasing`、`capture_not_ready`、`capture_context_changed`、`capture_no_job`。
開始不能の詳細はstatusのstart_rejection（device/audio未準備、非STOPPED、output未停止、reader解放待ち、
media busy、policy pending、offset unknown/非0、policy/resource予算非対応等）で区別する。
`media_activity`はmedia workerの一時点の状態を示すstringで、`IDLE`（処理/受付待ち/未回収結果なし）、
`PENDING`（受付済み実行待ち）、`RUNNING`（worker処理中、coordinator待ちやcallback前後も含む）、`RESULT_PENDING`（結果回収待ち）、
`UNKNOWN`（内部callerが内訳を供給しない）を取る。実sessionでは同じmutex下の一回の観測から
この値とmedia idle gateを作り、判定の内訳だけを追加する。FDが閉じていても結果回収前はidleではない。
このfieldは取得slotのbusyや実FD/物理driveの所有を直接測定した値ではなく、後続POSTの受付を予約もしない。
`IDLE`でもSTOPPED/output/reader/policy/offset等の他gateを満たすとは限らない。
既存の開始gate、schema version、POST body/error、result manifestには変更を加えない。
owner内では適用可能な媒体結果/失効情報を先に反映し、届いている明示API要求を新しい定期observe投入より先に評価する。
受理直後はcaptureのbusy/reader解放を再取得して定期observeを抑止するが、start要求そのものを待ち行列へ保管しない。
既にmedia処理が残る場合やservice後の到着は従来どおり409になり得る。GETのcan_startを後続POSTの受付保証にしない。

stateはIDLE/COLLECTING/PREPARED/COMPLETE/INCOMPLETE/INVALID。busyとreader_releasedは結果状態と独立し、
取消/期限/失効後もin-flight I/Oから戻るまでbusy=trueになり得る。新startは拒否し、完了後の明示再操作だけを受ける。
取消は対象session/jobを検証し、完了済みreceiptに対する取消も公開を撤回する。
play/seek/track/eject等は先にcaptureを失効させ、解放未完了なら要求自体は未実行で手動再操作とする。
Stopは既にSTOPPEDでも取消できる。HTTP/CEC/stdin、policy/device/disc変更のどれも旧receiptを残さない。

manifestはschema_version=1、kind=complete_pcm_manifest、duration_ms、identity（session/job/disc/device観測世代、
capture reader incarnation、policy revision、明示offset）、backend、TOC、[begin,end) coverage、read_policy、
local_read_observations、reader_released、cache_independence=NOT_PROVEN、external_verification=NOT_CHECKED、
fingerprint（Cのlocal JSON結果）を持つ。inputのcoverage完了は原PCMの正当性やcache独立性の証明ではない。
reader incarnationをdevice観測generationと同じcounterとして扱わない。現slot一件だけを保持し、履歴/PCM保存はしない。
現在のSTOPPED、CLEAN/RECOVERED、strategyやread_stallへ別の意味を押し込まない。
通常再生history、MusicBrainz Disc ID、metadata、部分PCM fileからmanifestを生成しない。
workerの[内部契約](integrity-design.md#acquisition-worker-implementation)でいうgateを実sessionが観測するが、
local試験だけで実drive/ALSA/物理release確認済みと描画しない。取消/期限もkernel I/Oを強制中断せず、
destructorのjoinまで非blockingとする保証はない。

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

詳細応答rootはschema_version:int=1、session_id:string、stream_generation:uint、history:object、disc_map:object?。
rootにはrevisionを持たず、disc_mapは独自revisionを持つ。STREAM historyはsession/stream、
DISC集計はsession/disc世代を照合する。stream変更だけでDISC集計を破棄しない。
詳細HTTPとWSは同時点保証なし。常時詳細pollingを前提としない。

### 音声優先の非同期診断

read-history/disc-mapはmain ownerが有界な観測値だけをcaptureし、専用workerでJSONを生成する。
音声供給は生成完了を待たない。実行中1件・置換可能pending1件を上限とし、同じcontext/種類の要求は集約する。
種類ごとの再capture間隔は既定2秒以上で、要求数に比例して生成jobを積まない。map取得は128件の履歴をcopy/JSON化しない。
この集約はJSON更新機会の集約であり、読取り観測を採用成功へ書き換えたり、履歴evictionを隠したりしない。

両応答はrootに`snapshot:{sequence:uint,captured_at_unix_ms:int}`を追加する。
sequenceは同じdaemon内のcapture順であり、read_sequence・state revisionとは別のcounter。
captured_at_unix_msはcapture時のUnix epoch millisecondsで、応答時刻や音声再生headではない。
wall clock補正やclientとの時計差により鮮度の推定に誤差があり得る。2秒は鮮度の上限保証ではない。
同contextの完成済みJSONがあれば、更新中・生成失敗時にもその取得時点の結果を返す。古さを正常/最新と推定しない。

contextはstream世代とvalidated disc世代（なしを含む）で照合し、変更後は旧cache/pending/完了結果を適用しない。
初回や変更直後に完成済み結果がない場合は503 `diagnostics_pending`。provider未提供時のread-historyは従来どおり
`read_history_unavailable`、disc-mapは`disc_map_unavailable`。生成失敗も未取得として扱い、成功JSONを捏造しない。
両endpointのJSON上限は各1 MiB（超過をrouteが検出した場合500 `diagnostics_too_large`）。
JSON本文の送信はwriteable callbackごとに最大4096 bytes、保持中の診断応答は最大8件。
送信枠満杯は503 `diagnostics_busy`。slow clientのために新しい履歴を保持し続けず、生成済み文字列だけを有界に保持する。
GET以外は405。503時のclient再取得も有界間隔にし、busy loopや操作POSTの再送で代用しない。

disc-map応答はschema_version/session_id/stream_generation/snapshot/disc_mapを持ち、historyは持たない。
標準Playerは返答のsession/stream/disc世代を最新snapshotと照合し、旧返答を採用しない。
同discで既に保持したDISC集計そのものはstream変更だけでは消さず、次の有効な返答まで取得時点付きで表示できる。

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

以下は診断consumerの復元ルールであり、technical statusと#24の標準Player Phase 1で実装済みである。
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
#89/#90はPR #94/#95で完了し、実機異常系は#146へ分離。#24のPlayer UI Phase 1と
通常系PiのAPI/CDP照合は完了しているが、実機異常系検証済みという意味ではない。

## Disc layout（#98）

`disc.layout`はnullまたは次のobject。既存tracks/player fieldは変更しない。

| field | 型 | 意味 |
|---|---|---|
| session_id | string | read.session_idと同じdaemon session |
| disc_generation | uint | 受理済みTOCの観測世代。evidence.disc_generationと照合 |
| start_lba | int | 最初のaudio trackの開始LBA |
| leadout_lba | int | 最終trackの終了LBA（含まない） |
| tracks | array | TOC順のnumber:int、start_lba:int、end_lba:int |

範囲は[start_lba, end_lba)。先頭以前のpregap/lead-inを表さず、非audio領域の測定済み宣言ではない。
受理済みの有効TOCがありAUDIO_READY、再取得不要、session/世代既知のときだけ公開する。
NO_DISC/LOADING/UNSUPPORTED/EJECTING/EJECT_ERROR、再取得中、TOC不在ではnull。
metadata未取得でも公開する。旧tracksが一時保持されてもlayout=nullを現在座標の不在として優先する。
同sessionかつ同disc_generationのevidenceだけを対応させる。device_generationはreader open単位で、
TOC識別子ではない。reader再openだけでTOC座標が変わるとは限らず、物理交換検出は#88の範囲。
現在曲のstart_lbaにplayer.position_framesを加えれば表示用の絶対進捗になるが、
対応曲なし/null/範囲外は位置不明として扱い、物理ヘッド位置や実可聴位置とは呼ばない。

## Disc read map（#99）

GET /api/read-historyのrootへ`disc_map`を追加する。通常state/WSには含めない。
layout公開条件を満たさないとnull。従来historyはSTREAMのままで意味を変更しない。

| field | 型 | 意味 |
|---|---|---|
| scope | string: DISC | 現在受理したdisc観測世代 |
| disc_generation | uint | disc.layoutと対応する世代 |
| revision | uint | 観測更新番号。disc世代変更で0へreset |
| observations_complete | bool | 容量超過/不正結果/結果未取得例外/revision飽和なし。全disc読取済みの意味ではない |
| capacity | uint:256 | 最大区間数 |
| storage_bytes | uint | native集計構造体サイズ。JSON長ではない |
| regions | array | start_lba:int、end_lba:int、flags:uint。半開区間 |

flagsはbit集合: 1=request attempted、2=全要求frame取得成功、4=direct retryあり、
8=repeat試行複数、16=RECOVERED、32=UNCERTAIN、64=backend fixup/skip/read/cache error観測。
要求区間を対象とし、0/部分frameの失敗もattempted+uncertainで表す。acceptedがない区間は
取得済みと塗らない。部分取得の正確な場所は推測しない。個数・時系列・独立物理再読込の保証ではない。
同じflagsの隣接/重複だけ結合し、異なる観測は重なる。最後の成功で過去異常を消さない。
未保持領域は未観測であって未読/正常ではない。regionはソート順を保証しない。
上限超過/不正結果ではobservations_complete=falseで保持内容を凍結する。読取例外で結果が得られない場合も同様。
revision上限時も凍結しwrapしない。既存区間は下限の観測情報で、完全な履歴ではない。

workerの一次read結果から集計しevent queue dropと独立。stop/seek/曲変更/policy変更では保持する。
同discのcancel済みstreamから遅れて返ったreadも実観測として集計するが、旧disc結果は捨てる。
TOC再受理時にresetする。未検出の物理交換は#88の範囲で、永続化しない。
詳細応答rootのsession_idとmap.disc_generationを最新disc.layoutに照合し、不一致なら捨てる。
STREAM historyは従来どおりstream_generationも照合する。mapだけをstream変更でresetしない。
同discで古いmap revisionへの巻戻りを拒否し、layout=null/世代変更では旧mapを破棄する。
取得は詳細展開/明示更新時を基本とする。標準Playerだけは`PLAYING`中のmap進行表示のため2秒に一回を上限として再取得するが、WS更新ごとには取得しない。最大256区間のdisc_map objectはテストで64 KiB未満を検査する。historyを含む応答全体の上限ではない。

### stream終了と音声出力の終了待ち

`read.current_playback`は出力エラーでstreamを終了した場合も破棄する。
workerのcancel/discardではevent queueと`dropped_events`を同時にリセットし、
旧streamの欠落件数を新streamの診断へ持ち越さない。disc_mapは別のdisc世代寿命に従う。

nonblocking drain中は追加のALSA delay照会を行わず、位置・根拠は最後に取得した値を保持する。
この間のcurrent_playbackを刻々の実音声位置として扱わない。正常drain完了時に停止・破棄する。
drain中または全PCM提出後のunderrunはエラー停止とし、最終sectorへ再seekしない。
途中のunderrun復帰と異なり、正常完走したという保証にはしない。

### Overlap evidenceの未観測と拒否（#115）

NOT_REQUESTEDはoverlap無効、STREAM_BOUNDARYは直前の採用PCM不在、NOT_CHECKEDは比較対象はあるが候補一致に至らず比較未実施を示す。overlap_frames_comparedは実比較時だけ非0となる。accepted_candidate/accepted_attemptはoverlapを含む採用条件を満たした場合だけ設定し、拒否時はnull。matching_readsは候補同士の一致回数であり、overlap成功や採用を意味しない。

## Read Offset

2026-10-16、[integrity仕様](integrity-design.md#10の採用仕様と実装)のD、
「補正可能な完全CD frameだけを再生する」に従って実装したschema_version=1の追加fieldである。
旧payloadで欠けていれば未取得と扱い、旧clientは未知fieldを無視できる。

### 起動設定と再生可能範囲

既存`drive.read_offset_samples`のint?/nullは維持し、次を追加する。

| JSON path | 型・値 | 意味 |
|---|---|---|
| drive.read_offset_source | string: NONE / USER_CONFIGURED | 未指定はNONE、明示値（0を含む）はUSER_CONFIGURED。TESTED/DATABASEの自動取得は実装しない |
| drive.read_offset_detail | string | 未指定は空。明示入力は最大128 bytesの固定説明で、物理校正未検証を示す。任意CLI文字列や内部pathを転載しない |
| read.offset_window | object? | offset未指定、layout無効、TOC不在ならnull。明示値と有効layoutがある場合だけ以下を計算する |
| read.offset_window.disc_generation | uint | 同じsnapshotのdisc.layoutの世代。rootのread.session_idも照合する |
| read.offset_window.offset_samples | int | このwindow計算に使用したsigned 32-bit stereo sample frame数 |
| read.offset_window.correctable | sample range? | 数学的に補正可能な論理sample範囲。空ならnull。取得成功を意味しない |
| read.offset_window.playable | LBA range? | Dで許可する完全CD frameの範囲。空ならnull。取得成功を意味しない |
| read.offset_window.unavailable | array of sample range | 物理的に補正できない論理区間、昇順・重複なし・最大2件。空は除外なし |
| read.offset_window.alignment_excluded | array of sample range | 補正可能だがDのframe整列のため除外する区間、昇順・重複なし・最大2件。空windowならcorrectable全体を1区間で表す |
| read.offset_window.reason | string: NONE / BOUNDARY_CLIPPED / NO_PLAYABLE_RANGE | 再生範囲を制限しなければNONE、制限ありかつ再生可ならBOUNDARY_CLIPPED、playable=nullならNO_PLAYABLE_RANGE |

sample rangeは`{start_sample:int,end_sample:int}`、LBA rangeは`{start_lba:int,end_lba:int}`で半開区間とする。
いずれも空区間をobjectにしない。sample位置は`LBA * 588`の絶対座標で、channel別sampleの個数ではない。
内部ではsigned 64-bitで検証するが、有効TOCのint32 LBAとsigned 32-bit offsetから算出するため、
公開する位置はJavaScriptの安全整数範囲内に収まる。計算不能を0や空配列へ変換しない。
無効TOCは既存layout無効化の規則に従い、このwindowもnullにする。

windowはmain ownerがTOCと起動設定から計算する値で、stream coverageとは寿命も意味も異なる。
stop/seek/policy変更で消さず、layout=null・disc世代変更・session変更時は旧windowを破棄する。
TOC再受理後に同じ値になる場合も新disc_generationで再計算する。No Discでも起動設定値は残るが、windowはnullである。
probe結果でユーザー設定を消さず、vendor/modelと物理同一性・校正の正しさを結び付けない。
物理交換を未検出の場合まで有効性を保証するものではない（#88）。

### Read evidenceと試行

`latest/current_playback/active_warning/history.regions[]`へ`offset`（object?）を追加する。
未指定時はnull。明示0を含む設定があるreadではobjectとし、現行のoffset_statusと区別して設定根拠を保持する。

| offset配下 | 型・値 | 意味 |
|---|---|---|
| samples | int | 当該readのimmutableな設定値。現時点のdrive表示から旧履歴の値を補完しない |
| source, detail | string | 明示入力由来のUSER_CONFIGUREDと固定説明。現時点のdrive表示から旧履歴を補完しない |
| single_physical_read | object? | singleの実backend要求/結果。repeatは複数試行なのでnull。範囲検証でI/O未発行の場合もnull |

`single_physical_read`は`start_lba:int`、`frames_requested:uint`、`observed_start_lba:int`、
`frames_read:uint`、`native_error:int`を持つ。位置とframe数は補正用の余分なsectorを含む
実backendのCD frame座標/個数であり、native_errorはbackendの診断値である。
backend callbackが戻らず観測できない結果を生成しない。backend内部の個別ioctlや物理再読回数を表す値ではない。
top-levelの`start_lba/frames_requested/frames_read`は引き続き出力論理CD frame範囲であり、
singleの物理read結果をそのまま代入しない。必要な物理要求全量が得られなければ論理PCMは非採用としframes_read=0とする。

repeatの既存`verification.attempt_details[]`ではphysical_start_lba、observed_start_lba、
physical_frames_requested、frames_readに実backendの物理CD frame値を入れる。
新たに`logical_start_lba:int`、`logical_frames_requested:uint`を追加し、
比較した補正後の論理範囲（先行overlapを含む）を表す。completeは必要な物理入力と補正後候補の
両方が完全なときだけtrueとなる。比較・overlap_frames_comparedは従来どおり論理CD frame数である。
singleを架空のrepeat試行として配列に追加せず、attempt上限8と既存履歴上限128を維持する。

offset_statusは未指定ならUNKNOWN、完全な補正PCMを採用した場合（明示0を含む）はCORRECTEDとする。
既知でも失敗・非採用のreadはUNCORRECTEDとし、read status、single物理readのnative_error/取得数、
repeatの試行・時間予算の根拠と併せて判別する。
これは未補正PCMを代替採用した意味ではない。補正と反復一致・C2 trust・原盤一致を別に表示する。
physical readのC2/retry/backend観測はguard sectorやoverlapを含む。その範囲の異常を、
補正出力内の特定sampleの異常と断定しない。既存の保守的なC2集約とstatus優先順位を維持する。

### 互換性とcoverage

追加情報はschema_version=1のoptional fieldとし、旧payloadで欠けていたら未取得と扱う。
CORRECTEDだけがありoffset objectがないpayloadから値や物理範囲を推定しない。
REST/WSのPresentation Modelとread-historyのevidenceは同じserializerを使用する。
`GET /api/read-history`の既存root fieldはschema_version/session_id/stream_generation/history/disc_mapを維持し、
非同期診断では取得時点を示すoptionalなsnapshotも持つ（[非同期診断](#音声優先の非同期診断)参照）。
read objectやwindowを新たにrootへ追加しない。windowはstate/WSで取得する。
既存stateの1 MiB制限とdisc_map objectの64 KiB未満の試験を維持し、
最大8 attempt・128 evidenceとoffset追加情報を含む実serializer出力で容量・型を検証する。
履歴は従来どおりsession/stream、windowはsession/discで照合し、旧非同期応答を新世代へ適用しない。
offset情報は各evidenceに値で保持し、history eviction後もcurrent_playbackの根拠を維持する。

採用coverage、stats、disc mapの単位と観測規則は変えない。Dでは採用PCMが完全CD frame単位なので、
accepted_unique_framesに588を掛けるとそのstreamの採用sample frame数の下限を得られる。
guard/overlapを二重加算せず、unavailableとalignment_excludedをacceptedとして塗らない。
単位の違う値を合算せず、window全体が取得済みとも表示しない。
offset_windowはSTOP後も端除外の理由を表示できるが、完走記録や外部checksum入力の完成証明ではない。
read.activity=COMPLETEは指定したstream範囲のread完了であり、drain完了・全disc再生完了の宣言にしない。
物理read faultの代替出力や完全取得PCM基盤を、この追加fieldで実装した扱いにしない。
