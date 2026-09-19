# 詳細設計

公開関数の引数・型はリンク先のヘッダーを正とする。ここでは制御順序と所有権を記す。
private helperを全件転記する代わりに、機能境界と不具合に関係する処理を対象とする。

ReadResult、PcmBlock、DaemonSnapshotにはread evidence、集計、DriveCapabilities、bounded event、
ALSA再生headの根拠推定を持たせる。反復一致検証とtechnical statusも実装済みである。
専用diagnostic event stream、C2/cache/offsetなどの未実装案は
[拡張設計](../development/integrity-design.md)を参照する。

## 起動とmain loop

[main.cpp](../../src/main.cpp)はCLIのmode競合、数値範囲、build optionを検証し、
診断関数または`run_player_session()`へ分岐する。引数エラーは2、実行時例外は1。
[player_session.cpp](../../src/player_session.cpp)はmain thread上にcontroller、engine、media tracker、
metadata session、任意のAPIを構築する。

`Signals`はworker生成前にSIGINT/SIGTERMをblockしてsignalfdを作る。
workerはmaskを継承し、mainのpollで停止要求を処理する。

一回のloopは概ね次の順序で実行する。

1. 停止境界でpending ReadPolicyを適用し、read eventを回収する。
   pending ejectがあればreader解放を調べ、MediaWorkerへ投入を試す。
2. 500 ms期限で、非PLAYINGかつeject待ちでなければmedia観測を要求。
3. media結果を回収し、TOC・controller・metadata世代を更新。
4. metadata結果を回収し、世代とTOCが一致する場合だけ適用。
5. 250 ms期限でCEC登録状態を確認し、`PlaybackEngine::tick()`を呼ぶ。
6. API snapshotを250 ms期限で更新し、API serviceを呼ぶ。
7. signalfd・CEC fd・任意のstdinを最大10 ms pollし、操作を適用。

CEC受信はqueueを空になるまで回収する。操作後に位置/状態が変わればengineをsynchronizeする。
shutdownでは音声停止を先に行い、その後workerのdestructorがjoinする。

## media・TOC関数

| 関数・型 | 契約 |
|---|---|
| [find_optical_drives](../../include/optical_drive.hpp) | sysfsを列挙する値snapshot。テスト時はroot pathを差し替え |
| [read_cd_media](../../src/cd_device.cpp) | 一回open→drive status→ready時disc status→close。観測とraw値を返す |
| read_cd_toc | AUDIOのみ受理。TOCHDR・TOCENTRYをLBAで読み、全件検証後にDiscTocを返す |
| [make_audio_toc](../../src/disc_toc.cpp) | 1〜99の連続番号、非負・単調増加の位置、有効leadoutを検証。不正はinvalid_argument |
| [MediaStateTracker::observe](../../src/media_state.cpp) | 観測をmedia状態へ変換。unknownは維持。playerは直接操作しない |
| [MediaWorker::request/pop](../../src/media_worker.cpp) | pending/busy中の追加requestはfalse。blocking callbackをworkerで実行し、例外をerror付き結果へ変換 |

`run_player_session()`がTOCの採用とcontroller更新を担当する。LOADINGではTOC再確認を要求するが、
既存discの即時削除はしない。NO_DISC/UNSUPPORTEDではdiscを外しreader破棄を要求する。
同じTOC判定は番号・開始LBA・長さ・leadoutの比較で、物理disc固有番号ではない。

### ejectの処理順序

API handlerでstop→engine.synchronize→worker.discard_reader→begin_ejectを行い、要求を保持する。
metadataをinvalidateし、旧observe/TOC結果はEJECTING中に適用しない。
`PcmWorker::device_released()`とMediaWorkerのrequest受理を確認してからejectを開始する。

eject_cdは一時fdをopenし、CDROM_LOCKDOOR(0)後にCDROMEJECTを呼ぶ。
100 ms間隔で20回TRAY_OPENを確認し、未確認ならもう一度だけejectする。
最大2回の試行で、ioctl自体の待ち時間は別途かかる。
CDROMEJECTの失敗時は再lockを試みて例外を返す。状態確認失敗・確認timeoutでは再lock処理は行わない。
成功結果の適用時にloaded_tocを破棄しcontroller.remove_discする。
失敗結果ではeject_failedに理由を渡し、TOCを保持する。

