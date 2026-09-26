# 機能設計

この文書は現行機能を記す。QUIET/BALANCED/SECURE、C2、cache対策、offset、外部照合の未実装要求は
[読み取り信頼性の仕様](integrity-design.md)を参照する。現行direct/paranoiaの選択や正常再生は、
原盤PCMとの一致やdrive cacheから独立した読み取りを保証しない。
Phase 1aではread回数を変えず、既存ReadResultの観測、read-only drive能力probe、bounded event、
ALSA再生headに対応する根拠の推定をsnapshotへ公開するところまで実装した。
公開するCLEANは検証済みの意味ではなく、local verificationをSINGLE_READ等で別に示す。
Phase 1bではこのsnapshotだけを入力にする読み取り専用technical status画面を追加した。
これは診断用であり、TV向け本番UIやauthoritative stateを兼ねない。
`/player`は同じsnapshotを表示用に整形する初期のNow Playing画面である。画面は状態を所有せず、
操作POSTも送らない。任意導入の`picdplayer-kiosk.service`はCageからWayland版Chromiumを起動し、
この画面だけをtty1へ表示する。daemonとbrowserは別serviceであり、browserが状態を所有しない。
Now Playing document内のcursorはCSSで隠すが、Cageのerror pageや他applicationまで含む
cursor非表示の保証、画面遷移、画面からの操作、quiet bootは未実装である。
反復一致読み取りは既定75 frame区間で2-of-3比較を行う。設定変更の契約は以下に記す。

## UIのカスタマイズ

UIのHTML/CSS/JSは`ui/default/`を単一のソースとし、ビルド時にfallback用として埋め込む。
`--custom-ui PATH`はAPI有効時だけ指定可能で、起動時に上限付きでload/validationする。
不正なCustom UIはWARNを記録してdefaultへ戻り、`/player`上でも無効化を通知する。
`/builtin/player`とtechnical statusはCustom UIから独立して配信する。通常の再生状態や
metadata modelはUI選択によって変更しない。manifest version・ファイル上限・URL契約は
[Custom UI](../manual/custom-ui.md)を参照する。runtime JS検査、hot reload、設定APIは未実装。

## media・TOC

sysfsのSCSI type 5から光学ドライブを列挙する。USB/SATAを識別条件にはしない。
ドライブの存在、mediaの存在、Audio CD判定は別の確認である。
Linux CD-ROM ioctlで状態・種別・TOCを読み、音声のみのCDをDiscTocへ変換する。
mixed-mode、負LBA、隠しtrack、CD-TEXT、複数sessionの解釈は対象外。

media観測は停止中に500 ms周期で要求する。PLAYING中は新規観測を止め、
物理取り出しはread失敗で再生停止した後に観測する。LOADINGは取り出しと断定しない。
同じTOCの再確認では再生位置を保持し、取り出し後または異なるTOCでは先頭へ戻す。
player session内では観測開始済みioctlもPCM操作と共有mutexで直列化する。

## 再生操作

| 操作 | 動作 |
|---|---|
| play | discがあればPLAYING。PAUSEDからは現在位置で再開 |
| pause | PLAYINGからPAUSED。位置を保持 |
| stop | STOPPEDにしてdiscの先頭trackへ戻る |
| next | 次track先頭。最終trackでは変化なし |
| previous | 曲開始3秒未満なら前track、3秒以上なら現在track先頭。最初のtrackはその先頭 |
| track N | TOC内の番号を選ぶ。再生/停止/一時停止状態を保持 |
| seek | 相対移動。曲境界を跨ぎ、disc先頭〜leadout直前へclamp |
| disc終端 | ALSA drain完了後、STOPPED・先頭trackへ戻る |

通常CDの先頭番号は1だが、内部モデルでは1固定ではない。
CEC seekは1回±10秒。対話CLI/APIは相対秒数を指定する。

## reader・音声出力

