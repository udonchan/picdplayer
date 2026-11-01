# 読み取り信頼性・説明可能性の仕様

この文書は読み取り信頼性（integrity）の横断仕様であり、基本・機能・詳細設計の一部である。
2026-09-25にDevelopmentの拡張案を統合した。実装済みの契約は
[機能設計](functional-design.md)と[詳細設計](detailed-design.md)、実機確認範囲は
[検証状況](../development/verification.md)を参照する。

以下では、継続して守る原則と未実装の要求を定義する。未実装の要求を統合したことは、
現在のCLI/APIで利用可能になったことを意味しない。型名・追加JSON field・endpoint名・
上限の候補値は明示したとおり暫定であり、公開前に互換性と実測を基に確定する。

## 実装との対応

| 区分 | 内容 |
|---|---|
| 実装済み | read-only能力probe、UNKNOWNモデル、区間付きReadEvidence、stream世代、ALSA再生head推定、bounded event、technical status |
| 実装済み | 先読み容量・開始閾値、drive access直列化、single/repeat、ReadPolicyの停止境界でのruntime適用 |
| 実装済み | direct backendのC2 pointer opt-in、capabilityに応じたsingle/repeat strategy選択とdowngrade reason公開 |
| 実装済み | 明示read offsetとsample補正・playable window・端coverage。物理校正/試聴は別確認 |
| 実装済み・受入範囲を分離 | local checksum/明示jobはPR #256でmaster統合済み。過去の取得記録と今回のlive照合・残受入は[検証状況](../development/verification.md#pr256-readonly-20261031)で区別する |
| 未実装/未確認の要求 | cache軽減を伴う独立性、C2 trust評価、追加mode、外部照合。表示済み能力の実装は下記v1完成条件に従う |
| 未確定の詳細 | cache対策手順、mode既定値、照合サービス・依存library |

現在の公開型は[ReadEvidence / ReadDiagnostics](../../include/integrity_state.hpp)、
[DriveCapabilities](../../include/drive_capabilities.hpp)、[ReadPolicy](../../include/read_policy.hpp)、
[PlayerEvent](../../include/player_event.hpp)を正とする。後述の拡張モデルと同名でも同一の契約とは限らない。
現在のeventはREAD_OBSERVEDで、`WS /api/events`はsnapshot配信である。
device/disc generation、policy revision、128件の詳細履歴、stream coverage、256区間のdisc mapは公開済み。独立した完全replay用diagnostic streamは提供しない。