## PlayerControllerと再生engine

[PlayerController](../../src/player_controller.cpp)はmain thread専用、hardware非依存。
load_discは公開mutableなDiscTocを再検証する。play/pause/stop/next/previous/select_track/seek_relativeは
意図と位置だけを更新する。seek_relativeは加算前に差分をclampしoverflowを避ける。
playback_positionはPLAYING中の前進位置だけを受理し、finishedは停止して先頭へ戻す。

[PlaybackEngine::synchronize](../../src/playback_engine.cpp)は旧PCMをcancel、ALSA reset、
保持blockと送信量を消去し、PLAYINGなら現在LBAから新しいworker世代を開始する。
`tick()`は先読み閾値を満たすまで待ち、一回最大12回のnonblocking writeでmain loopを占有しない。
部分writeの残りはblock内offsetで保持する。

位置は `start_lba + (submitted_stereo_frames - ALSA_delay) / 588` から求める。
reader終端後もALSA drainが完了するまでfinishedにしない。
AudioUnderrunでは送信済み量から再開LBAを求め、readerを破棄して再bufferする。
CD frameへの切り捨てにより最大1 frame弱の重複があり得る。
それ以外の例外はstop・cancel・reader破棄・ALSA reset後、sessionのログ処理へ戻す。

[PcmWorker](../../src/pcm_worker.cpp)はreaderをworker threadだけで生成・使用・破棄する。
start/cancelでgenerationを更新し、古いread完了データを採用しない。
queueは設定されたblock上限でcondition_variableにより待つ（singleの既定50 block）。
discard_readerは待機中workerも起こすが、進行中ioctlは中断しない。
device_releasedはreaderが閉じ、readが進行中でないことをmutex下で確認する。
accepted blockにはReadResultから作ったReadEvidenceを付ける。WorkerStatusのReadDiagnosticsは最新readと
stream開始後の集計を値コピーで返す。世代が変わったread完了はPCMと同じく集計にも加えない。

`make_read_evidence`は成功かつ要求全frame取得をcompleteとする。statusの優先順位は
不完全取得→UNCERTAIN、fixup報告（skipなし）または反復不一致後の一致→RECOVERED、
その他retry/skip/read error/cache error→UNCERTAIN、残り→CLEANである。
local_verificationはmatching_readsが2以上ならMULTIPLE_MATCH、次にparanoia verify/fixupが
あればBACKEND_REPORTED、残りはSINGLE_READ。これは観測の分類で、全policy条件の充足や
原盤一致の保証ではない。現行backendはC2 NOT_CHECKED、offset UNKNOWNを返す。
frames_acceptedは要求全量を成功取得した場合だけ加算する。verified_callsは2回以上一致の
read数であり、policyの必要一致数を満たした採用read数と同一とは限らない。

READ_OBSERVED eventは256件上限の別queueへ渡し、main側でsequenceを付ける。通常成功はDEBUG、
backend回復報告はINFO、未確実な結果はWARNING。start/cancel/discardで旧世代のeventを破棄する。
PcmBufferConfigは容量と開始閾値をCD frameで保持し、有効read block数へ変換する。既定750/45 frame、
上限2250 frameで、0、15の倍数でない値、開始閾値が容量を超える値を起動前に拒否する。
underrun時の開始閾値増加も設定された容量を上限とする。

[DriveAccessCoordinator](../../include/drive_access.hpp)は一台のdriveに対するblocking callをmutexで直列化する。
PcmWorkerのfactory/seek/read/closeとMediaWorker callbackが共有する。mutex待ちはworker thread内で行い、
main loopを止めない。実行中ioctlのcancelや公平性は提供せず、eject優先は既存の停止・reader解放順序で保証する。