CddaReaderはseek後に連続readする小さなinterface。Player側はbackendを知らない。
`direct`はCDROMREADAUDIO、`paranoia`はlibcdio-paranoiaを使用する。
TOC取得はどちらでも既存Linux ioctl経路を維持する。
paranoiaはoptional build、runtimeの未知名・無効なbackend指定はエラーにする。
採用・性能比較は保留で、directを当面の運用で使用している。

現在の出力はALSA PCMを通じたHDMIを標準構成とする。`AudioOutput`境界と`--audio-device`は
存在するが、HDMI以外の出力profileやdevice検出は実装していない。USB S/PDIF、I²S S/PDIF、
外部I²Sの将来候補と、bit-perfectを安易に主張しない原則は
[デジタルオーディオ出力の将来拡張](../development/digital-audio-output.md)を参照する。
ALSAへ要求するlatencyは既定200 msで、比較用に`--audio-latency-ms`（100〜2000）を指定できる。
これはCD-DA先読みqueueとは別で、main loopの一時停止に対する出力側の余裕である。

direct追加retryは診断CLIで0〜10回指定でき、既定0。playerは既定設定を使用する。
paranoiaはFULLからNEVERSKIPを除いたmode、最大retry 20、skipは失敗として扱う。
callback統計はdirectのretry回数と同じ意味ではない。

`--read-verification repeat`はbackendをdecoratorで包み、75 CD frame（終端のみ短縮）の同一区間を最大3回読み、PCM全体が2回一致した
候補だけを採用する。一致しない場合は再生を停止する。既定の`single`は従来どおり一回のreadを採用する。
反復一致はdrive cacheの影響を排除しないため、独立した複数readや原盤PCMとの一致とは表示しない。

API有効buildではread policyをruntimeで要求できる。daemonはrequested/effective/pendingを公開し、再生中の
変更はSTOPPEDまたはNO_DISCで適用し、次回playでreaderを作り直す。PAUSED中も保留する。
readerの設定をread途中で変えないため、現在再生中のPCMとその根拠は維持される。
設定の永続保存、backendの稼働中切替、QUIET/BALANCED/SECUREへの対応は未実装。

single modeのPCMは15 CD frame（200 ms）単位、repeat modeはseek overheadを抑えるため75 frame単位。
既定はqueue上限750 frame（10秒、PCM約1.68 MiB）、
開始閾値45 frame（0.6秒）。容量と開始閾値は15 frame刻みで最大2250 frame（30秒）まで
起動optionで設定できる。read policyはAPIで起動中に変更できるが、先読みqueueの容量・開始閾値は
起動時設定のままである。開始閾値は容量以下とする。
block数への変換では容量を切り捨て、開始閾値を切り上げる。切り上げた閾値が実際に
保持できるblock数を超える場合は容量へ丸めるため、任意のregion設定では実際の容量・
閾値が要求値と異なり得る。終端付近は閾値より短くても開始可能。
ALSA EPIPEはAudioUnderrunとしてreset・reader再生成・再bufferする。
先読み閾値は復旧ごとに5 block増加し、設定されたblock容量で止まる。
増加はengine内で保持し、ReadPolicyを適用すると初期閾値へ戻す。
復旧回数上限はなく、長時間停止や傷discでの音質・応答は未評価。

同一driveに対するreader open/seek/read/closeとmedia/TOC/eject/capability probeは共有mutexで直列化する。
進行中ioctlは強制中断せず、ejectは従来どおりstream停止、reader解放確認後に要求する。
速度設定はまだ行わない。既定bufferは通常CDでstartup、seek、track変更、pause復帰と連続再生を
実機比較して決めたが、傷discや長いread stallに対する余裕は未評価である。CD queueが満杯でも
main loopがALSA latency近く停止すればunderrunし得るため、両bufferを同じものとして扱わない。

## CEC

Linux CEC UAPIを直接利用し、Playback DeviceとしてOSD名PiCDPlayerで登録する。
Physical Address未確定時は250 msごとに確認。Logical Addressはkernelが選び、8固定にしない。
同名登録は再利用し、別名登録は上書きしない。終了時も登録解除しない。