未実装項目の着手・進捗・完了条件は[残課題一覧](../development/backlog.md)で追跡する。
phase番号は段階の対応付けであり、phaseの一部分を実装しただけで全体を完了とはしない。
Drive capabilityの表示済み項目には、後述の[v1完成条件](#drive-capability-v1-completion)を適用する。
上記の未実装という現況記録は、それらをv1後へ延期してよいことを意味しない。

## 1. 目的と原則

「現在再生しているPCMを、どの観測と検証に基づいて採用したか」を説明する。
音質改善や原PCMの完全な復元を主張する機能ではない。
優先順位はcorrectness、truthfulness、explainability、drive independence、graceful degradation、
playback continuity、quiet operation、extensibility、UI simplicityとする。

- read成功は証拠であり、正しい原PCMの証明ではない。
- C2非検出、複数read一致、外部照合成功、offset補正は別々の事実である。
- capability UNKNOWNをNOへ、offset不明を0へ変換しない。
- backend名やSECUREという設定だけを根拠に「検証済み」と表示しない。
- UI/診断接続の停止・遅延がaudio処理を待たせてはならない。
- 不明・未解決・代替PCMの使用を隠して再生継続を成功扱いにしない。

## 2. 現行実装と追加要求の境界

観測core、technical status、先読み設定、drive access直列化、任意の反復一致は現行設計へ統合した。
追加要求の中心はC2 trust評価、cache独立性、追加mode、外部照合である。offsetの実装は後述#10を参照する。direct C2 pointerは
明示opt-inで、capabilityに応じた現行single/repeat strategy選択とdowngrade reasonを公開する。
ReadPolicyの稼働中切替は実装・自動試験済みで、
通常CDでPLAYING/PAUSED中の保留とSTOPPED境界での適用を確認した。現在の小さなReadPolicyと、
以下の全要素を備えた将来policyは区別する。

初期の依存調査では観測coreへの新規libraryは不要だった。MMC/C2/cacheはLinux UAPIと
drive/bridgeの挙動を確認してから選び、外部照合のprotocol・利用条件は着手時に再調査する。
追加要求の統合だけを理由に既存reader、ALSA、CEC、metadataを再設計しない。

## 3. 目標構成と所有権

以下は未実装部分も含む目標構成である。現行の所有権を保って段階的に拡張する。

```mermaid
flowchart TD
    Drive[Optical Drive] --> Adapter[Drive I/O adapter]
    Adapter --> Reader[CddaReader / positioning]
    Reader --> Verify[Verification / recovery]
    Verify --> Queue[Accepted PCM + region evidence]
    Queue --> Engine[PlaybackEngine]
    Engine --> ALSA[ALSA / HDMI]
    Adapter -. observations .-> Facts[Bounded observations / events]
    Verify -. evidence .-> Facts
    Engine -. playback position .-> State[main: integrity state]
    Facts --> State
    State --> Snapshot[DaemonSnapshot / diagnostic events]
    Snapshot --> UI[UI / diagnostic log]
```

Drive Adapterは責務の境界であり、初期段階で全backendを巨大なclass階層へ置き換えない。
既存LinuxIoctlReaderのtransport callback、MediaWorkerのcallbackをprobe/試験の差し替え点に使う。
vendor/model固有の対応が必要になったらadapter側へ閉じ込め、適用条件と根拠を公開する。

PCM reader・検証器はPcmWorker側、PlayerStateと公開integrityはmain側で所有する。
検証はPCMを採用する前の処理であり、UIとは独立。観測は小さな値をqueueで渡す。
既存TOC、metadata、CEC、ALSAを別のlibraryへ移さない。追加thread poolは不要。

## 4. 拡張データモデル（未実装部分を含む）

ここでの名称は目標モデルの責務を表す。現行型への追加方法・公開名は実装時に確定する。
既存PlaybackStateを再定義しない。

| 型 | 主な情報・寿命 |
|---|---|
| CapabilityValue<T> | optional value、evidence一覧、観測時刻、適用範囲、probe error |
| DriveCapabilities | identity、DAE、C2 support/trust、cache、accurate stream、speed control、offset |
| DriveState | device generation、接続/probe状態、要求速度・報告速度・実測throughput |
| ReadPolicy | 希望する検証、時間/試行予算、buffer、未解決時動作 |
| ReadStrategy | 選択された手順、使用backend、利用可能な観測、降格理由 |
| ReadState | IDLE/READING/VERIFYING/RECOVERING/BUFFERING/FAILED、read対象区間 |
| RegionIntegrity | 区間、観測・比較・独立性、offset、採用理由、policy revision |
| IntegrityStats | 試行数、採用区間、recovered/uncertain量、外部照合範囲 |
| ReadProvenance | 問題区間のReadAttempt一覧、採用候補、比較とfallbackの理由 |
| PlayerEvent | machine-readable type、severity、presentation、世代・区間・payload |

bool能力はYES/NO/UNKNOWN。数値不明はnull。根拠はKERNEL_REPORTED / DRIVE_REPORTED /
TESTED / DATABASE / USER_CONFIGURED / INFERREDを区別し、raw根拠と試験条件を残す。
相反する根拠も一方を消さず保持し、採用した判断を示す。TESTEDは条件内での確認であり永久保証ではない。

RegionIntegrityは一次元scoreにせず、次の軸を独立に持つ。

| 軸 | 値案・意味 |
|---|---|
| read_status | UNKNOWN / CLEAN / RECOVERED / UNCERTAIN。CLEANは利用できた観測で異常なし |
| local_verification | NONE / SINGLE_READ / BACKEND_REPORTED / MULTIPLE_MATCH。比較対象範囲と回数を併記 |
| read_independence | UNKNOWN / CACHE_POSSIBLE / CACHE_MITIGATED。物理再読込の保証とはしない |
| external_verification | NOT_CHECKED / UNAVAILABLE / MATCH / MISMATCH、照合方式・範囲・confidence |
| offset_status | UNKNOWN / UNCORRECTED / CORRECTED、値・source・補正範囲 |
| c2_status | UNKNOWN / NOT_AVAILABLE / NOT_CHECKED / CLEAN / REPORTED |
| sample_origin | READ / SILENCE / INTERPOLATED。置換した区間を明示 |

C2付きREAD CDはread-only SG_IOの実転送長とstatus/residualを検査する。要求した2352+294 byte/sectorが
全て返った場合だけPCMをdecodeしCLEAN/REPORTEDを付け、不完全な成功はUNKNOWN/errorとして通常audio fallbackへ渡す。
packetは1〜75sector、CD-DA user+C2・subchannelなし/control0のlayoutに限定し、追加subchannelを要求して
小さいbufferをfull replyとみなさない。block単位のC2観測であり、drive-wide trustやpointer位置精度を証明しない。

C2非対応、C2取得を行っていない、取得したが非検出、を区別する。
paranoiaのverify/fixup callback件数をMULTIPLE_MATCHや訂正sector数に直接変換しない。
現行のBACKEND_REPORTEDは「libraryがこの種類のcallbackを報告した」までをevidenceとする。

## 5. 再生位置と根拠の対応

先読み中のLBA、queue内の採用済みLBA、ALSAへ送信済みLBA、再生位置推定値を分ける。
future regionのrecoveryを、現在聞こえるPCMのrecoveryとして表示しない。

PcmBlockへ軽量なregion evidenceまたは有効な参照を付け、部分ALSA write後も範囲を保持する。
一つのblock内で状態が違えば半開区間[start_lba,end_lba)へ分割する。
offset補正後の細かな境界はstereo sample frame単位で併記する。
ALSA delayで消費済みと判定するまで送信済みregionの根拠を保持し、current integrityを引く。
出力device以降のTV/ARC/アンプの遅延やbit一致はこの位置推定と保証の対象外。

device generation（再接続）、disc generation（観測した交換）、stream generation（seek/再開）、
policy revisionを明示し、古いPCM・観測・検証結果を現行状態へ適用しない。
UNKNOWNな区間をtrack全体のCHECKED表示で覆わない。track集計は検査済みcoverageと未検査coverageを持つ。
同じsectorの再試行はattempt数へ、distinctな採用sectorはcoverageへ計上して重複加算を避ける。
seekでcoverageに穴がある場合、track checksum完成とはしない。

## 6. PlaybackMode / ReadPolicy / ReadStrategy

`PlaybackMode → ReadPolicy + DriveCapabilities + backendの観測能力 → ReadStrategy`とする。
direct/paranoiaはbackendでありQUIET/BALANCED/SECUREとは別。paranoiaを選ぶだけでSECUREとはしない。
現行snapshotのrequested_mode=LEGACYとsingle/repeatのReadPolicyを維持し、
QUIET/BALANCED/SECUREを利用可能なmodeとしてCLI/APIで受け付けない。
現行snapshotは`read.strategy`にrequested/effective、pending、downgrade reasonを公開する。effectiveは
現在readerへ適用済みの構成であり、requestedとの差はstream途中でreaderを切り替えない境界を表す。
C2 pointer要求ではprobe前・support=UNKNOWN・support=NO・probe結果が現行streamへ未適用を区別する。
これらはC2 trustやPCM品質を表さない。QUIET/BALANCED/SECUREなど追加modeのstrategyは未実装である。

| 未実装mode | 目標policy |
|---|---|
| QUIET | 騒音と連続再生優先。取得可能な異常時だけ限定retry。無検証を検証済みとは表示しない |
| BALANCED | 通常区間は軽量な観測/overlap、疑わしい区間だけ追加read。将来default候補 |
| SECURE | 同一区間の複数read・cache対策・整列比較を優先。必要ならbuffering待機 |

ReadPolicy候補はminimum_matching_reads、maximum_attempts（初回込み）、region_time_budget_ms、
preferred_speed、noise_priority、target_buffer_frames、startup_buffer_frames、overlap_frames、
c2_policy、cache_policy、allow_playback_stall、unresolved_data_policyとする。
desired_confidenceは手順への要求であり、根拠のない確率値では表さない。
retry回数・比較回数・library内部retryを分け、二重retryで予算が膨張しないようbackend別に計上する。

未解決時の設計候補はSTOP / BEST_AVAILABLE / SILENCE / WAIT_WITH_BUDGETだが、
現行に選択肢が4つあるわけではない。#156の判断と境界は[Unresolved PCM Policy](#unresolved-pcm-policy)を参照する。

現行single/repeatの未解決時方針はSTOPである。read error、短いread、またはrepeatの候補不成立では、当該PCMをqueueへ入れず、workerをFAILEDにしてPlaybackEngineがSTOPへ遷移する。古いblock、未初期化buffer、推測したPCM、無音を代替として出力しない。seek、stop、eject、disc世代変更はworker generationを切り替え、以前のstreamのPCMやpolicyを次のstreamへ混在させない。このSTOPはC2 trustや原盤一致の判定ではなく、未解決PCMを成功として再生しないための現在の安全境界である。

BEST_AVAILABLE、SILENCE、WAIT_WITH_BUDGETは今回採用しない。現行CLI/APIは受け付けず、
代替PCM・追加WAIT・新modeを実装しない。将来再検討する場合も、以下の採用前条件を満たす必要がある。

### Unresolved PCM Policy

2026-10-16、#156の設計判断としてSTOPを既定かつ唯一の実装済み方針のまま維持し、
他3候補は現時点で採用しないことを利用者が承認した。永久禁止ではなく、採用理由と安全性を
再評価できる条件を残す。#11のSTOP実装とPR #154、移管を記録したPR #157を変更する判断ではない。

#### 現行STOPの適用境界

- 未解決とは、read error、短いread、repeatの必要一致数未達、overlap不一致等により、
  現行readerが当該要求の完全な採用PCMを返せないことをいう。workerは当該blockをqueueへ入れず停止する。
- read evidenceのUNCERTAINは根拠の分類であり、採用失敗と同義ではない。完全なreadがretry等でUNCERTAINに
  分類されても、現行policyが採用する経路は変えない。これをBEST_AVAILABLEの実装済みと扱わない。
- STOPでは未解決区間へ新たに採用するPCMがなく、そのsample originを原音・無音・推測PCMとして発明しない。
  失敗時は先読みqueue・保持block・出力待ちPCMも破棄し得る。欠損の直前まで必ず聴ける保証や、
  HDMI/ARC/アンプまで瞬時に無音になる保証ではない。
- 反復・時間予算は既存ReadPolicyのまま（既定2一致、最大3試行、10秒）。新しい外側retry予算を追加しない。
  進行中kernel ioctlは強制中断せず、main loopでの停止とreader所有threadによる後からの解放を区別する。
- session、reader-open device generation、disc/stream generation、policy revisionの有効条件を維持する。
  seek/stop/eject/世代変更後の遅延PCMを採用せず、前streamのblockを失敗区間の代用品にしない。
  STREAM根拠の失効と同disc世代のDISC観測は別scopeであり、stream切替だけでdisc観測を破棄する意味ではない。

#### 候補比較と不採用理由

以下の条件は将来の採用前要件であり、現行機能・利用可能な設定値ではない。
未採用案の数値上限や新field名を既定値として追加せず、採用を再検討する時に確定する。

| 候補 | 想定するPCMの由来・適用前提 | 試行/時間上限と最終動作 | 今回の判断理由 |
|---|---|---|---|
| STOP | 採用不能な要求には代替PCMを提出しない | 現行reader予算内で解決しなければSTOP。ioctl中断成功は主張しない | 継続。未解決PCMを成功として出力しない境界を既存実装・試験で説明できる |
| BEST_AVAILABLE | 同一世代・論理区間・offset条件で実際に取得した完全初期化済み候補だけ。検証条件未達を明示し、候補なしはSTOP | 現行read予算を超える試行を暗黙に足さない。候補の保持数・bytes・選択規則を固定し、不適格ならSTOP | 「最良」を選ぶ根拠と、検証不足PCMを出力する製品要求が未確定。click/異音等の評価もない。一致閾値未達の最多票やC2 reportだけで品質最良としない |
| SILENCE | 新規に合成したzero PCM。実CDに記録された無音と区別し、未取得区間を取得済みにしない | 置換可能な区間長・連続回数・合計時間を有限に定め、上限または位置不明ならSTOP | 原音を欠いたまま進捗が進む。sample origin・欠損量・再生位置・復帰時の連続性を利用者へ伝える契約と聴感評価がない |
| WAIT_WITH_BUDGET | 待機そのものはPCMの由来ではない。成功後に採用する実read PCM以外を出力せず、待機を無音挿入として扱わない | 同一未解決区間の単一deadlineと残attemptを共有し、再開ごとに予算をresetしない。期限切れはSTOP、停止/世代変更は旧待機を無効化して遅延結果を捨てる | 既存repeat/read-stall予算との二重化、buffer枯渇時の出力・進捗停止の仕様が未確定。ioctlを中断できず、単なる待機追加で連続再生を保証できない |

BEST_AVAILABLEが実際に読んだ候補であっても、原盤一致・cache独立性・現行policyの検証成功は別である。
候補の一部を古いPCMで埋めたり、未初期化bufferを採用したり、根拠のない補間で「原音」を推測しない。
SILENCEを将来採用するなら、合成区間はUNCERTAINかつ合成由来と明示し、取得済み/検証済みの集計へ入れない。
いずれも全track/disc取得、checksum完成、外部MATCHへ読み替えない。完全取得入力の適格性は#160で別途定義する。

#### メッセージと表示の境界

現行ReadPolicyのC++型と公開JSONに`unresolved_data_policy`や`sample_origin`というfieldはない。
本節の語は仕様上の方針・由来の概念であり、新しいenum、event、REST/WS field、Settings選択肢や
fixture値を導入しない。[現行メッセージ契約](message-contract.md#unresolved-pcm-policy)の範囲で表示する。
失敗readのUNCERTAINや候補間の一致回数だけから、PCMが採用・提出されたと推定してはならない。

将来の代替出力には、要求した方針/実際の動作、理由、論理区間、実際に読んだ/合成したPCMの由来、
採用量と欠損量、検証不足をsnapshot・event・詳細履歴・Custom UIで一貫して説明する契約が必要である。
WAITでは待機中と再生中を区別し、期限・残予算・最終動作・進捗の扱いを定義する。
既存のCLEAN/RECOVERED/UNCERTAINやstrategy降格理由だけに新しい意味を押し込まない。
field名・型・単位・欠損時・世代・有界保持・旧client互換性は採用前に確定し、今回の公開表へ先取りしない。

#### 他の停止・復旧との区別

#10の端で取得不能な区間とalignment除外は再生前に計算するwindow制限であり、代替PCMや
read failureからのfallbackではない。予定window終端の正常drainと、未解決時のエラー停止を区別する。
#34のread-stall監視とALSA underrun復旧も本Issueでは変更しない。後者は出力経路の再bufferであり、
未解決PCMへの代替やintegrityのRECOVEREDを意味しない。C2 packet failure後の通常readも、
実CDのPCMを再取得するbackend経路であり、SILENCE等の追加出力方針とは別である。
物理USB消失・resetの検出は#88、異常媒体や別driveの実証は#146に残す。

#### 再検討と独立実装の条件

連続再生のために検証不足PCM・合成・待機のどれを許容するのか、利用者の製品上の要求が生じた時だけ
候補を再検討する。具体的な方針、上記の有限上限・最終STOP、公開契約、聴感/操作の受入条件を
別途承認してから、採用する候補に限定した独立実装Issueを作る。今回は採用候補がなく、後続実装Issueを作らない。

| 将来の検証条件 | 最低限確認すること |
|---|---|
| read error/partial/候補なし/一致不足/overlap不一致 | STOPとの出力差、採用/非採用の区間・根拠、初期化済みbufferだけを使うこと |
| retry・repeat・deadline | backend内部retryと方針のattemptを区別し、単一予算、上限でのSTOP、戻らないioctl時のmain-loop応答を確認 |
| stop/seek/eject/disc・reader・policy世代変更 | 待機/候補/出力途中の取消、旧PCM・旧結果の拒否、root sessionとの照合 |
| 出力とcoverage | 有効PCM、検証不足候補、合成、無出力を区別し、提出/消費位置と取得/検証coverageを混同しない |
| REST/WS/history/Custom UI | 欠損・unknown・履歴eviction・再接続でも、表示から原音一致や採用成功を推測しない |
| 構成/実機 | API無効を含む再build・回帰、test-only fakeでの全sampleと有界性、別承認の実機音声/操作評価。fake成功を物理傷やC2 trustの保証にしない |

この設計判断は代替PCM機能の実装・自動試験・実機確認の完了ではない。現行STOPの実装を変えない。
当時の文書整理後、利用者からcommit・push・master統合を別途承認されたが、Issue状態変更・配備は対象外だった。
2026-10-17のIssue整理承認により、PR #234の採否判断を設計成果として#156をCloseした。
追加fallbackは採用せず、実装・配備・実機試験を追加したものではない。

## 7. NO DISCでの能力probe

<a id="drive-capability-v1-completion"></a>
### Drive capabilityのv1完成条件（2026-10-31採用）

利用者の指示により、現在の標準PlayerのDrive capabilityに表示している全項目について、
値を取得・評価し、根拠と不明理由を扱う経路の実装をv1完成の必須条件とする。
未実装の項目を固定のUNKNOWN/null/NOT AVAILABLEで表示するだけでは完成としない。
表示名の変更や項目の非表示だけで、この実装条件を満たしたことにしない。
これは要求の追加であり、以下の現行probeや過去の検証記録を実装済みへ変更するものではない。

| 対象項目 | v1で必要な処理と意味の境界 |
|---|---|
| DAE | デジタル音声抽出の対応情報・読取実績を根拠と条件付きで評価する。アナログ再生commandの対応を代用しない |
| C2 support | drive reportの取得・解釈と失敗理由を扱う。reportの対応可否と実際のC2取得・精度を分ける |
| C2 trust | 条件・範囲を定めた信頼性評価経路を実装する。support=YESや通常CDでC2非検出だけをtrustの証明にしない |
| Read cache | cacheに関する取得・評価経路とその根拠を実装する。cacheの報告と物理再読込の独立性は別とする |
| Accurate stream | 連続性に関する取得・評価経路と適用条件を実装する。小区間一致だけから無条件に対応と推定しない |
| Speed control / Speed request | 対応照会、明示要求、受理・失敗・未要求を区別する。要求受理を実速度の測定結果にしない |
| Speed observed | 実速度の報告・観測を取得する経路を実装する。要求倍速やjob全体の所要時間から実速度を代入しない |
| Read offset | 明示入力の取得・適用・根拠と未設定を区別する。未指定を0にせず、明示0も校正済みと扱わない |

全項目を常にYES/NOや数値で埋めることは要求しない。実装された経路でも、対象drive/bridgeが必要情報を
提供しない、取得が失敗した、評価条件が揃わない等の場合にはUNKNOWNを維持する。
未実装を「driveが情報を提供しない」と読み替えてはならない。根拠のある非対応だけをNOとし、
失敗・欠損・未設定からNOや既定の数値を作らない。実装済みだが未評価である状態と、評価経路自体の未実装も区別する。

UNKNOWNの理由は、取得待ち、未設定、評価未実施・根拠不足、drive/bridgeから必要情報を取得不能、
権限・I/O・応答形式の失敗、観測世代の失効等を、分かる範囲で区別して説明可能にする。
値・根拠・取得/評価状態・理由を混同せず、既知の失敗を単なる「不明」で消さない。
原因が特定できない場合は、その限界を示し、ドライブ故障や非対応と断定しない。
API/診断で理由を追跡できることを要求するが、新しいfield名・enum・wire schemaはこの節では確定しない。

理由を画面でどこまで常時表示するか、詳細表示への導線、文言・配置・可読性は別のUI検討とする。
UIの説明量が未決定でもbackendの取得・評価・理由保持を延期せず、全項目の実装条件も緩めない。
probe/評価は既存のdrive排他・世代失効・有限予算を守り、追加読取や設定変更が必要な試験は別途合意する。
この要件採用だけで自動取得、校正、追加の実機I/Oや権限変更を承認したことにはしない。

受入では、各項目の実経路と公開結果を照合し、取得・評価成功、根拠付き非対応、取得失敗、
未設定/未評価、失効について適用するケースを検証する。固定UNKNOWNでの穴埋めを検出し、
自動試験と対象Piでの確認範囲を分けて記録する。全driveで値を確定できる保証は要求しない。

通常再生の小区間を再読して一致を取るREPEAT検証と、disc全域を対象にlocal SHA-256指紋を生成する
明示PCM取得jobは別の機能である。どちらの成功も、能力全項目の評価完了や外部MATCHの代わりにしない。

### 現行probeの実装範囲

現行probeはread-onlyかつ任意失敗可能。discなしでも取得できる情報だけを返す。
probe failureはcapability NOではなくUNKNOWN+error。probe成功を再生可能化の条件にしない。
現行では起動時にMediaWorkerへ一度投入し、同workerの後続media処理はprobe完了を待つ。
非同期であることはprobeの遅延がdisc認識へ影響しないという保証ではない。

| 情報 | 取得候補と扱い |
|---|---|
| vendor/model/firmware | 現行はsysfs device/vendor, model, revから取得。欠落は不明として扱う |
| kernelが公開する機能 | CDROM_GET_CAPABILITY。driver/interface側の根拠として保持 |
| DAE | MODE SENSE(10) CD/DVD capability page 0x2aのCDDA_CSをDRIVE_REPORTEDとして取得。analog Audio play bitを代用しない。read実績のTESTED評価とは分ける |
| C2 support | MMC `GET CONFIGURATION`のfeature descriptor一覧にあるCD Read Feature（0x001e）のC2 Flagsを`CDROM_SEND_PACKET`で読む。成功時だけDRIVE_REPORTEDのYES/NO。実測trustおよびread時のC2観測とは別管理 |
| accurate stream | 同pageのCDDA_SAをDRIVE_REPORTEDとして取得。PCM連続性の実測・cache独立性とは別管理 |
| read cache | current Caching page0x08のRCDをreported enabled policyへ投影。buffer sizeやdisabled報告を物理cache不存在/再読独立性と混同しない |
| speed control | CDC_SELECT_SPEEDはkernel report。実際の設定可否・効果は別途確認 |
| current speed | 同page14-15のraw kB/sをDRIVE_REPORTEDとして公開。max8-9・要求値・CD/DVD共通x換算を代用せず継続実測とはしない |
| read offset | 未指定はUNKNOWN。起動時の明示値だけUSER_CONFIGUREDで適用。vendor/modelから推定せず、校正/database取得は未実装 |

Linux CDROM_GET_CAPABILITYにはC2 trustやoffsetの直接的な情報はない。
CDC_PLAY_AUDIOをDAE成功の証明として使わない。
CDROM_SELECT_SPEEDは設定操作なので現行のprobeでは実行しない。
C2 supportの初期probeは`CDROM_SEND_PACKET`で読み取り専用の`GET CONFIGURATION`から最大512 byteの
feature descriptor一覧を取得する。command failureまたは不正な応答はUNKNOWN+`probe_error`にし、NOへ
変換しない。Featureが不在という有効な応答だけはDRIVE_REPORTEDのNOにする。応答が上限を超える場合も
UNKNOWNとして扱う。generic packet対応だけで全MMC機能が使えるとはしない。

DAE/accurate streamの追加queryはMODE SENSE(10)、DBD・PC=current・subpage0・allocation512。
Linuxではread-only SG_IOのstatus/residualから実転送長を取り、mode header・block descriptor・全page境界を
その範囲内で検査してから一意の通常page 0x2aのbyte5 bit0/1を読む。未転送の初期化0からNOを作らない。
失敗・欠損・短い/曖昧な応答はUNKNOWNと既存flag.detailに理由を残し、C2/kernelの成功値を消さない。
probe_errorは従来のkernel/C2段階の理由で、空でも追加queryや全項目の成功を意味しない。
追加command timeoutは1500ms設定。MediaWorkerの逐次処理を維持するが、厳密wall-clock上限やdisc認識無遅延の保証ではない。
根拠は[sdparmのpage定義](https://github.com/doug-gilbert/sdparm/blob/master/src/sdparm_data.c)と
[sg3_utilsのMODE SENSE実装](https://github.com/hreinecke/sg3_utils/blob/master/lib/sg_cmds_basic2.c)を照合したlibrary sourceであり、
一次MMC規格の全適合を確認したものではない。page 0x2aは後期MMCではobsoleteで、取得不能ならNOではなくUNKNOWNとする。
raw速度は同じqueryのfield長を確認して読むため追加I/Oはない。0 reportとfield欠損を区別し、0を有効速度へ補完しない。
offset根拠は[cdrdaoのdriveInfo](https://github.com/cdrdao/cdrdao/blob/master/dao/GenericMMC.cc)で照合したlibrary sourceで、
firmware精度・物理回転やCD/DVD倍率を保証しない。
cache policyは同じ有界mode-page parserでcurrent page0x08のbyte2 bit0 RCDを読む。
RCD=0をreported enabledのYES、1をreported disabledのNOとし、未取得/不正をNOにしない。
物理cache有無やCD-DAへの適用/独立性を判定した値ではないため、read strategyのCACHE_POSSIBLEを解除しない。
追加MODE queryは1回/世代、allocation512/timeout1500ms設定でretryなし。独立なpage失敗で他の成功値を消さない。

probeはMediaWorkerで逐次実行し、drive I/OはPCM readerと直列化する。
待機中はUNKNOWNを公開する。hotplug/reset時の能力snapshot無効化・再取得、driveなしとdiscなしの
lifecycle分離は#88の責務であり、#7の初回probeへ含めない。
#88の今回差分では以下の観測世代と失効を実装し、物理試験と検出不能範囲を別に残す。

### Device Lifecycle Observation

`DeviceLifecycleObserver`はowner threadでdevice nodeとread-only sysfs metadataをsamplingし、
main/readerは有界なcacheだけを読む。optical read、journal、root権限、udev triggerには依存しない。
Linuxではblock nodeのmajor/minor/inode、`/sys/dev/block/<major>:<minor>`のresolved pathと、
USB祖先がある場合のbusnum/devnumを内部signatureに使う。vendor/model/TOC/serialの推測はしない。
signatureはOS観測値であって物理同一性ではなく、公開IDとして送らない。

| 観測 | 公開/失効の意味 |
|---|---|
| PRESENT | block nodeと必要metadataを観測した。媒体あり・音声正常・物理同一性を意味しない |
| ABSENT | nodeのstatがENOENT/ENXIO。物理的に抜かれた原因やtrayの空を断定しない |
| UNKNOWN | 初回待ち、unsupported platform、非block path、権限/metadata/format/取得失敗等。ABSENT/NOへ変換しない |
| 初回/状態変化/PRESENTのsignature変化 | 観測context generationをadvanceする。安定したUNKNOWN/ABSENTや同じsignatureではadvanceしない |
| reader failure/stallのmain観測 | 安全側の失効としてadvanceしREADER_FAILUREとする。USB交換/reset確定という意味ではない |

既定sampling間隔は250 msで、初回probeは待ち時間なしでownerが開始する。これは最大検出latencyの保証ではない。
sampling中にmainがreader failureを通知した場合、その前に開始した古いsampleは適用しない。
metadata待ち中もmain用mutexを保持しない。初回観測が返らない場合も500 msの猶予後はUNKNOWN contextで
通常のmedia確認とlegacy single playbackを許すが、未bindのprobe reportでC2 packetを有効にしない。
観測が後から変わればそのcontextも失効させる。

`drive.observation.generation`は同process/session内の失効counterで、既存read evidenceの
`device_generation`（成功したreader openのcounter）や`disc_generation`、`stream_generation`とは別である。
数値が同じでも同一の名前空間ではなく、再起動や同じpath/vendor/modelから物理同一と推定しない。

main ownerはcontext変更時に次を一つの失効処理として行う。

- controllerのvalidated TOCを外してstreamをcancelし、worker自身へreader discardを要求する。
  進行中ioctlを中断・別thread closeせず、retiring I/Oが終わるまで新contextのprobe/controlを発行しない。
- disc世代・disc map・stream履歴・現在PCM・旧event/metadata/artwork/offset windowを失効させる。
  旧probe/TOC/speed/control結果を新deviceへ適用せず、新deviceで旧streamを自動再開しない。
- measured/reportされたcapability・vendor/model/firmware・speed request receiptを消す。
  operatorのrequested policy、設定保存値、configured speed/明示offsetは保持する。USER_CONFIGURED offsetは校正証明ではない。
- fresh probe/media/TOCを新contextで確認してから操作を再開する。UNKNOWN観測ではcapabilityをUNKNOWNのまま扱い、
  legacy playbackを不必要に禁止しない。未知C2をNOにせず、requested/effective/pending/downgradeを区別する。

device再接続でMediaStateTrackerの空tray履歴をresetするため、再接続そのものはdisc挿入autoplayの根拠ではない。
同じdevice contextで実際にempty trayを観測した後のaudio disc挿入は従来どおり扱う。
player/legacy disc statusのNO_DISCはvalidated audio TOCがない場合にも使い、物理drive不在の証拠にしない。
既存controllerが操作可能な間のStop/Pauseはlocal audio制御として扱い、観測contextの再検証だけを理由に拒否しない。

MediaWorkerは共有coordinatorを一箇所で所有し、request選択時の観測generationを保持する。
coordinator取得前後のadmission checkと結果の世代checkを行い、取消された待機中の旧start/stop/speed/ejectを発行しない。
callbackを実際に呼んだ場合だけ結果を作り、skipした操作を成功ackとして公開しない。
許可済みopaque callは中断できず、終了後の旧結果を捨てる。callbackはcoordinatorを再入してはならない。
PcmWorkerもcoordinator取得後にstream世代を再検証し、意図的な取消をread errorにしない。
factory/seek/readと同様、discard・error・shutdownのcloseもowner/coordinator内で行い、queue mutexをI/O中に保持しない。

実readerにはepoch guardを付け、mainが変更を適用する前でも新しい観測世代と異なるseek/readを拒否し、
戻った旧readをqueueへ入れない。すでにALSAへ提出済みのPCMやsampling前のphysical変化まで瞬時に撤回する保証ではない。
`retiring_io`中のinflight metricは旧ownerの解放待ちを含み、新deviceのread成功/失敗へ読み替えない。
destructorのjoinはI/O/metadata probeが戻るまで待つ場合があり、通常main loopでのnonblocking処理とは分ける。

#### Observation Limits

同じnode/signatureのままのreset、sample間に完結して識別情報も戻る交換、libraryが吸収して外へ出ないI/O障害は
検出を保証しない。250 ms間隔は物理連続性の証明でもudev event全件の取得でもない。
reader failure通知は原因を確定せず、曖昧なEIOやread stallをreset/物理交換と断言しない。
新たなUSB/power異常では追加の読出しを止め、原因/物理系の確認と、別承認の抜き差し・故障試験へ分離する。
fakeでの失効検証や正常CD再生だけでphysical hotplug/resetの実機受入を完了したとは扱わず、#88はOpenのまま残す。
速度変更・cache対策の追加前にdevice I/Oの調停を整え、ejectを優先する。
現行probeに能動的cache/C2精度試験やトレイ操作は含めない。

確認資料: 実機の`/usr/include/linux/cdrom.h`、`/usr/include/scsi/sg.h`、
[Linux CD-ROM ioctl](https://docs.kernel.org/userspace-api/ioctl/cdrom.html)、
[Linux SCSI Generic](https://docs.kernel.org/scsi/scsi-generic.html)。
これらはLinux入口の確認であり、MMC全機能やこのASUS driveの能力を確認したものではない。

## 8. bufferと検証・recovery

既存bounded dequeを維持し、必要性が実測で示されるまでring bufferへ置き換えない。
容量と開始閾値はCD frame単位で扱い、read regionの長さと分離する。
現在のsingle/repeatのread量とbufferの既定値は機能設計を参照する。
BALANCEDの10〜30秒は評価候補であり、今すぐ既定値にしない。
PCM単体は176400 bytes/秒なので10秒約1.68 MiB、30秒約5.05 MiB。
比較用候補・overlap・provenance・ALSA bufferの予算も別途上限を持つ。
buffered時間はqueue・engine未送信分・ALSA delayを分け、二重計上しない。

現行repeat verifierは既存seek/readを包み、最初の論理block以外で直前に採用した15 CD frameを先行overlapとして読む。
overlapは出力に含めず、直前PCMとの一致を確認する。比較対象がないstream開始/seek直後は`STREAM_BOUNDARY`、
不一致はfail-closedである。候補PCMは最大8、overlapは最大75 CD frameに制限する。
今後cache軽減を実装する場合も既存seek/readを包むverification layerから必要な前後区間を読む。
paranoiaの連続読み取り状態を壊すstateless read_regionへの全面変更は行わない。
overlap/複数readを提供できるbackendの組合せを明示し、使えないstrategyは降格する。
比較前にbyte order・サンプル位置・offset条件を統一し、overlap分を二重再生しない。
disc端を越える読込は要求せず、検証不能な端区間を明記する。

```mermaid
stateDiagram-v2
    NORMAL --> SUSPICIOUS: anomaly
    SUSPICIOUS --> REREADING
    REREADING --> RECOVERED: policyを満たす一致根拠
    REREADING --> UNCERTAIN: 試行/時間予算切れ
    RECOVERED --> NORMAL: 採用
    UNCERTAIN --> NORMAL: 明示fallbackで継続
```

UNCERTAINからSTOPするpolicyもある。overlap不一致は不連続の疑いであり、欠落/重複の原因を即断しない。
CRCは候補グループ化・診断用。可能なら最終的な一致判定はPCM全bytesで行い、CRC衝突を同一視しない。
「3回一致」は回数と比較条件を示す。single readの独立性はUNKNOWN、現行repeat verifierの採用結果はCACHE_POSSIBLEである。
cache defeatやdistant readをしただけで物理再読込を保証したとはしない。

offsetは正負付きstereo sample frame単位（両channelで同じ時間位置）とする。
採用databaseの符号規約を明記し、補正で必要な端データが読めない場合はその範囲を不明/代替扱いにする。
speed設定非対応ならdrive既定、C2不可なら利用可能なoverlap/repeated read、offset不明なら未補正で再生する。
ALSA underrun復旧は出力継続の処理であり、read integrityのRECOVEREDとは別のeventと統計にする。
同じstreamでは最大3回まで再生位置からreaderを再生成して再開し、上限到達時は停止する。これは
read errorの再試行方針や未解決PCMの扱いを変更しない。

### Offset入力・校正契約（#162の設計、#10の実装）

#162はPR #161で入力・校正契約の文書化として完了した。そのClosedは補正実装の根拠ではない。
#10の今回差分でCLI、reader補正、根拠公開、fake PCM試験を実装し、限定的なPi確認を別に記録した。

`read_offset_samples`はstereo sample frameの符号付き整数とする。論理PCM位置を`p`、
設定offsetを`o`としたとき、補正後に論理位置`p`へ置くPCMは物理reader位置`p + o`から得る。
従って`o > 0`は後方の物理PCMを前倒しにし、論理要求の末尾より後の入力を必要とする。
`o < 0`は先頭より前の入力を必要とする。この規約をdatabaseや校正値の規約と比較せずに
値だけ取り込まない。1 CD frameは588 stereo sample frameなので、補正はCD frame境界に
揃わない値を含み得る。

初期入力は`--read-offset-samples SIGNED_INTEGER`であり、`/etc/default/picdplayer`の
既存`PICDPLAYER_EXTRA_ARGS`からも渡せる。対象は`--player`だけで、起動時に符号付き32-bit整数として
構文とoverflowを検証する。実装はdiscのaudio範囲と一回のreader要求から安全に扱えない値を拒否し、
任意の上限値を黙って丸めない。現行入力はdaemon再起動でだけ変更でき、stream途中やruntime Settingsでは適用しない。

未指定は`read_offset_samples=null`、`offset_status=UNKNOWN`であり、0へ読み替えない。明示した0も
校正値として公開する。値を実際に適用したreadは`CORRECTED`、値が判明しても適用されないreadは
`UNCORRECTED`とする。公開値にはUSER_CONFIGUREDの根拠と物理校正未検証のdetailを伴わせる。
TESTED / DATABASE由来の値を取得する機能は未実装である。明示ユーザー値は将来の
校正・database値より優先し、利用条件・機種/firmware一致・符号規約を検証できないdatabase値は
自動採用しない。

補正入力がdiscのaudio範囲外となる先頭・末尾は、ゼロ埋め・繰返し・推定PCMを生成しない。
その論理区間はcoverage不足として結果・外部checksum入力から除外し、全域照合をMATCHにしない。
seek、disc generation変更、read error、代替PCMも同様にcoverageを途切れさせる。実装時は
正負offset、非frame境界、disc端、partial read、repeat overlap、disc交換をfake PCMで検証し、
校正値の物理確認は#146で別途記録する。

### #10の採用仕様と実装

2026-10-16、Dと追加wire契約の仕様採用、実装・自動試験、Debug配備・限定実機試験を順に承認して進めた。
以下は今回実装した契約であり、A/B/Cは比較の記録として残す。未指定は従来どおりnull/UNKNOWNである。
自動試験と限定的なPi確認は[検証記録](../development/verification.md#10-read-offsetの実装と限定実機確認2026-10-16)を参照し、
物理校正・原盤一致・試聴・全disc完走まで確認済みとは扱わない。

#### 入力と適用

- `--player`限定の`--read-offset-samples SIGNED_INTEGER`を起動時だけ受け付ける。
  十進整数の先頭の`+`/`-`を許し、空・符号だけ・小数・末尾文字・32-bit overflowは起動前に拒否する。
  `/etc/default/picdplayer`からは既存の`PICDPLAYER_EXTRA_ARGS`で渡し、新しい保存schemaやSettings操作は追加しない。
- 未指定はnull/UNKNOWNで従来のread経路を維持する。明示0は既知の値であり、適用成功したreadはCORRECTEDとする。
  値のsourceはUSER_CONFIGURED、detailは明示入力であって物理校正を検証していないことを説明する。
  値の入力だけでTESTED、原盤一致、外部MATCH、C2 trustを成立させない。
- 設定はdaemon起動中に変更しない。変更には再起動を必要とし、reader再生成時も同じ設定を使う。
  CLIの構文検証と、TOC受理後のaudio範囲に対する検証は区別する。discがない起動を失敗させない。
  全audio範囲より絶対値が大きいか等しいoffsetは有効な補正区間を持たないため、TOC受理後に理由付きで再生を拒否する。
  不正値を丸めたり0へ戻したりしない。範囲計算は符号付き64-bitで行う。

#### 座標と取得範囲

sample位置はdisc先頭からの相対値でなく、LBAに588を掛けた絶対stereo sample frame位置とする。
TOCのaudio範囲を`[B,E)`、論理要求を`[p,p+n)`、offsetを`o`とすると、必要な物理PCMは
`[p+o,p+n+o)`である。補正可能な論理範囲は`[B,E)`と`[B-o,E-o)`の共通部分となる。
要求のうちこの共通部分に含まれない区間を、端で取得不能なsample区間として明示する。

例えばaudio範囲が`[0,5880)`なら、`o=+1`では論理`[0,5879)`が補正可能で末尾1 sampleが不足する。
`o=-1`では論理`[1,5880)`が補正可能で先頭1 sampleが不足する。`o=+588`なら末尾1 CD frameが不足する。
内部track境界にはdisc端と同じ欠損を作らず、隣接audio trackから必要なPCMを取得できる。
pregap、lead-in/out、負LBA、mixed-modeへの対応は追加しない。

物理要求はsample範囲を含むCD frameへ外側に丸める。開始は`floor((p+o)/588)`、
終了は`ceil((p+n+o)/588)`とし、取得後に前後の余分なsampleを除く。
全要求がaudio範囲内なら、CD frame整数個の論理要求に必要な物理frame数は論理frame数かそれより1多い値になる。
offsetの絶対値分のpaddingを確保しない。要求量・追加1 frame・repeat候補数からmemoryを有界にし、
sizeの積算とLBA変換時にもoverflowを拒否する。実際のI/Oは既存DriveAccessCoordinator内で行う。

#### 端区間の選択肢と推奨案

| 案 | 再生への影響 | 今回の判断状態 |
|---|---|---|
| A: 要求全量を作れなければ非採用・STOP | 負offsetで先頭再生不可、正offsetで末尾前STOPになり得る。1 sample不足でもblock・先読みqueue・出力待ちPCMへ影響する | 初期仕様には非採用。最初に提示した最小変更案として記録を保持 |
| B: 補正可能なsample範囲だけを再生 | 先頭/末尾が短くなる。sample単位の開始・終端・進捗・drain契約が必要 | 初期仕様には非採用。必要になれば追加設計と承認を要する |
| C: 端だけ未補正または代替PCMで継続 | 補正条件やsample originが区間で変わる。無音等は#156の採否にも関係する | 今回は採用を提案しない。自動fallbackを実装しない |
| D: 補正可能な完全CD frameだけを再生 | Bを内側のCD frame境界へ丸める。物理的な不足に加えて最大587 sample frameを除外するが、既存の整数CD frame契約を維持できる | 採用・実装、fake PCM成功。USB異常等による停止履歴を残し、利用者確認後に最終packageのPi 6条件成功。校正・試聴は未確認 |

Aでは、範囲外I/Oを発行せず、当該要求を失敗としてPCMをqueueへ入れない。
既存read failureと同じ停止経路を使う案であり、block途中までを必ず聴ける、または不足sampleの直前で
正確に停止する保証ではない。既存workerが未提出queueを破棄する影響も含む。正常なdisc終端drainとは区別する。
`PcmWorker::run()`はread失敗時にqueueをclearし、`PlaybackEngine::tick()`はerror経路で保持blockを
破棄してoutputをresetする。この経路の流用では、取得不能な末尾1 sampleだけでなく、既に読めた先読みPCMや
出力待ちPCMも失い得る。短いdiscや終端付近のseekでは再生開始前にSTOPする可能性もある。
従ってAを「端の不足分だけを止める」仕様として採用しない。取得済みPCMをdrainしてから不足境界で止める案も、
既存read errorとは異なる終了契約が必要であり、Aの最小実装へ黙って追加しない。
取得可能な入力のpartial readやread errorでも不完全PCMを採用せず、ゼロ埋め・反復・推測PCMを生成しない。

#### Dの採用仕様: 事前に限定する再生範囲

補正可能な論理sample範囲を`[C0,C1)`とする。`C0=clamp(B-o,B,E)`、
`C1=clamp(E-o,B,E)`であり、TOCの先頭と末尾は588の倍数である。
Dの再生範囲はCD frame座標の`[L,R)`、`L=ceil(C0/588)`、`R=floor(C1/588)`とする。
`L>=R`なら再生可能な完全CD frameがないため、再生を理由付きで拒否する。
補正可能なsampleが残っていても再生不可になり得る点を隠さない。

全discでの物理的に取得不能な論理sampleは`[B,C0)`と`[C1,E)`、
追加のalignment除外は`[C0,L*588)`と`[R*588,C1)`である。
空区間は省略し、再生範囲が空ならalignment除外は`[C0,C1)`一つだけとする。
これらはTOCと設定から計算した可否であり、取得済みcoverageではない。
例えば10 CD frameのdiscで`o=+1`なら、物理不足は末尾1 sample、alignment除外はその直前の587 sample、
再生範囲は最初の9 CD frameである。`o=-1`なら先頭側に同じ除外が生じ、2番目のCD frameから再生する。

再生前にwindowを確定し、workerはその範囲内だけを読む。先読みが`R`へ達しただけではcontrollerを停止せず、
成功したqueueを順に出力し、既存のnonblocking drainが完了してからSTOPPEDに戻す。
これは「指定windowの供給終了」であって、未取得のdisc端を含む全disc再生・全域照合の成功ではない。
正常window終端をread errorやUNCERTAINとして偽装しない。実際のread error、partial read、timeoutは
従来のfail-closed STOPを維持し、未知の欠損をこの予定された端除外へ読み替えない。

操作の候補位置はcontrollerのTOC座標で求めてから、選択trackと`[L,R)`の共通部分で検証する。
play、自動再生、track/next/previousでは、その共通部分が空なら操作を理由付きで拒否し、
別trackへ黙って飛ばさない。共通部分があれば位置をその範囲内へclampする。
seekは従来どおりtrackを跨ぐ意図を許し、disc全体の`[L,R)`へclamp後、実際のtrackを再計算する。
PAUSED中の位置変更も同じ規則を使う。適用可能な操作は通常の状態更新経路へ渡し、
拒否した操作は再生状態・位置を変更しない。NO_DISCの動作と明示stop後の先頭track復帰は従来どおりとする。
play再開時に範囲を検証し直すため、STOPPEDの先頭位置が除外区間でも未補正PCMを出力しない。
TOC、曲長、`disc.layout`を短縮・付替えせず、表示進捗は元の論理位置で示す。
従って先頭から0でない進捗で始まり、元の曲長へ到達せず終了する場合がある。

offset指定時の端除外は技術診断と標準Playerの既存offset表示付近に、未補正/代替出力ではない旨とともに表示する。
read failureのsticky warningや#164のpolicy/strategy表示へ流用せず、Monitorの2列構図は変えない。
必要な最小表示を#10の実装scopeに含めるが、Custom UIへ新しい画面実装を義務付けない。

#### 根拠・coverageの公開契約

現在のCD frame単位の`frames_read`、`accepted_unique_frames`、disc mapと、補正用のsample単位範囲を混同しない。
既存fieldの型・単位を変えず、追加情報として次を持つ。
field名・型・null・保持境界は[Read Offset契約](message-contract.md#read-offset)を正とする。

| 情報 | 要求する意味 |
|---|---|
| 設定値・source/detail | 起動設定の根拠。probe結果で上書きせず、drive自身の校正reportとは呼ばない |
| 論理要求・必要な物理sample範囲 | 補正前後の対応。実発行したsector範囲と、範囲検証だけで拒否した要求を区別する |
| 端で取得不能な論理sample区間 | 上記の共通部分から算出する。read失敗で位置が不明な欠損を推測しない |
| 採用した補正PCMの範囲 | 完全成功した論理要求だけ。Aの非採用block、Dのalignment除外、物理的な不能区間を同じ欠損量にしない |
| readごとのoffset状態 | 未指定はUNKNOWN、適用して採用したPCMはCORRECTED。既知でも非採用のreadを補正成功と表示しない。非採用時の既知値と適用失敗理由を併記する |
| 結果の有効条件 | session、reader-open device generation、disc generation、stream generation、policy revisionと対応させる |

設定の既知性とPCMの適用結果を分け、API無効buildでもPCM側の根拠を保持する。
有界履歴がevictionされた場合、残る集計をsample全域の証明に使わない。offset適用だけで全track/disc取得や
外部照合成功を成立させない。全域checksum入力の完全性は#160の別契約であり、今回実装しない。

repeatは、同じ論理要求へ同じoffsetを適用した候補PCMを比較し、先行overlapを出力から除く。
補正用の余分なsectorを採用coverageへ二重加算しない。repeatはoffset適用readerを包み、
論理範囲の候補を比較する。既存attemptのphysical値は実backendのsector座標を維持し、
比較した論理範囲は別fieldで示す。補正用sectorで得たC2/backend/retry観測は、その物理範囲の観測として保持し、
除去したsampleでの異常が残る場合もPCM本体の正確な異常位置を特定したとは表示しない。
seekでoverlapの前回tailを捨て、stop・disc変更・設定再起動では旧PCMと根拠を採用しない。
これは#88の物理交換/reset検出を追加するものではない。

#### 受入条件と検証境界

| ケース | 自動検証で確認すること |
|---|---|
| 未指定と明示0 | 未指定のPCM/read動作が不変、nullと0・UNKNOWNと適用成功が別になる |
| 正負1、587、588、589 sample | channelを取り違えず`p+o`のPCMと全sample一致。sector丸めと余分なsampleの除去が正しい |
| 起動入力 | `+`/`-`、32-bit両端、overflow、空、符号だけ、小数、末尾文字、非player指定を検証する |
| 小さいdisc・極端な値 | TOC後の適用不能を理由付きで拒否し、wrap・巨大allocation・範囲外I/Oを起こさない |
| track/disc境界 | 内部track境界で欠損なし。disc端は採用した契約どおりで、不能sample・alignment除外・非採用範囲を区別する |
| Dのwindowと終端 | 正負offsetで可否範囲を事前算出し、短いdiscの空windowを拒否。短い最終blockでもqueueを捨てずdrain後停止する |
| Dの操作と表示 | play/自動再生/track/pause再開/seekを共通windowで検証。対象trackの共通部分なしは別trackへ飛ばさず拒否。元TOC進捗と端除外の表示を照合する |
| partial/read error/timeout | 不完全PCMや旧bufferを採用せずSTOP。ioctlの強制中断を成功条件にしない |
| single/repeat | 補正とoverlapによる重複/欠落なし、不一致時の非採用、retry/試行数/C2根拠を維持する |
| seek/stop/再open/disc変更 | 遅延旧readのPCMと根拠を拒否し、新世代へ混在させない |
| 公開経路・互換性 | Presentation ModelからREST/WS/historyまで値・source/detail・範囲が一致。旧fieldの単位不変、欠損を推測補完しない |
| 構成回帰 | source再build後の関連試験・全CTest・API/metadata無効構成。fixtureは公開済み契約に合わせて追加する |

sample位置とchannelを識別できるfake PCMを`read_offset_test`で全sample比較し、
ScriptedCddaReaderでは遅延旧readと世代失効を検証する。通常のscenario harnessも回帰対象だが、
物理校正・drive cache・試聴・TV表示の確認には使わない。
今回のPi試験は通常CDの短時間single/repeat、pause/seek、端151 CD frameの再生と正常停止に限定した。
試験値0/±1は校正値ではない。物理校正・別driveは#146と連携し、未確認条件を残したまま#10をcloseしない。

## 9. provenance・統計の上限

以下は詳細provenance拡張の要求であり、現行ReadEvidenceとstream集計に対する追加仕様である。

ReadAttemptは区間、attempt番号、digest/比較結果、C2情報、read所要時間、cache対策、errorを持つ。
ReadProvenanceはaccepted attempt/candidate、matching count、acceptance_reason、policy revisionを持つ。
正常区間は同じ根拠をまとめ、問題区間だけ詳細を残す。

拡張時の暫定容量はrecent events 256件、問題region詳細128件、各regionのattempt詳細16件。
現行snapshotのrecent_events上限64件とworker event queue上限256件とは区別する。
件数に加えbytes上限を設定し、overflow/eviction数とdetail_availableを公開する。
長い履歴を捨ててもaggregateと現在再生に必要な根拠は保持し、未記録を「問題なし」にしない。
統計はdisc/stream別のscopeとcoverageを付ける。unknown件数を0件成功として表示しない。
永続JSONLログは任意。容量・rotation・書込み失敗を制限し、worker/mainを同期disk writeで待たせない。
永続ログを実装する段階で専用のbounded出力経路を検討する。

## 10. player → UI protocol

現行GET /api/stateおよびWS /api/eventsはschema_version=1、drive/read/recent_eventsを含むsnapshotを返す。
拡張時もsnapshot形式を維持する。追加fieldを無視できるclient方針と、破壊的変更時のversion更新を定める。
`persistent state`は再接続時に取得できる現在値の意味であり、disk永続化を要求しない。

目標snapshotはrequested mode/effective strategy、capabilities、read activity、buffer、
current playback regionのintegrity、coverage/stats、active warnings、last_event_sequenceを含む。
警告が解消したかもstateで保持し、eventを逃しても復元できる。
UIはread activityとcurrent playback integrityを別表示する。

現行では直近64件のbounded eventをsnapshotの`recent_events`に含め、既存WebSocketでも
snapshot更新として配信する。通常成功はDEBUG eventとして保持して行ログへ出さず、RECOVERED/UNCERTAINを記録する。
現行sessionの行ログは非DEBUG eventをWARNで出すため、event内のseverityとloggerのlevelは一致するとは限らない。
worker側のevent queueは256件を上限とし、溢れは`read.dropped_events`に表す。
履歴の再送、gap検出、詳細provenanceが必要になる段階では新しい`WS /api/diagnostics`（仮称）を用意し、
既存eventsへ異種messageを混ぜない。将来のイベント例:

```json
{
  "schema_version": 1,
  "session_id": "daemon-instance",
  "sequence": 42,
  "disc_generation": 3,
  "stream_generation": 7,
  "monotonic_ms": 123456,
  "type": "read_retry",
  "severity": "INFO",
  "presentation_priority": "ACTIVITY",
  "region": {"start_lba": 18342, "end_lba": 18343},
  "payload": {"attempt": 3, "reason": "pcm_mismatch"}
}
```

event sequenceとsnapshot revisionを分ける。再起動でsession_idを変える。
接続時には最新snapshotを取得し、そのlast_event_sequence以後を適用する。
順序境界はmain threadで確定し、ring保持範囲外ならgapと最新snapshotを返す。
replayはbounded、slow clientはcoalesce/dropまたは切断し、audioをbackpressureしない。
drop件数を明示し、欠けたprovenanceを再構成したように見せない。

severityはDEBUG/INFO/WARNING/ERROR、presentationはBACKGROUND/NORMAL/ACTIVITY/IMPORTANT/STICKY。
recovery進行は通常INFO+ACTIVITY、未解決PCMはWARNING+STICKY。
backendはreason codeを送り、UIが翻訳・dedup・coalesce・最小表示時間・priority overrideを行う。
最小表示時間の初期候補は1秒。sticky解除条件はwarning stateの解消/確認とし、通常progressで消さない。

NO DISC画面はdrive名・能力・根拠・UNKNOWNを表示する。discなし時にintegrityをCLEANと表示しない。
通常画面の1行statusは読み取り/先読み/比較/回復を表現し、詳細から採用理由へ進める。
"PCM verified locally"単独の断定表示は避け、「同一区間を3回比較し一致、cache独立性不明」等の根拠にする。
NO DISC表示やtechnical statusは既存の診断画面で扱う。Chromium kioskは別serviceとして導入済みで、
通常はNow Playingを表示する。有界provenanceとactive warningは実装済みである。

## 11. 外部検証

外部PCM検証のproviderを実装する前に、protocol・checksum・offset規約・利用条件を一次資料で確認する。
MusicBrainz Disc IDや曲名一致をPCM検証と混同しない。MusicBrainz Disc IDはTOC由来であり、
PCMの同一性を表さない。AccurateRipの第三者アクセスは承認済みプログラムに限定されるため、
PiCDPlayerは明示的な合意なしに既定providerとして利用しない。

CTDBは候補として調査したが、公式資料はCD全体をripして照合する方式を説明している。
現行のbounded再生queueと限定したread evidenceは全disc PCMを保存・証明しないため、
再生中に観測した部分PCMからCTDBのMATCHを導かない。外部照合を実装する場合は、
完全取得PCMのcoverage・generation・再生とのI/O調停を#160で先に定義する。
その取得は通常再生のcritical pathに置かず、通常起動時に自動で開始しない。
外部照合は方式・version・track identity・coverage・offset・confidence値を伴う独立した結果とする。
confidenceを一般的な正解確率へ変換しない。MISMATCHだけでdrive故障を断定しない。
track末尾で結果が出てもよい。seek/skipや未解決・代替PCMによりchecksum入力が不足した場合は
NOT_CHECKED/UNAVAILABLEと理由を返し、追加rippingを自動で開始しない。
外部照合失敗でも再生を止めず、履歴のlocal evidenceを上書きしない。

### Complete PCM Input

2026-10-16、#160は設計だけを進めるものとして、PCM非保存のincremental方式、明示開始、
通常再生との同時取得なし、全域coverageの厳密判定を利用者が承認した。
以下は当初production取得の将来契約だった。後述C/D開発差分ではlocal指紋の明示jobを実装し、
通常のPlayer snapshotと起動/再生動作へ自動取得や外部照合の意味を追加しない。
#236でpure input core、#237で注入readerを所有する内部workerをtest-onlyとして追加した。
2026-10-19のC/D開発差分でlocal SHA-256と実session/factory/APIへ接続するが、
入力完全性やfake取得を実機full-disc captureの確認済みと扱わない。外部providerは未接続のままである。
COMPLETE等の語は専用jobの状態で、既存`read.activity=COMPLETE`の意味を変更しない。
#13はClosedだが外部provider互換checksum/transportは未実装である。PR #161は制約整理であり、外部照合完成の根拠にしない。

#### 対象と完全性

取得jobは有効なaudio-only TOCの全track番号・開始LBA・leadoutと、session、disc generation、
専用job識別子、capture readerのopen incarnation、read policy revision、offset値/source、
canonical profileとconsumerのalgorithm/versionを開始前に固定する。再生workerのstreamとは別の識別範囲であり、
reader-open incarnationを物理deviceの交換/reset検出と同一視しない。

最初のaudio track開始を`B`、leadoutを`E`（CD frame座標）とし、全域は半開区間`[B,E)`とする。
first trackが1でない場合もTOCどおりに扱い、`B`より前のpregap/lead-inやleadout以降、mixed-modeは対象にしない。
各trackはTOCの連続した区間であり、全域の長さは`(E-B)*588` stereo sample frame、byte数は`(E-B)*2352`である。

COMPLETEは以下をすべて満たしてmain ownerが確定した場合だけ成立する。

- 専用jobのreaderが、固定した全論理範囲を順番に取得し、完全な採用PCMだけをconsumerへ渡した。
- 各blockの開始位置が直前の確定末尾に一致し、各trackとdiscの確定末尾が正確にTOC末尾へ達した。
  retry/overlap/offset用guard sectorを重複計上せず、byte総数だけの一致をcoverageの証明にしない。
- 全blockのconsumer処理と最後のfinalizeが成功し、取消・失敗・期限切れ・容量超過がない。
  consumer失敗後は部分的なdigestを捨て、同じstateへ再送や続きを継ぎ足さない。
- 完了通知の適用時にも開始時の有効条件が一致し、取消要求が先に処理されていない。
  最後のI/O後にもTOCを再検証し、reader解放まで確認してから確定する。

連続prefixとtrack末尾の台帳を専用jobで保持し、再生の128件履歴やdisc mapから再構成しない。
欠落・重複・順序逆転・位置不一致は失敗として打ち切り、seekで穴を埋めて同じjobを再開しない。
既存のread policy内で回復して完全採用に至ったblockは扱えるが、retryやUNCERTAIN等の根拠を消さない。
COMPLETEは取得範囲とconsumer処理の完全性だけで、全PCMがCLEAN、物理再読、校正済み、原盤一致という意味ではない。

未完了は同じ有効条件のjobが取消・read失敗・期限等で完走しなかったこと、無効は世代やprofile等が変わり
当該結果を現在の入力へ適用できないことをいう。未完了では最後に確定した全blockのprefixだけを下限として示し、
部分digestを全域checksumとして公開しない。開始拒否はjobの完走失敗と分け、drive I/Oを発行しない。
過去の完了記録を保持する場合も、条件変更後の現物discへ再関連付けしてはならない。

#### Offsetとcanonical PCM

初期のcanonical profileは44.1 kHz、signed 16-bit、stereo、L/R交互、2の補数、little-endian、
headerなしのbyte列とする。1 stereo sample frameは4 bytesで、host native memoryをそのままhashしない。
gain、resample、de-emphasis、補間、zero padding、track間の追加無音は行わない。
例えばL=`0x1234`、R=`-2`は`34 12 FE FF`、L=`-32768`、R=`32767`は`00 80 FF 7F`となる。
track境界を跨いでも同じ論理順序で処理し、consumerが必要とするtrack別stateを境界で切り替える。

正規化は#10の`logical p <- physical p + o`に従う。初期profileは全`[B,E)`を要求し、
offset未指定を0とは推定しない。明示0は全域を取得できても、物理校正済みとは扱わない。
非0 offsetでは現行の安全なaudio範囲外のsampleが必要になるため、端不足を検出して開始前に拒否する。
#10の再生windowだけを最後まで読んでもCOMPLETEにせず、alignment除外・不能区間は全域不足として扱う。
providerが別の端除外やoffset探索を要求する場合も、暗黙にtargetを短縮せず別profileの設計承認を必要とする。

consumerは入力profile、algorithm/version、必要state量、処理単位・上限、既知vectorを持つ既知の実装だけを使う。
consumerはcapture worker上の有界なCPU処理とし、network/disk I/Oを行わない。chunkの正常復帰で
全byteの消費を確認し、block全chunkの成功後にだけcoverageを進める。例外後のconsumer stateは再利用しない。
設計当初はprovider/checksum algorithmを採用しなかった。2026-10-19に後述のlocal SHA-256 profileだけを
利用者が採用し、外部providerは未採用のままとした。未確定/非対応consumerでfull-disc readだけを先に始めない。
canonical byte列のprofile一致とprovider protocolの適合性は別で、例えばこのbyte列をCTDBへそのまま送れるとは主張しない。
AccurateRipへの無許可アクセス、submission、修復、PCM uploadは行わない。

#### 取得・取消とI/O排他

初期版は一つの明示jobだけを受け付け、PLAYING/PAUSED中は開始しない。STOPPED、ALSA出力停止、
playback queueの取消とreader解放、audio-ready TOC、有効なprofile/consumer、全域適格性を開始前に確認する。
通常起動、disc挿入、通常再生、外部照合失敗から自動開始・再取得しない。
専用workerがreaderのopen/seek/read/closeを所有し、既存DriveAccessCoordinatorでmedia/probeも含め直列化する。
単なるmutexは公平性や優先割込みを提供しないため、main ownerが新規capture I/Oの発行を明示的に止める。

| 事象 | 初期版の動作 |
|---|---|
| 通常再生中/出力未解放で開始要求 | 開始拒否。通常再生をreset・pause・中断してcaptureを開始しない |
| stop/取得取消 | jobを即座に取消扱いにし、新規capture I/Oとconsumer入力を止める。reader解放は別途待つ |
| play/seek/track/eject要求と競合 | captureを取消し、解放前の操作は未実行として理由を返す。main loopで待たず、自動予約もしない。解放後の再操作で通常経路へ進む |
| read/consumerの失敗・timeout | jobを未完了として打ち切り、checksum候補を捨てる。既存STOP方針以外のPCMを生成しない |
| disc/reader/policy/profileの変更 | jobを無効化。古いjobの完了通知や部分consumer stateを新条件へ適用しない |
| USB消失/reset/電源異常を観測 | jobを無効化して追加readを止める。自動再試行せず、物理系の確認へ分離する |
| 最後のblock完了と取消が競合 | main ownerの確定順で判定し、取消が先ならCOMPLETEを確定しない |

取消・期限は進行中kernel ioctlを強制中断しない。main loopは取消済み/解放待ちを区別し、
readerが戻らない間に「解放済み」として再open・再生・ejectを発行しない。遅延結果はjob tokenで捨てる。
取消でtokenを失効させても解放待ちは占有枠を維持し、旧readerを残したまま新jobを開始しない。
正常なcapture終端はreaderとconsumerの終了であり、ALSA再生やdrain完了を意味しない。
通常再生への非干渉とは、job不在では取得経路が動かず、競合時もmain loopをblocking waitさせないことをいう。
明示取得中に即時再生できる保証ではない。物理I/Oの解放待ちと再操作の必要性を隠さない。

#### Memory・期限・結果の有効条件

全disc PCMのvector/file、内部spool、WAV、disk cache、途中再開checkpointは作らない。
consumerへ順次渡したPCMは不要になり次第破棄し、再起動時はjob・部分digest・完了manifestも失効させる。
次は初期設計の上限であり、現在のCLI/APIの値ではない。将来拡張する場合は別途レビューする。

| 対象 | 初期設計上限と拒否条件 |
|---|---|
| 同時取得/結果 | active job 1件、直近terminal manifest 1件。manifestのエンコード済み表現は最大64 KiB、永続化なし。native保持量も別途合計memoryへ含める |
| 新規所有memory | PCM候補・scratch・consumer state・台帳・manifestを合計8 MiB以内。実装時にallocation/high-waterを検証。thread stackと既存backend/library内部は別見積りで、process RSSの上限とは呼ばない |
| consumer state/canonical scratch | stateは宣言量1 MiB以内、canonical変換は最大16 KiB chunk。上限・処理量を宣言できないconsumerは開始拒否 |
| read構成 | singleは15 CD frame、repeatはregion最大75、先行overlap最大75、候補最大8。固定した有効policyが上限外なら開始拒否し、黙ってpolicyを書き換えない |
| job deadline | 操作者が有限時間を明示し、最大3時間。monotonic clockで全取得・finalizeを含め、再試行ごとにresetしない。既定の自動実行時間は設けない |
| call deadline | 既存effective read予算と残job時間の短い方を監視。超過時はjobを失効させ、ioctlは返るまで解放待ちとする |

候補8・region75・overlap75の物理入力は1回最大151 CD frameである。候補8+scratch2のPCM、
16 KiB変換chunk、1 MiB consumer、128 KiB台帳/結果を数える例は4,747,552 bytesで、8 MiB内に収まる。
これは実装のheap測定ではない。vector capacity/一時copyを含め上限内に保つ責任は実装側にあり、
不明なlibrary内部のmemoryまで有界と推測しない。diskを使わないことを理由にallocation failureを無視しない。

結果は同じsession/job/TOC/観測世代/profileに対してだけ有効。新jobや条件変更では古いmanifestを適用せず、
破損・不正長・不明version・consumer失敗は無効とする。完了結果にも範囲・offsetの根拠・異常観測を残す。
同じTOCやvendor/modelだけで物理同一性を保証しない。未観測の交換/resetは現行generationでは検出できない。
本契約の完了は観測された有効条件に対する入力完全性であり、現物discの同一性証明ではない。
production統合では#88の消失/reset/同一path再接続の失効成果と結び付け、古いcapture結果の自動再関連付けを禁止する。

#### 後続の独立実装単位

| 単位 | 単独の完了条件と依存 |
|---|---|
| [A: 完全性・canonical/incremental core (#236)](https://github.com/udonchan/picdplayer/issues/236) | A区間でpure coreとtest-only対象を追加。TOC全域、連続prefix、track境界、byte order、宣言上限、取消/finalize競合を検証し、後続Dからreuse。core自身はprovider通信・drive I/O・公開APIを持たない |
| [B: 排他的取得worker (#237)](https://github.com/udonchan/picdplayer/issues/237) | B区間で注入reader所有・coordinator排他・call/job予算・取消/release待ち・旧結果拒否を内部workerへ実装しfakeで検証。後続Dで実session/factoryと接続するが、実drive受入は未確認 |
| [C: checksum profile/consumer adapter (#238)](https://github.com/udonchan/picdplayer/issues/238) | 2026-10-19にprovider非依存local SHA-256を採用。consumer/core試験を経てD開発差分から利用。外部互換profileは公式protocol・利用条件の別採用が必要。通信/外部結果を実装済みとしない |
| [D: 明示jobのproduction統合と公開 (#239)](https://github.com/udonchan/picdplayer/issues/239) | 2026-10-19のlocal差分で実session/raw factory/API/Settingsへ接続。開始拒否/取消/release待ち/手動再操作、有効なmanifestを実装。実drive取得・ALSA/物理失効・UI実機評価は別承認で未確認。最終UI再設計やprovider通信は含めない |

設計PR #235段階では、利用者の承認は文書の統合、A/B/C/Dの実Issue作成とAだけの実装・自動検証・統合だった。
その時点ではB/C/Dの実装、providerアクセス、実機操作、Issue closeは対象外で、表は分割案だった。
PR #235の設計統合後に上記4 Issueを作成し、#160のsub-issue、BのA依存、DのA/B/C/#88依存を記録した。
その後#237だけも実装・自動検証・commit/push/統合まで追加承認された。当時はC/D、provider、実機操作、Issue closeは対象外だった。
2026-10-17のIssue整理承認により、#160は設計・作業分解、#236/#237はPR #240/#241のtest-only実装を
それぞれの限定scopeの完了としてCloseした。親の設計完了を子のproduction完成まで待たせず、
C/Dの未実装と#88の物理受入未完了は維持する。実装・公開契約・実機挙動はこの整理で変更しない。
#13が必要とする完全取得入力はA/Bだけでは成立せず、
profile適合とDの受入まで必要である。providerの調査は並行できるが、全域coverageがない入力を外部照合へ渡さない。
#13のClosedを機能完成や依存充足に読み替えず、再openもしない。

#### 将来の受入試験

正常な全域、非1始まりTOC、分割境界、gap/duplicate/out-of-order、partial/zero/error、
overlap/guardの二重計上、offset未指定/0/正負、consumer例外、容量超過、deadline、seek/stop/eject、
disc/reader/policy交換、遅延旧結果、再起動、最終blockと取消競合をfakeで検証する。
完全readがUNCERTAINの場合と、未解決で非採用の場合を区別し、前者も原盤一致とは表示しない。
実機では別承認の開始/取消・通常再生への復帰・電源/USB安定性・音声/操作影響を確認する。
PR #235では設計だけを整理し、checksum計算・capture・provider・自動ripping・PCM保持・実機操作は実装/実行していない。

#### Pure Core Implementation

#236の[CompletePcmInput](../../include/complete_pcm_input.hpp)はthread-confinedな入力モデルであり、
A区間ではCMakeの`complete_pcm_input_test`だけへcompile/linkした。後述D開発差分ではAPI有効時のdaemonにも接続する。
呼び手はjob専用のtrusted consumer、固定identity/profile/TOC/policy、有限durationとmonotonic clockを渡す。
初期版のoffsetは明示0だけであり、原盤一致・校正済みを示す値ではない。

`append`は正規化後のReadResultと完全なPCM spanだけを受け、全blockのconsumer処理成功後にprefixを進める。
trackを跨ぐspanも境界で分け、最大16 KiBのscratchでS16LEを順次生成する。全域PCMは保持しない。
`read_observations`は受理したread結果の観測であり、consumer確定prefixとは別である。
consumer途中失敗時にread観測が残っても`accepted_frames/accepted_bytes`は進めない。

`prepare`は全域とconsumer finalizeの成功を確認するだけで、resultをまだ公開しない。
`confirm`で同じidentityと最終TOC、reader解放の申告を確認して初めてcoreのreceiptをcompleteにする。
これはpureなgateであり、実TOC読出しや物理releaseの検出はB/Dの責任である。
取消が先ならreceiptを作らず、callback内の取消/失効はcallbackから戻ってからconsumerをdiscardする。
完了済みでもcontext変更の確認や明示invalidateでresultを非公開にする。新jobへ自動移植しない。

repeatのoverlap意味は既存verifierを正とする。先頭STREAM_BOUNDARYでも要求量は15、実比較は0であり、
次の比較量は直前block長と要求量の小さい方になる。SINGLEへrepeat証拠を渡す設定不一致は拒否する。
入力のUNCERTAIN分類だけでは非採用にせず、現行readerが採用した完全性と既存policyの成立を検査する。

上限判定はcore所有scratch・有界なTOC/identityとconsumerの宣言量・結果spanに対するもので、
任意callbackの隠れたallocationやI/Oをsandbox化してはいない。consumer resultは最大64 KiB以下だが、
core単独ではmanifest全体のencoding/容量、globalなactive slot、実I/O監視や物理releaseを判定しない。
内部I/O監視・release待ちは下記B、production admission/公開とprovider採用はC/Dに分離する。
coreの`call_budget`は残予算計算と期限判定であり、ioctl/CPUを中断するwatchdogではない。
millisecond未満の残予算は切り捨てて0とし、新しいreadに残job時間を超える予算を与えない。

#### Acquisition Worker Implementation

#237の[CompletePcmWorker](../../include/complete_pcm_worker.hpp)は明示使用の内部componentで、
B区間では`complete_pcm_worker_test`だけにlinkした。後述D開発差分ではdaemonのmain/session/APIと接続する。
constructorはowner threadを作るだけで、job受理までreader factory/TOC probeは呼ばない。
callerはraw reader factory、TOC probe、共有DriveAccessCoordinator、thread-safeで非blockingなmonotonic clockを渡す。
factory/probeはcoordinatorを再入せず、consumerはfreshなjob専用のbounded CPU実装でなければならない。

`start`のSTOPPED/出力停止/queue取消/reader解放/audio-ready gateはcallerの申告であり、
このcomponentが実runtimeを自動観測した根拠ではない。pure coreの適格性検査はI/O前に済ませ、
busy/not-ready/再使用job IDは拒否する。job IDはinstance寿命内で増加させ、同じpathやTOCで結果を再利用しない。
一つのdriveのcapture admissionはDが一つのworker instanceへ集約する必要があり、coordinator自体はglobal job leaseではない。

| 段階 | 所有・判定 |
|---|---|
| 受付/preflight | boundedなCPU検査だけをcaller上で行う。取消/条件変更と競合した未確定admissionはepochで拒否する |
| 初期TOC/open/seek/read/最終TOC/close | owner threadだけがcoordinator内で呼ぶ。状態mutexをcoordinator待ち・I/O中に保持しない |
| raw readとrepeat | workerが明示0 offsetと既存repeat policyを包む。raw seek/read境界にもadmission checkを置き、取消後の次のwrapper試行を止める |
| consumer | owner threadでA coreへ渡す。chunk/finalize前後に取消/期限を確認し、mainからthread-confined coreを同時操作しない |
| 解放/後処理 | 取消後もcleanup closeだけはownerがcoordinator内で行う。consumer cleanupまで終わる前にslotを再利用しない |
| prepared/confirm | 最終TOCとreader破棄を確認してresultをprivate stagingへコピー。callerのconfirmで現identity/gate/期限を再検証して初めて公開可能な内部receiptにする |

callの開始許可はcoordinator取得後の短い状態lock内をlinearization pointとする。
許可済みcallは取消後もin-flightとして戻るまで待つ。coordinator待ち中の取消は取得後の再checkでI/O発行を止める。
raw reader/libraryの一回のcall内部やkernel ioctlは強制中断せず、次の制御可能な境界で止める。
coordinatorは公平性を保証しない。取得全体を長時間mutexで占有せず、各callごとに解放する。

`poll(current, gate)`は条件変更を失効へ反映し、call/job期限を監視するcontrol操作である。
workerがI/Oで止まっていても呼び手はpoll/cancelできるが、実runtime側で定期的に呼ぶ接続はDに残る。
取消・timeoutはbusyをすぐfalseにせず、旧I/Oとcleanupの終了まで新jobを拒否する。
`reader_released`は旧jobのcallback/reader/consumerがquiescentで今後I/Oを発行しないことを示す。
fdがまだ開いていることの直接測定でも、物理device交換/reset/同一性の証明でもない。

正常時もpreparedではslotを保持し、callerのconfirmまたはcancelを必要とする。
cancelとconfirmは一つの状態lockで順序付け、cancelが先ならresultを公開しない。
context変更後のreceiptや遅延readは適用せず、破棄したjobを自動再開・予約しない。
結果は最大64 KiBのconsumer payloadだけで、manifest全体のencodingはDに残る。
snapshotや明示result copyを呼び手が無制限に保持することまでframeworkのmemory上限は保証しない。

正規化PCM bufferはsingleで15 CD frame（35,280 bytes）、repeatで最大75 CD frame（176,400 bytes）。
内部snapshotの`accepted_frames`はconsumer確定prefixのCD frame数、`accepted_bytes`はそのPCM byte数である。
`read_observations`はAが受理した正規化readの集計であり、取消・不完全read等で受理しなかった物理callを含む総I/O回数ではない。
その0を、device I/Oがなかった、または失敗しなかった根拠にしない。
`pcm_capacity_bytes`は現存buffer容量、`peak_pcm_bytes`はそのjobで観測した最大容量で、reader/backend内部のheapやRSSではない。
coreのreservation、consumer宣言量、wrapperの有界候補数とは区別する。全域PCMを蓄積・file保存しない。

cancel/pollはreaderやconsumerを待たないが、destructorはownerをjoinするため進行中callが戻らなければ待つ。
detach、別threadからの強制close、ejectの先行を導入しない。Dはrelease待ち中にmain loopで破棄/joinしないことが必要である。
fake試験の成功を実driveの故障・中断・物理releaseが確認済みという根拠にはしない。

#### Local PCM SHA-256 Profile（2026-10-19）

利用者は、通常再生の必須ではない将来拡張だったC/Dを、明示全域取得とlocal指紋の用途で進めると
明示した。これはAccurateRip/CTDB等の外部provider採用や利用許諾ではない。
最初のC区間は[consumer](../../include/complete_pcm_sha256.hpp)とcore接続試験だけだった。
続くD開発差分ではAPI有効時にdaemonへもcompile/linkし、実raw readerから同じconsumerを利用する。

| 項目 | 採用値・意味 |
| --- | --- |
| canonical | `s16le-44100-stereo-v1`。上記canonical PCMと同一。header、track区切りbyte、追加無音、gain等をhashしない |
| algorithm/version | `sha256` / `local-pcm-sha256-v1`。[FIPS 180-4](https://doi.org/10.6028/NIST.FIPS.180-4)のSHA-256を使用 |
| 範囲 | discはcoreが受理したcanonical byte列の連結、trackは各TOC track内のbyte列。範囲と端/offset適格性の判断はcoreが所有する |
| offset | 初期の明示0制約を維持。物理校正済みとせず、非0の端を暗黙に除外しない |
| 入力上限 | 1 chunkは16 KiB以下、4 bytes整列、track番号1〜99の単調順。coreが4-byte stereo sample frame単位と正しいTOC境界を供給する |
| 新規所有state | OpenSSLの固定`SHA256_CTX`をdisc/現trackに各1つ、最大99のtrack digestと固定16 KiB結果buffer。宣言は`sizeof`で算出し1 MiB以内 |
| 実装上の選択 | opaqueなEVP provider stateのallocationを上限と推測しないため、現行OpenSSLの固定SHA APIを使用。low-level APIのdeprecated属性は当該翻訳単位だけ抑止し、独自hash algorithmは実装しない |
| CPU/I/O | worker上でchunk単位にincremental処理。consumerにthread/network/file/drive I/Oはない。PCMは蓄積せずcontext/digestのみ保持 |
| 結果 | 最大16 KiBのUTF-8 JSON、disc/各trackのbyte数と小文字64桁hex SHA-256。`kind=local_pcm_fingerprint`、`external_verification=NOT_CHECKED`を必須とする |
| 寿命 | 一job専用consumer。finalize後の入力/再finalizeは拒否し、取消/無効化でdiscardする。部分resultをCOMPLETEとして公開しない |

JSONのfieldは`kind`、`algorithm`、`version`、`canonical`、`external_verification`、`bytes`、`sha256`、
`tracks`で、track要素は`number`/`bytes`/`sha256`を持つ。入力を読む間は結果を公開しない。
consumer単体のalgorithm vector試験は任意の整列byte列や空入力も使うが、それをTOC全域取得receiptと扱わない。
COMPLETEは従来どおりcore/workerとDのconfirm・実release・有効条件の成立を必要とする。
digestは入力byte列の識別であり、repeat誤一致の解消、原盤正当性、物理disc同一性、外部MATCH、bit-perfect出力を証明しない。
state宣言はconsumerの具体的な所有memoryであり、thread stack・process RSS・全library全体の上限ではない。
`CompletePcmInput`の結果公開条件、8 MiB予約、1 MiB consumer上限、cancel/context失効は変更しない。

#### Explicit Local Jobs Runtime（#239）

`CompletePcmSession`を単一session ownerとし、`run_player_session()`が実controller/engine/worker/media状態を供給する。
API有効・API起動時に一slotを用意するが、明示start受理までfactory/TOC/consumerの処理を開始しない。
API名・field/versionは[公開契約](message-contract.md#explicit-local-pcm-jobs開発差分)で固定する。

- 正確なSTOPPEDだけを許す。engineは同期開始時にquiescenceを取り消し、queue取消と成功したoutput reset、
  保持PCM/evidence/submissionの消去後だけquiescentを立てる。ALSA handleのcloseを意味せず、PAUSED/NO_DISCは不適格。
- 通常STOP/自然終了ではplayback readerを非同期に退役する。quiescentだけでなく実`device_released()`を別確認する。
  解放待ちでstartが拒否されても取得は予約せず、利用者が再操作する。Playは従来のfactoryを必要時に再openする。
- 開始可否のpolicy/resource判定とcore予約量は同じpure preflightを使う。通常再生で許されるregionでも
  captureの75-frame制約や8 MiB予約に適合しなければ`policy_budget_unsupported`を示し、開始可能と偽表示しない。
- start時にbackend/direct option・観測generation・policyを固定し、raw factory/TOCを共有coordinatorの内側で実行する。
  worker Bがoffset0/repeat wrapperを所有し、通常再生用の既に補正/再読されたfactoryを二重利用しない。
- device観測generationはidentityへ明示的に含め、capture reader incarnation/job番号やdisc_generationと混同しない。
  hostの観測は継続し、退役完了・eject・probe/media/speed/spindownのdriveアクセス条件にはcaptureも含める。
- HTTP/CEC/stdinのplay/seek/track等とpolicy変更は先にcaptureを失効させる。解放中なら要求は未実行とし、
  reader解放後の手動再操作だけを受ける。Stopは既にSTOPPEDでも取消する。無関係な未知stdin文字列では取消しない。
- APIで受けた取消を先に処理してからownerがpoll/confirmする。後続のCEC/stdin操作も次の公開応答より前にreceiptを撤回する。
  GETは読取専用であり、取得進行・期限・confirmをGET頻度に依存させない。
- media idleは実行中/受付待ち/未回収結果が全てないことを要求する。statusの`media_activity`は
  同じ一回の観測による内訳であり、FD0をidleへ変換したり、結果を未適用のまま捨てたりはしない。
  GETのidle表示と後続startの受付は別時点で再評価し、表示から予約/自動再試行は作らない。
- ownerは既存media結果とengineの失効情報を適用した後、届いているAPI要求とcapture pollを新しい定期observeより先に処理する。
  observe投入直前にはcaptureのbusy/実解放等を再取得し、同じ反復で受理したjobの解放前に媒体アクセスを増やさない。
  既存のprobe/TOC/eject/speed/idle-stopや実行中/未回収結果は優先度のために破棄せず、開始gateを緩めない。
- 現slotのmanifestだけをcacheし、現在context/全gate不一致では結果を返さない。完了後も新操作やdevice/policy/disc変更で撤回する。
  manifestは64 KiB以内、HTTP結果は既存の4 KiB分割/同時8 response上限を使う。PCMやmanifest履歴をdisk保存しない。
- Settingsは30分上限を明示した取得/取消buttonと状態・release待ち・結果API位置を表示する。statusのread-only pollingは
  panel内だけで、開閉やpoll失敗からPOSTを自動生成しない。202をCOMPLETE/解放済みとせず、拒否/未確認時は手動再操作とする。
- Settingsの7 buttonは利用不能でもfocus可能とし、操作不可はaria-disabledで示す。条件不足での決定は理由表示だけで
  POSTを送らず、説明文を特別なcursor対象にしない。backendの開始条件やloopback限定はこのUI表現で緩めない。

実装はroot/sessionの状態を観測してgateを供給するが、fake成功を実ALSA停止・kernel I/O解放・物理drive適合の証拠にはしない。
workerのcall/job期限は未完了化・後続I/O停止の期限であり、in-flight kernel I/Oを強制中断する保証ではない。
shutdownは所有threadをjoinし、無期限に生存するdetached driveアクセスは作らない。

## 12. 移行phaseと完了条件

| Phase | 小さな実装単位 | 現状と未完了部分の受入条件 |
|---|---|---|
| 1a Observable core | 能力のUNKNOWNモデル、既存read統計、PCM世代/区間との対応、snapshot/event・診断ログ | 実装・通常CDで実機確認済み。read-only能力probe、bounded event、ALSA再生head推定を含む |
| 1b Observable presentation | NO DISC能力表示、technical statusの小さなrenderer、event受信 | 実装・自動試験済み。通常再生、再読み込み、再接続をbrowserで実機確認済み。NO DISC表示の実機確認は継続 |
| 2 Buffered Reader | 既存queueの容量/閾値設定、device I/O調停、速度設定と失敗fallback | 容量/閾値と直列化を実装し、通常CDで実機比較済み。速度要求はPiでioctl受理と短時間API再生まで確認。傷disc・長時間stall評価は未完了 |
| 3 Checked Reading | overlap・候補比較・bounded recovery・provenance、BALANCED | 反復候補比較と先行overlapを実装し、hardware非依存試験と通常CDの限定Pi再生を確認。cache軽減、異常媒体/長期のPi評価、BALANCEDは未完了 |
| 4 Drive-aware Secure | MMC/C2、cache評価/対策、offset、strategy選択、SECURE | 対応driveと根拠を実測、非対応は明示降格。QUIETもpolicyとして確認 |
| 5 External Verification | checksum/照合、confidence・coverage、遅延結果 | 部分再生/交換/外部障害を誤ってMATCHにしない |

各phaseで設計レビュー→hardware非依存試験→ユーザーによる実機確認を行う。
観測coreは追加drive readや速度変更を必要としない。未実装要求のためだけにreader APIを全面変更しない。
CMakeはモデル/集計テストを基本buildへ、JSON/API試験をENABLE_APIへ分ける。
ENABLE_METADATA=OFF / ENABLE_API=OFFでも観測coreと再生は利用可能にする。
追加runtime optionはそのphaseで機能が成立したものだけ公開し、未実装指定を成功扱いしない。
Phase 2の速度要求は明示opt-inとして実装し、通常CDでioctl受理と短時間API再生まで確認した。既定では要求しない。速度の実測と効果は未確認であり、通常運転#184、定量比較#83、非対応driveでの失敗#146に分けて評価する。

## 13. 試験計画

以下は拡張全体の試験計画である。一部は既存試験と重なるが、すべて検証済みとは扱わない。
現在の確認済み範囲と実測は[検証状況](../development/verification.md)を参照する。

- モデル: UNKNOWN/NO、C2 support/trust、offset null/0、根拠の競合と失効。
- policy: mode変換、能力不足時のstrategy降格、attempt/時間/memory上限、fallback選択。
- 正常fake reader: 読み取り回数とPCMがPhase 1で不変、single readをCHECKEDとしない。
- 異常fake reader: C2報告かつ一致、A/B/B/B、不一致継続、部分read、位置ずれ、read failure。
- cache fake: キャッシュ同一結果を独立3回readとしない。C2不可でもstrategyが成立する。
- PCM境界: overlapの欠落/重複防止、offset正負、disc端、byte order、CRC衝突時のbytes比較。
- playback: 部分write、ALSA delay、starvation、underrun、先読み区間と可聴位置の分離。
- lifecycle: seek/stop/eject中のread完了、disc A→B、旧event・旧external resultの拒否。
- provenance: reason生成、unique coverage、ring eviction、現在再生分の保持、欠落の明示。
- protocol/UI: serialization、sequence gap、再接続、priority、dedup、最低表示時間、sticky警告。
- 分離: API無効、UI切断、slow client、診断log失敗でもaudio queueが待たない。
- 外部照合: track全域/部分、offset条件、match/mismatch/unavailable、代替PCMを含む区間。

試験は既存fake callbackを拡張し、余分なthreadを作らずdeterministicに予算を検証する。
実機では空driveのprobe、通常CDの差分なし再生、操作応答、能力reportと実測の差を順に確認する。
傷disc、cache defeat、速度変更、長時間readはコマンド・期待結果を提示してユーザーが実行する。
実機でC2の信頼性やoffsetを未確認のままTESTEDにしない。ビルド・自動試験は
[Mac + Docker手順](../manual/mac-docker-development.md)を使い、Piではruntime/hardware検証を行う。

### Deterministic Integrity observation fixture（#110）

`tests/integrity_fixture.hpp`の`ScriptedCddaReader`はtest-onlyの`CddaReader`である。各stepは
論理start LBA、要求/取得frame数、`ReadStatus`、native error、direct retry、backend event、
local verificationを明示し、workerが渡すPCM bufferのframe数とseek位置がstepと一致しなければ失敗する。
scriptを使い切ったread、seek前のread、read error後の再seekなしのreadは失敗する。

このfixtureは`PcmWorker`が既存の`ReadEvidence`、stats、coverage、disc map、active warning、
history/event windowへ投影する経路をhardware非依存で検証する。`integrity_fixture_test`はclean、
direct retryによるUNCERTAIN、backend fixupまたは複数candidate mismatch後のRECOVERED、read error、
mixed map、history/eventのevictionを確認する。fixtureはC1/C2、物理再read、drive cache独立性、
read speed、傷の物理形状を生成せず、production daemon、CLI、packageには含めない。Player UIを通す
manual review harnessは#111で別途扱う。

### Local Player Integrity scenario review harness（#111）

`integrity_scenario_harness`はtest/development targetであり、`ScriptedCddaReader`、silent audio output、
固定TOC/drive capabilityを使う。通常の`cdplayerd`、systemd service、package、ALSA、CEC、実driveを変更しない。
`clean`、`retry`、`recovered`、`uncertain`、`mixed`、`read-ahead`、`transition`を選択すると、既存の
`PcmWorker → PlaybackEngine → PresentationModel/serializer → ApiServer → /player`経路でloopback APIを公開する。
`transition`はCLEANからUNCERTAIN、RECOVEREDへ移るfixtureである。C1/C2、read speed、physical reread、
傷の物理形状、bit-perfectは再現・保証しない。`read-ahead`ではsilent outputがPCMを消費せず、queued blockの
latest readをcurrent playback evidenceなしで確認する。正常に読み取りを継続するscenarioではfixture PCMを
通常の`PlaybackEngine`経路で消費する。`uncertain`ではread errorをworker stateに残して表示するため、
通常runtimeが行う停止処理を意図的に進めない。

Docker build後、Macのbrowser/CDPから確認する場合は次のように固定portを公開する。

```sh
docker run --rm -p 127.0.0.1:18080:18080 -v "$PWD:/src" -w /src picdplayer-build \
  ./build-container/integrity_scenario_harness --scenario transition --port 18080
```

起動時に`INTEGRITY_SCENARIO_HARNESS development_only=1 scenario=transition`を標準出力へ記録する。
表示された`http://127.0.0.1:18080/player`を開く。終了は`Ctrl-C`で行う。これは開発用のlocal fixtureであり、
実機異常media試験（#146）や通常Player/Custom UIのhardware検証を置き換えない。

CDPで標準rendererのスクリーンショットを取得する場合は、harnessをport 8080へ公開し、MacのChromeを別profileで
起動する。既存`measure-kiosk-cdp.py`はtarget URLを`http://127.0.0.1:8080/player`として選ぶため、このportを使う。

```sh
# terminal 1
docker run --rm -p 127.0.0.1:8080:8080 -v "$PWD:/src" -w /src picdplayer-build \
  ./build-container/integrity_scenario_harness --scenario mixed --port 8080

# terminal 2（Mac）
profile_dir="$(mktemp -d)"
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
  --remote-debugging-address=127.0.0.1 --remote-debugging-port=9222 \
  --user-data-dir="$profile_dir" http://127.0.0.1:8080/player
python3 scripts/measure-kiosk-cdp.py --seconds 1 --screenshot /tmp/picdplayer-integrity-mixed.png
```

スクリーンショットはChrome rendererのsurfaceを示すだけで、HDMI scanoutやTVの実表示を保証しない。Chromeを閉じた後、
一時profileは必要なら手動で削除する。

Macからこのfixtureだけを実行する場合は、既存Docker buildを作成してから次を実行する。

```sh
./scripts/build-container.sh
docker run --rm -v "$PWD:/src" -w /src picdplayer-build \
  ctest --test-dir build-container -R '^integrity_fixture$' --output-on-failure
```

`-V`を追加するとCTestの詳細を表示する。これはC++ test targetであり、Piへのdeploy、daemon起動、
Player UI表示は行わない。

### repeat試行根拠の追加（#35）

`read.latest/current_playback.verification`に有界な試行詳細と採用候補を追加する。
field・単位・null・互換性の定義は[機能設計](functional-design.md#有界なrepeat試行根拠35の初期実装)を参照。
旧payloadの欠損は未取得として扱い、試行やcandidateを生成しない。wrapperのPCM一致は
物理再読込・cache独立性の証明ではない。stream単位の有界coverageとstream/policy識別子を追加した。詳細は機能設計の
「stream coverageと根拠の世代」を参照。

#35でreader/disc観測世代とstream内128件の詳細履歴を追加した。仕様と限界は機能設計の
「reader/disc世代と詳細履歴」を参照。物理hotplug検出や再起動間の識別を保証しない（#88）。

詳細履歴は常時snapshotから分離し、GET /api/read-historyで取得する。通常stateのread.session_idと
stream_generationを照合して旧結果を捨てる。契約の正本は機能設計「詳細履歴のオンデマンド取得」。
#36のwarning/event復元は実装済み。追加の異常系自動検証#90は完了。実機異常系は#146、Playerへの統合は#24で扱う。

#36でread.active_warningとevent_windowを追加し、technical statusはsnapshotで警告を置換する。
契約の正本は機能設計「診断snapshotの復元」。既存event sequenceは維持し、read_sequenceを追加。
欠落はUNKNOWN、stream/session変更で旧状態を破棄する。完全なreplayは提供しない。

## Player UI統合案の管理（#24）

#24は現行APIと#98/#99の追加契約を利用する実装Issueとして正式化した。標準Playerの実装は
`ui/default/`に実装しPR #108でPhase 1を完了した。通常Pi CDPで確認済みで、異常scenarioは#109〜#111、実機異常系は#146、通常系の継続運転は#184、性能測定は#83で追跡する。
#35/#36と追加検証#89/#90は完了。公開仕様の正本は[メッセージ契約](message-contract.md)。
主表示を維持してcurrent/latest、stream警告、方針、buffer、drive能力を統合する。
全ディスク円盤read mapを必須成果物とし、TOC/世代公開#98とdisc領域集計#99をHard dependencyとする。
#98はPR #101、#99はPR #102で完了済み。#24はこれらの契約を利用して実装できる。
現行の128件履歴だけで全disc履歴を表示しない。
詳細履歴はオンデマンド取得し、最新session/stream照合と有界保持を必須にする。
標準target viewportでは主再生情報を優先する。#24のPhase 1は実使用で評価するため、現在取得できるread/drive値を
折り畳まず常時表示する。primary（曲・artwork・再生状態・進捗）、secondary（current/latest、方針、buffer、
coverage、map）、diagnostic（LBA、観測窓、能力値）の階層を文字サイズ・contrast・spacingで表す。これは最終的な
常時表示項目を確定するものではなく、評価後に削減や状態依存表示を検討する。`CLEAN`はcurrent evidenceのscope付き
分類として表示し、全disc/原盤/bit-perfect保証に見せない。mapはobserved clean、retry/repeat、recovered、
UNCERTAIN/backend anomaly、unobservedの意味と限界をlegendで示す。
接続表示はIntegrity header内でdaemon state API/WebSocket接続だけを表す。常時brandingを置かず、backgroundは
neutral darkとする。具体的なレイアウト・色・描画方式は実装時に決めるが、未観測値を生成しない条件を維持する。

メッセージのfield・型・単位・意味・世代・順序・欠落・互換性を変更する際は、
同じ変更作業で本仕様、関連する機能/API/Custom UI文書、および#24のCurrent state・Data sources・
Acceptance criteriaを更新する。実装済み契約と提案中の項目を混在させない。
#7〜#13全体の完成を一律の前提にはせず、親#12を重複したblockerにしない。
容量・根拠保持の追加検証#89はRelatedとして扱う。

## Disc領域集計（#99）

workerが一次read結果を最大256区間へ集約する。停止/seekをまたいで保持し、TOC再受理でresetする。
同じ観測flagsの隣接/重複区間だけ結合し、過去異常を後の成功で上書きしない。
容量超過時は不完全な下限として凍結する。未観測を未読/正常と塗らない。
詳細はGET /api/read-historyのdisc_mapとして取得し、通常WSには載せない。
field・flags・世代照合はメッセージ契約を正本とする。#24の描画はPR #108で実装済みである。

### Overlap監査補足（#115）

候補未確定・read失敗ではoverlapはNOT_CHECKEDであり、不一致を観測したことにはしない。比較frame数は実比較時だけ設定する。拒否候補のaccepted識別子はnull。正式な状態・型はメッセージ契約を参照する。