[CddaReader](../../include/cdda_reader.hpp)は生成時open、seek、read、destructorによるcloseのRAII interface。
read bufferはCD frameの整数倍で、ReadResult.frames_read部分だけが有効。
最初とread_error後にseekが必要。EOF判定は呼び手がTOCで行う。
[direct](../../src/cdda_reader.cpp)と[paranoia](../../src/paranoia_reader.cpp)は同じPCM形式を返す。
[RepeatedReadVerifier](../../src/repeated_read_verifier.cpp)はreaderを包み、各試行前に同じLBAへseekする。
候補をPCM全sampleで比較し、policyの必要一致数で採用する（既定2一致、最大3試行）。
試行数・完全read数・最大一致数・不一致数・時間予算超過をReadResultへ記録する。
試行または時間予算（既定10秒）で未解決ならframes_read=0のread_errorを返し、
呼び手のbufferへ候補PCMをコピーしない。時間予算は進行中のblocking readを中断しない。
PcmWorkerはsingleで15 frame、repeatでpolicyのregion_frames（既定75）を要求する。
queue容量はCD frame設定をregionで割って切り捨て、開始閾値は切り上げる。
切り上げ結果がqueue容量を超える場合は容量へ丸め、待機条件が達成不能にならないようにする。
既定設定ではsingleは10秒/0.6秒、repeatは10秒/1秒（75 frame blockへの切り上げ）となる。
各世代の処理開始時にblock量と容量をmutex下でコピーし、
旧世代のreadが戻るまでに設定が変わっても可変設定をlock外から参照しない。
[AudioOutput](../../include/audio_output.hpp)のwrite/delayの単位はCD frameでなくstereo sample frame。
[ALSA実装](../../src/alsa_output.cpp)はnonblocking PCMを44.1 kHz、signed 16-bit、stereoで開き、
EPIPEをAudioUnderrunへ分類する。latency要求は既定200 ms、起動optionで100〜2000 msを比較できる。
これはPcmWorkerの先読み容量と独立しており、main loopが長く停止するとCD queueにPCMが残っていても
ALSA underrunになり得る。

## CEC・API・状態公開

[CecDevice](../../src/cec_device.cpp)のupdateはopen・adapter確認・非同期claim確認を担当する。
receiveは一件dequeueし、応答が必要なmessageを処理して任意のCecCommandを返す。
[cec_command_from_ui_code](../../src/cec_input.cpp)はhardware非依存でキーを意味的操作へ変換する。

[make_daemon_snapshot](../../src/daemon_snapshot.cpp)はplayerとTOCの整合を検証し、
track内位置と長さを追加した値コピーを作る。network/hardware I/Oはしない。
[serialize_daemon_snapshot](../../src/daemon_snapshot_json.cpp)は1回のJSON生成結果を配信と変更検出に使う。
変更検出ではtransport用revisionの数字だけを無視し、semanticな状態が同じなら配信revisionを増やさない。
[serialize_daemon_snapshot](../../src/daemon_snapshot_json.cpp)はrevision/player/media/disc/metadataをJSON化する。
optionalはnull。metadata.selectedはcandidate配列の0始まりindex。
Phase 1aではschema_version=1とdrive capabilities、read activity、strategy、latest/current playback evidence、
aggregate stats、直近64件のeventも含む。latestはreader側の最新先読み区間。current_playbackは
ALSAへ提出したPCM範囲とdelayから求める再生head推定で、TV/ARC/アンプ内部の遅延は含まない。
read objectはbuffer_capacity_framesとstartup_buffer_framesも公開する。
`last_prebuffer_wait_ms`はplay、seek、track変更、underrun復旧の後に、engineが最初に先読み条件を
満たすまでのmonotonic時間である。`prebuffer_target_frames`はその時点の必要PCM量であり、ALSA以降の
出力遅延は含まない。single/repeatやbuffer設定を比較する診断値で、再生開始の外部的な保証時刻ではない。

ReadPolicyはdaemon sessionがrequested/effective/pendingとして所有する。workerへ渡されたeffective policyは
stream中に変更しない。APIで再生中に新policyを受けた場合はrequested/pendingだけを更新し、stop後に
PcmWorkerへreconfigure要求を出す。workerは既存readerをowner threadで閉じ、次回startで新policyに対応する
block sizeとreaderを使う。この順序によりread途中の比較条件やevidenceを混在させない。