Power StatusはON。自身宛のSET_STREAM_PATHでACTIVE_SOURCEを通知し、
他機器のACTIVE_SOURCEで非activeになる。REQUEST_ACTIVE_SOURCEにはactive中だけ応答する。
起動時に自動でTV入力を切り替えない。
Play/Pause/Stop/Skip Forward/Skip Backward/Fast Forward/Rewindを再生操作へ変換する。
登録後にREGZAとMarantzのARCが復帰する実機結果はあるが、全TVでの保証ではない。

## API

libwebsocketsを採用しmain threadからserviceする。HTTPとWebSocketを一つのoptional依存で扱う。
既定はlistenなし。`--api-port`で127.0.0.1へlisten、`--api-listen`で数値IPを指定できる。
外部listenでも操作POSTと起動telemetryは実際の接続元がloopbackの場合だけ許可する。外部POSTは403。
本文あり／なしの両経路で同じ接続元判定を使う。
認証・TLSは未実装で、外部公開は信頼できる開発LANでの診断用途に限る。

### Presentation Modelの診断投影

`GET /api/state`とWebSocketは`drive`、`read`、`recent_events`も公開する。
診断のJSON変換は既存snapshot serializerと共有し、provider ID/URLを追加公開しない。
technical statusは`player.track_number/position_frames`、`disc.state/title/artist`、
`tracks`、`enrichment.status`と上記診断値を表示する。

| Method/path | 入力・結果 |
|---|---|
| GET /api/state | provider非依存のPresentation Model JSON |
| GET /api/read-policy | requested/effective/pendingを即時取得 |
| POST /api/read-policy | 下記5 fieldのJSON、受理204。適用完了はpolicy状態で確認 |
| WS /api/events | 接続時と公開状態変化時に同じJSON。clientからの操作messageは不可 |
| GET /debug/status | drive/read/disc/eventを表示する読み取り専用diagnostic HTML |
| GET /debug/status.css, /debug/status.js | diagnostic画面の埋め込みasset |
| GET /player, /player/ | 選択中のUI。標準はalbum、track、位置、cover artを表示する読み取り専用Now Playing HTML |
| GET /player.css, /player.js | 選択中のNow Playing asset（Custom UI未採用時はbuilt-in） |
| GET /builtin/player, /builtin/player.css, /builtin/player.js | 常にbuilt-inの復旧用画面・asset |
| POST /api/ui-boot | 任意の標準UI起動telemetry、受理204。再生commandとは独立 |
| POST /api/play, /pause, /stop, /next, /previous | bodyなし、受理204 |
| POST /api/seek | `{"offset_seconds":10}`、±86400秒、受理204 |
| POST /api/track | `{"track":2}`、1〜99かつ実disc内、受理204 |
| POST /api/eject | bodyなし、受理202。物理完了は状態で確認 |

seek/trackはfieldを1個だけ持つJSON object。操作body上限4 KiB、state上限1 MiB。
未知pathは404、不適切なmethodは405。不正入力は400、body上限超過は413。
通常操作はdiscなし/EJECTING時に409。操作の受理は音声出力開始の完了を意味しない。
状態は250 msごとに変化を検査し、revisionを増加して配信する。HTTP直後のstateも最大でこの更新待ちがある。

read-policyのbodyはmode、region_frames、required_matches、maximum_attempts、time_budget_msの
5 fieldのみ。modeは入力single/repeat、公開値SINGLE/REPEAT。数値は非負整数で、
region_framesは15以上・15の倍数・設定buffer容量以下、required_matchesは2以上、
maximum_attemptsはrequired_matches以上8以下、time_budget_msは1〜60000。
singleでも保持する検証設定を検査する。共通入力不正は400、現在のbuffer容量に収まらなければ409。
single時の実read量はregion_framesによらず15 frame。repeat時にregion_framesを用いる。
technical statusはeffective strategyとReadPolicyを別々に表示し、未適用の要求は
`effective → requested (pending)`として示す。初回にGET stateを読み、以後WebSocketで更新する。接続断ではstateを再取得してから
再接続するため、eventを一件ずつ完全に受信したことを状態復元の前提にしない。metadata文字列は
DOMのtextContentとして扱い、HTMLとして解釈しない。画面から操作POSTは送信しない。

