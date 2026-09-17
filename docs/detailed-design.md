# 詳細設計

公開関数の引数・型はリンク先のヘッダーを正とする。ここでは制御順序と所有権を記す。
private helperを全件転記する代わりに、機能境界と不具合に関係する処理を対象とする。

今後のread evidence保持、PCM区間との対応、structured eventとUI protocolの型・境界案は
[読み取り信頼性の拡張設計案](integrity-design.md)を参照する。ここで説明する現行ReadResult、
PcmBlock、DaemonSnapshotにはその拡張をまだ実装していない。

## 起動とmain loop

[main.cpp](../src/main.cpp)はCLIのmode競合、数値範囲、build optionを検証し、
診断関数または`run_player_session()`へ分岐する。引数エラーは2、実行時例外は1。
[player_session.cpp](../src/player_session.cpp)はmain thread上にcontroller、engine、media tracker、
metadata session、任意のAPIを構築する。

`Signals`はworker生成前にSIGINT/SIGTERMをblockしてsignalfdを作る。
workerはmaskを継承し、mainのpollで停止要求を処理する。

一回のloopは概ね次の順序で実行する。

1. pending ejectがあればreader解放を調べ、MediaWorkerへ投入を試す。
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
| [find_optical_drives](../include/optical_drive.hpp) | sysfsを列挙する値snapshot。テスト時はroot pathを差し替え |
| [read_cd_media](../src/cd_device.cpp) | 一回open→drive status→ready時disc status→close。観測とraw値を返す |
| read_cd_toc | AUDIOのみ受理。TOCHDR・TOCENTRYをLBAで読み、全件検証後にDiscTocを返す |
| [make_audio_toc](../src/disc_toc.cpp) | 1〜99の連続番号、非負・単調増加の位置、有効leadoutを検証。不正はinvalid_argument |
| [MediaStateTracker::observe](../src/media_state.cpp) | 観測をmedia状態へ変換。unknownは維持。playerは直接操作しない |
| [MediaWorker::request/pop](../src/media_worker.cpp) | pending/busy中の追加requestはfalse。blocking callbackをworkerで実行し、例外をerror付き結果へ変換 |

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

[PlayerController](../src/player_controller.cpp)はmain thread専用、hardware非依存。
load_discは公開mutableなDiscTocを再検証する。play/pause/stop/next/previous/select_track/seek_relativeは
意図と位置だけを更新する。seek_relativeは加算前に差分をclampしoverflowを避ける。
playback_positionはPLAYING中の前進位置だけを受理し、finishedは停止して先頭へ戻す。

[PlaybackEngine::synchronize](../src/playback_engine.cpp)は旧PCMをcancel、ALSA reset、
保持blockと送信量を消去し、PLAYINGなら現在LBAから新しいworker世代を開始する。
`tick()`は先読み閾値を満たすまで待ち、一回最大12回のnonblocking writeでmain loopを占有しない。
部分writeの残りはblock内offsetで保持する。

位置は `start_lba + (submitted_stereo_frames - ALSA_delay) / 588` から求める。
reader終端後もALSA drainが完了するまでfinishedにしない。
AudioUnderrunでは送信済み量から再開LBAを求め、readerを破棄して再bufferする。
CD frameへの切り捨てにより最大1 frame弱の重複があり得る。
それ以外の例外はstop・cancel・reader破棄・ALSA reset後、sessionのログ処理へ戻す。

[PcmWorker](../src/pcm_worker.cpp)はreaderをworker threadだけで生成・使用・破棄する。
start/cancelでgenerationを更新し、古いread完了データを採用しない。
queueは20 block上限でcondition_variableにより待つ。
discard_readerは待機中workerも起こすが、進行中ioctlは中断しない。
device_releasedはreaderが閉じ、readが進行中でないことをmutex下で確認する。

[CddaReader](../include/cdda_reader.hpp)は生成時open、seek、read、destructorによるcloseのRAII interface。
read bufferはCD frameの整数倍で、ReadResult.frames_read部分だけが有効。
最初とread_error後にseekが必要。EOF判定は呼び手がTOCで行う。
[direct](../src/cdda_reader.cpp)と[paranoia](../src/paranoia_reader.cpp)は同じPCM形式を返す。
[AudioOutput](../include/audio_output.hpp)のwrite/delayの単位はCD frameでなくstereo sample frame。
[ALSA実装](../src/alsa_output.cpp)がEPIPEをAudioUnderrunへ分類する。

## CEC・API・状態公開

[CecDevice](../src/cec_device.cpp)のupdateはopen・adapter確認・非同期claim確認を担当する。
receiveは一件dequeueし、応答が必要なmessageを処理して任意のCecCommandを返す。
[cec_command_from_ui_code](../src/cec_input.cpp)はhardware非依存でキーを意味的操作へ変換する。