[probe_drive_capabilities](../../src/drive_capabilities.cpp)はsysfsのvendor/model/revと読み取り専用の
CDROM_GET_CAPABILITYを調べる。現段階でYES/NOを付けるのはkernelが報告するspeed controlだけで、
DAE、C2、cache、accurate stream、offsetはUNKNOWNを維持する。MediaWorkerで非同期に実行するため、
失敗や遅延はdisc認識と再生可能化を妨げない。

[route_api_request](../../src/api_server.cpp)はmethod/path/bodyを検証してhandlerへ渡す純粋な入口。
実接続のloopback判定はApiServer callback側で行い、route単体は認証境界ではない。
serviceはlws_cancel_serviceでwake-upを予約してからlws_service(context,0)を呼ぶ。
timeout=0だけでは待受を避けられずmain loopを止めることがあったため、この順序を保つ。
publish_stateは送信用snapshotを更新する。mainが完成済みJSONを用意し、callback内でTOCやnetworkを読まない。
[technical_status_page](../../src/technical_status_page.cpp)はHTML/CSS/JavaScriptをcompile時に埋め込む。
追加filesystemやNode runtimeを要求しない。画面はGET stateとWS eventsだけを消費し、再接続時には
snapshotから全表示を再構築する。外部文字列はtextContentへ設定し、innerHTMLへ渡さない。
[now_playing_page](../../src/now_playing_page.cpp)も同じ配信経路だけを使用する。metadata.selectedを
candidate配列のindexとして解決し、現在track番号でTrackMetadataを探す。metadataが不在ならDiscToc由来の
track番号と時間だけを表示する。cover_art.image_urlはbrowserのimgへ渡し、load errorでは表示を戻す。
このpageはdaemonへの操作・metadata候補選択・画像cacheを実装しない。

## daemon logging

[AsyncLogger](../../src/logger.cpp)はwall clock（UTC）、process起動後のmonotonic経過時間、level、
component、messageを一行で出力する。INFO/DEBUGはstdout、WARN/ERRORはstderrへ送るため、systemdでは
両方をjournaldが収集できる。診断CLIのPCMやJSON等の結果はログではなく、従来どおり直接出力する。

daemonのmain loopは文字列を有界queueへ追加するだけで、terminalやjournaldへのwriteを待たない。
専用threadがqueueを順に出力する。queue上限は512件であり、満杯時のDEBUG/INFOは捨てる。
WARN/ERRORでは可能なら古いDEBUG/INFOを一件押し出す。失った総数は終了時に
`WARN logger: dropped=N`として通知する。logger自身の失敗は再生処理へ例外を伝播させない。
このthreadはhardwareやauthoritative stateへ触れず、出力遅延をmain/audio pathから隔離するためだけに使う。

現在のAsyncLoggerは、依存を増やさず、Pi 3とBuildrootで扱いやすい小さな非同期出力を実現するための独自実装である。
将来、出力先、runtime level変更、構造化field、rotation、journald以外のsinkなどを必要とする場合は、
spdlog等の一般的なC++ logging libraryとの比較を行う。比較では、binary size、Buildroot package化、
非同期queueの満杯時挙動、終了時flush、sinkのthread safety、ライセンス、再生threadを待たせない設定を確認する。
呼び出し側は`log_info("component") << ...`の境界を維持し、libraryを採用する場合もlogger実装を交換可能にする。

## metadata関数と非同期境界

| 関数・型 | 契約 |
|---|---|
| [calculate_musicbrainz_disc_id](../../src/musicbrainz_disc_id.cpp) | TOC再検証、LBA+150、discid_put/get_id/get_toc_string。device accessなし |
| [parse_musicbrainz_response](../../src/metadata_parser.cpp) | 該当Disc IDのmediumから候補生成、track positionを1始まり連続で検証、0/1/複数を分類 |
| parse_cover_art_response | front画像のHTTPS URLを選択。画像bytesは取得しない |
| [HttpClient::get](../../src/http_client.cpp) | HTTPS・timeout・受信サイズ・redirect policyを適用しstatus/type/bodyを返す |
| [lookup_musicbrainz_id](../../src/metadata_lookup.cpp) | cacheまたはHTTP→parse→単一候補CAA。CAA例外はartwork.errorへ格納 |
| lookup_musicbrainz_disc | Disc ID計算と上記lookup後、各候補の曲数を実TOCと照合。不一致はmetadata ERROR |
| [MetadataWorker::request/pop](../../src/metadata_worker.cpp) | pending最新1件、結果1件。worker内でlookup例外をERROR結果へ変換 |
| [MetadataSession::begin/invalidate/apply](../../src/metadata_session.cpp) | 世代更新、TOC保存、世代と全TOC一致時だけ結果適用 |