`/player`も初回GETとWebSocketで同じPresentation Modelを消費する。title/artistがなければ
`Audio CD` とtrack番号を表示するため、metadata無効・lookup失敗・候補曖昧でも再生画面は使える。
coverはdaemonが取得・形式確認したsame-origin resourceだけを返す。画像の失敗時はプレースホルダーへ
戻る。外部文字列はtechnical statusと同様にtextContentで表示する。HTTP responseはCSPで
script/style/imageをselfへ制限する。`connect-src`はselfとws:/wss:を許可し、画面の実装は
同一hostのAPIへ接続する。

起動telemetryの入力・イベント定義・流量上限は
[起動時間計測](../manual/systemd.md#起動時間の計測cage--chromium--標準ui)を正とする。
`first_render`は描画機会の近似、`ui_ready`はsnapshot反映・描画機会・WebSocket接続の成立であり、
HDMI first pixelやCEC/CD再生準備を保証しない。送信失敗は表示や再生を止めず、Custom UIに送信義務はない。

## 未実装のintegrity機能要求

[横断仕様](integrity-design.md)に従い、能力と根拠、requested modeとeffective strategy、
local比較とcache独立性、外部照合とoffset、採用PCMの由来を別々に公開する。
検証の試行・時間・memory上限と未解決時の動作を明示し、無期限retryを避ける。
UNKNOWNや照合できなかった範囲を成功として表示しない。新しいmode・field・endpointは
実装と互換性試験が完了した段階で上記の現行API一覧へ追加する。

## eject

要求をdaemonで保持するため、UIの再送pollingは不要。LOADINGやNO_DISCでも202で受理する。
EJECTING中の重複要求も202で、別のhardware操作を作らない。
再生を停止してreader解放を待ち、MediaWorkerからunlock→eject→tray確認を実行する。
tray openを確認できたら`disc.state=NO_DISC`、失敗なら`EJECT_ERROR`を公開する。
現行Presentation Modelはmedia error文字列を公開しないため、詳細はdaemonのjournalで確認する。

EJECTING中はAPI通常操作を拒否し、CEC・CLI再生操作も適用しない。state/quit・signalは有効。
失敗後はTOCを保持し、再ejectを受理できる。EJECT_ERRORはaudio_disc観測では維持するが、
no_disc/tray_open、not_ready、unsupported観測ではそれぞれ対応状態へ更新する。
ドライブの故障や無期限I/O待ちに対して「必ず物理的に開く」とは保証できない。

## metadata・artwork・cache

libdiscidのput APIへDiscTocを渡しDisc IDを計算する。device読み取りはlibdiscidへ移さない。
HTTPはlibcurl、JSONはnlohmann/json。HTTPSやJSON parserを独自実装しない。
exact Disc ID lookupのみで、TOC fuzzy検索やCD stubは使用しない。

該当Disc IDを含むreleaseのmediumを候補とし、0件はNOT_FOUND、1件はAVAILABLE、複数はAMBIGUOUS。
複数候補を自動選択しない。候補選択API/UIは未実装。
内部modelにalbum/track名・artist、release/release-group/recording ID、medium位置、country/dateを保持する。
曲長の正規値はDiscToc。metadataのms長は参考値である。

単一候補の場合だけCAA JSONを取得し、frontの500px→large→元画像URLを選ぶ。
artwork AVAILABLEはdaemonがJPEG/PNG/WebPのbytesを上限付きで取得し、same-origin local resourceとして
配信できることを意味する。画像取得/検証失敗はmetadata候補を破棄しない。現在はCAA処理完了後に
metadata結果全体をmainへ返す。

raw JSONを`metadata/{disc-id}.json`、`cover-art/{release-id}.json`へ、検証済み画像bytesを
`cover-art/{release-id}.image`へ保存する。
cache pathは明示指定。systemdでは/var/cache/picdplayerを利用できる。
書き込み失敗は無視して取得結果を利用する。期限・総容量制限・破損cacheからの自動再取得は未実装。
read-only rootへの移植時はcacheを別の書き込み可能領域へ置く。

HTTP接続timeout 5秒、全体15秒。MusicBrainz開始間隔1.1秒、429/503は最大3回。
User-Agentはコード内のPiCDPlayer/0.1.0とproject URL。Retry-After解釈は未実装。
MusicBrainz redirectは拒否、CAAはHTTPSに限り最大3回。host/IPの追加制限は未実装。
JSON本文上限はMusicBrainz 2 MiB、CAA 512 KiB。深さ・全field長の個別上限は未実装。
parserは必須構造を検査するが、任意文字列の欠落や型違いは空文字扱いになる。

Buildrootへの移植は未実施。過去の依存・license・package調査は
[metadata調査記録](../history/metadata-design.md)を参照し、移植時に対象revisionで再確認する。

### 有界なrepeat試行根拠（#35の初期実装）

`read.latest.verification`および`read.current_playback.verification`に
`attempt_details`、`detail_capacity`（8）、`accepted_candidate`、`accepted_attempt`を追加する。
各detailは1始まりの`attempt`、`frames_read`（CD frame）、`complete`、`native_error`、
`direct_retries`、`candidate`を持つ。candidateは同一read呼出内のPCM bytes一致による1始まりのIDで、
別区間・世代では比較しない。`accepted_attempt`は一致閾値へ到達した試行番号であり、
PCMを最初に取得した試行番号ではない。採用理由は既存の`local_verification=MULTIPLE_MATCH`に対応する。
失敗試行にcandidateを割り当てず、採用なしはnullとする。single/backend内部の試行詳細は観測しておらず、
配列は空・採用IDはnullとなる。空配列を試行ゼロの保証と解釈しない。
これはwrapperの観測であり、物理再読込・cache独立性・原盤一致を保証しない。

追加fieldは任意として扱い、旧payloadにない場合は未取得とする。REST/WSのsnapshot形は維持する。
最大8件の固定配列で保持し、PCM blockと共に現在再生区間へ届く。詳細attemptの永続履歴、
unique coverage、device/disc/stream世代とpolicy revisionの統合は#35の残作業である。
#24はDraft / Blockedのまま、この契約に合わせて表示候補を更新する。

### stream coverageと根拠の世代（#35、実装途中）

`read.stream_generation`と`read.policy_revision`を追加し、`latest/current_playback`の各evidenceにも
同名fieldを保持する。streamはworkerのstart/cancel/discardで更新し、policy revisionはworkerの
初期設定を1としてreconfigureごとに増加する。これはdaemon内だけの識別子であり、再起動間の比較は
できない。device/disc世代・daemon session IDはまだ未実装である。

`read.coverage`は`scope=STREAM`、`accepted_unique_frames`（CD frame単位）、
`observations_complete`、`region_capacity=128`を持つ。完全に成功して採用されたread区間の和集合を
固定128区間で集計し、overlap・retryを重複加算しない。失敗/部分readは加算しない。
これは先読みを含む採用PCMの観測範囲であり、実再生済み・全disc検証済み・原盤一致ではない。
`observations_complete=true`は採用観測を欠落なく集計できた意味であり、disc全域を読んだ意味ではない。
区間容量超過または不正な範囲を検出するとfalseにし、次のstreamまで下限値を固定する。
履歴を捨てて二重加算する方式は使わない。集計領域は固定配列で追加heap割当を必要としない。
start/cancel/discard時はcoverage・stream統計をリセットし、旧世代の遅延readは加算しない。

既存UIは追加fieldを無視できる。field欠損時は未取得とする。discを跨ぐcoverage、詳細履歴の
保持・eviction・detail_available、およびdevice/disc世代の統合は引き続き#35の残作業。
