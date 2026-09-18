# 機能設計

この文書は現行機能を記す。QUIET/BALANCED/SECURE、C2、cache対策、offset、外部照合の追加案は
[読み取り信頼性の拡張設計案](../development/integrity-design.md)を参照する。現行direct/paranoiaの選択や正常再生は、
原盤PCMとの一致やdrive cacheから独立した読み取りを保証しない。
Phase 1aではread回数を変えず、既存ReadResultの観測、read-only drive能力probe、bounded event、
ALSA再生headに対応する根拠の推定をsnapshotへ公開するところまで実装した。
公開するCLEANは検証済みの意味ではなく、local verificationをSINGLE_READ等で別に示す。
Phase 1bではこのsnapshotだけを入力にする読み取り専用technical status画面を追加した。
これは診断用であり、将来のTV向け本番UIやauthoritative stateを兼ねない。
反復一致読み取りは既定75 frame区間で2-of-3比較を行う。設定変更の契約は以下に記す。

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
起動optionで設定できる。起動中に変更するAPIはない。開始閾値は容量以下とする。
block数への変換では容量を切り捨て、開始閾値を切り上げるため、任意のregion設定では
実際の容量・閾値が要求値と異なり得る。終端付近は閾値より短くても開始可能。
ALSA EPIPEはAudioUnderrunとしてreset・reader再生成・再bufferする。
先読み閾値は復旧ごとに5 block増加し、設定されたblock容量で止まる。
増加はengine内で保持し、ReadPolicyを適用すると初期閾値へ戻す。
復旧回数上限はなく、長時間停止や傷discでの音質・応答は未評価。

同一driveに対するreader open/seek/read/closeとmedia/TOC/eject/capability probeは共有mutexで直列化する。
進行中ioctlは強制中断せず、ejectは従来どおりstream停止、reader解放確認後に要求する。
速度設定はまだ行わない。既定bufferは通常CDでstartup、seek、track変更、pause復帰と連続再生を
実機比較して決めたが、傷discや長いread stallに対する余裕は未評価である。

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
外部listenでも操作POSTは実際の接続元がloopbackの場合だけ許可する。外部POSTは403。
認証・TLSは未実装で、外部公開は信頼できる開発LANでの診断用途に限る。

| Method/path | 入力・結果 |
|---|---|
| GET /api/state | DaemonSnapshotのJSON |
| GET /api/read-policy | requested/effective/pendingを即時取得 |
| POST /api/read-policy | 下記5 fieldのJSON、受理204。適用完了はpolicy状態で確認 |
| WS /api/events | 接続時と公開状態変化時に同じJSON。clientからの操作messageは不可 |
| GET /debug/status | drive/read/disc/eventを表示する読み取り専用diagnostic HTML |
| GET /debug/status.css, /debug/status.js | diagnostic画面の埋め込みasset |
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

## eject

要求をdaemonで保持するため、UIの再送pollingは不要。LOADINGやNO_DISCでも202で受理する。
EJECTING中の重複要求も202で、別のhardware操作を作らない。
再生を停止してreader解放を待ち、MediaWorkerからunlock→eject→tray確認を実行する。
tray openを確認できたらNO_DISC、失敗ならEJECT_ERRORとmedia.errorを公開する。

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
artwork AVAILABLEはHTTPS画像URLの存在を意味し、画像binaryの取得・検証完了ではない。
artwork失敗はmetadata候補を破棄しない。現在はCAA処理完了後にmetadata結果全体をmainへ返す。

raw JSONを`metadata/{disc-id}.json`、`cover-art/{release-id}.json`へ保存する。
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