MetadataWorker.cancel_pendingは未開始requestと保存結果を消す。実行中HTTPの中断はshutdownの
closingフラグだけに連動する。交換時の安全性は中断ではなくMetadataSession.applyで保証する。
同一TOCへLOADINGから戻っただけの場合、現在のsession実装はmetadataを再要求しないことがある。
TOCが1以外の番号で始まる場合のmetadata track positionとの対応付けも未実装である。

raw cacheはsize確認→read→通常parser、書き込みはtemporary file→rename。
MusicBrainzの404は空releasesとして扱う。parse前にraw JSONをcacheするため、不正cacheが残る可能性がある。
HTTP本文上限はあるが、cacheの総量制限や全JSON fieldへの厳密な型検証は保証しない。

## drive start診断

[request_cd_start](../../src/cd_device.cpp)はdeviceを一時的に開き、Linux `CDROMSTART` ioctlを一回だけ
発行して閉じる。PCMやTOCを読まず、成功はkernel/driveが命令を受理したことだけを表す。
[probe_cd_start](../../src/cd_device.cpp)はその所要時間を出力する一回実行の診断である。
回転中かどうかを返す標準状態値としては扱わない。

daemonはAudio CDのTOC読取完了直後を最初の期限とし、STOPPEDまたはPAUSED中だけMediaWorkerへ
start_driveを要求する。要求受付時に次の期限を15秒後へ進める。PLAYING、NO DISC、LOADING、eject待ち・
実行中には要求しない。再生終了時に期限を過ぎていれば直ちに要求する。MediaWorker callbackは
DriveAccessCoordinator内で実行するため、PCM reader、status/TOC、ejectと同時にdevice ioctlを行わない。
結果ログの`rotation=UNVERIFIED`は、命令受理から物理的な回転状態を推測しないという契約である。
初回命令もbackground処理であり、再生可能化の必須条件にはしない。

## テスト境界

controller、media tracker、TOC、CEC変換、snapshot、parser/sessionはhardwareなしで試験する。
PCM/audio/media/metadata workerはfake callbackやfake出力を用い、世代・破棄・失敗経路を検証する。
APIはroute試験とloopback socket試験を持つ。詳細な確認範囲は[検証状況](../development/verification.md)を参照。

| 主な試験ソース | 対象 |
|---|---|
| [player_controller_test.cpp](../../tests/player_controller_test.cpp) | 操作・位置・disc範囲 |
| [playback_engine_test.cpp](../../tests/playback_engine_test.cpp) | fake reader/output、PCM世代、失敗・underrun |
| [media_worker_test.cpp](../../tests/media_worker_test.cpp) / [media_state_test.cpp](../../tests/media_state_test.cpp) | 非同期要求とmedia状態 |
| [musicbrainz_disc_id_test.cpp](../../tests/musicbrainz_disc_id_test.cpp) | 既知TOC/Disc ID、入力制約 |
| [metadata_parser_test.cpp](../../tests/metadata_parser_test.cpp) / [metadata_session_test.cpp](../../tests/metadata_session_test.cpp) | 候補変換・古い結果の拒否 |
| [api_server_test.cpp](../../tests/api_server_test.cpp) | route・socket・待受によるloop停止の回帰試験 |
| [daemon_snapshot_json_test.cpp](../../tests/daemon_snapshot_json_test.cpp) | 公開JSONの単位・null・状態 |
| [logger_test.cpp](../../tests/logger_test.cpp) | level別sink、時刻・経過時間・component、終了時flush |

自動試験は本物のCD・TV・アンプでの試聴やトレイ動作を代替しない。