[make_daemon_snapshot](../src/daemon_snapshot.cpp)はplayerとTOCの整合を検証し、
track内位置と長さを追加した値コピーを作る。network/hardware I/Oはしない。
[serialize_daemon_snapshot](../src/daemon_snapshot_json.cpp)はrevision/player/media/disc/metadataをJSON化する。
optionalはnull。metadata.selectedはcandidate配列の0始まりindex。

[route_api_request](../src/api_server.cpp)はmethod/path/bodyを検証してhandlerへ渡す純粋な入口。
実接続のloopback判定はApiServer callback側で行い、route単体は認証境界ではない。
serviceはlws_cancel_serviceでwake-upを予約してからlws_service(context,0)を呼ぶ。
timeout=0だけでは待受を避けられずmain loopを止めることがあったため、この順序を保つ。
publish_stateは送信用snapshotを更新する。mainが完成済みJSONを用意し、callback内でTOCやnetworkを読まない。

## metadata関数と非同期境界

| 関数・型 | 契約 |
|---|---|
| [calculate_musicbrainz_disc_id](../src/musicbrainz_disc_id.cpp) | TOC再検証、LBA+150、discid_put/get_id/get_toc_string。device accessなし |
| [parse_musicbrainz_response](../src/metadata_parser.cpp) | 該当Disc IDのmediumから候補生成、track positionを1始まり連続で検証、0/1/複数を分類 |
| parse_cover_art_response | front画像のHTTPS URLを選択。画像bytesは取得しない |
| [HttpClient::get](../src/http_client.cpp) | HTTPS・timeout・受信サイズ・redirect policyを適用しstatus/type/bodyを返す |
| [lookup_musicbrainz_id](../src/metadata_lookup.cpp) | cacheまたはHTTP→parse→単一候補CAA。CAA例外はartwork.errorへ格納 |
| lookup_musicbrainz_disc | Disc ID計算と上記lookup後、各候補の曲数を実TOCと照合。不一致はmetadata ERROR |
| [MetadataWorker::request/pop](../src/metadata_worker.cpp) | pending最新1件、結果1件。worker内でlookup例外をERROR結果へ変換 |
| [MetadataSession::begin/invalidate/apply](../src/metadata_session.cpp) | 世代更新、TOC保存、世代と全TOC一致時だけ結果適用 |

MetadataWorker.cancel_pendingは未開始requestと保存結果を消す。実行中HTTPの中断はshutdownの
closingフラグだけに連動する。交換時の安全性は中断ではなくMetadataSession.applyで保証する。
同一TOCへLOADINGから戻っただけの場合、現在のsession実装はmetadataを再要求しないことがある。
TOCが1以外の番号で始まる場合のmetadata track positionとの対応付けも未実装である。

raw cacheはsize確認→read→通常parser、書き込みはtemporary file→rename。
MusicBrainzの404は空releasesとして扱う。parse前にraw JSONをcacheするため、不正cacheが残る可能性がある。
HTTP本文上限はあるが、cacheの総量制限や全JSON fieldへの厳密な型検証は保証しない。

## テスト境界

controller、media tracker、TOC、CEC変換、snapshot、parser/sessionはhardwareなしで試験する。
PCM/audio/media/metadata workerはfake callbackやfake出力を用い、世代・破棄・失敗経路を検証する。
APIはroute試験とloopback socket試験を持つ。詳細な確認範囲は[検証状況](verification.md)を参照。

| 主な試験ソース | 対象 |
|---|---|
| [player_controller_test.cpp](../tests/player_controller_test.cpp) | 操作・位置・disc範囲 |
| [playback_engine_test.cpp](../tests/playback_engine_test.cpp) | fake reader/output、PCM世代、失敗・underrun |
| [media_worker_test.cpp](../tests/media_worker_test.cpp) / [media_state_test.cpp](../tests/media_state_test.cpp) | 非同期要求とmedia状態 |
| [musicbrainz_disc_id_test.cpp](../tests/musicbrainz_disc_id_test.cpp) | 既知TOC/Disc ID、入力制約 |
| [metadata_parser_test.cpp](../tests/metadata_parser_test.cpp) / [metadata_session_test.cpp](../tests/metadata_session_test.cpp) | 候補変換・古い結果の拒否 |
| [api_server_test.cpp](../tests/api_server_test.cpp) | route・socket・待受によるloop停止の回帰試験 |
| [daemon_snapshot_json_test.cpp](../tests/daemon_snapshot_json_test.cpp) | 公開JSONの単位・null・状態 |

自動試験は本物のCD・TV・アンプでの試聴やトレイ動作を代替しない。
