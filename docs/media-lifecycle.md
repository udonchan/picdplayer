# メディアライフサイクル

## 目的

`cdplayerd --player`を空のドライブで起動でき、Audio CDの挿入、準備完了、
取り出しを`PlayerController`の唯一の状態へ反映する。Linux ioctlの瞬間的な戻り値、
アプリケーションのメディア状態、再生状態は役割を分ける。

## 構成

`read_cd_media()`は`CDROM_DRIVE_STATUS`と、disc ready時の
`CDROM_DISC_STATUS`を一度だけ読み、`MediaObservation`を返す。
既存のTOC経路は変更せず、Audio CDがreadyになった時だけ`read_cd_toc()`を使う。

これらのioctlは`O_NONBLOCK`でdeviceを開いても待つ可能性がある。そのため
`MediaWorker`がmain loop外の1 threadでstatusとTOCを直列実行する。要求は一つだけ
保留でき、結果をmain loopで`PlayerController`へ反映する。CEC受信はioctlの完了を
待たない。

PLAYING中は新しいmedia polling要求を止める。ただし再生開始前に発行済みの照会と、
停止後も完了していないCD-DA読み取りの重複までは防いでいない。
完全なdevice access直列化は未実装であり、競合の可能性が残る。
APIから要求するejectだけは、後述の通りCDDA reader解放後に直列実行する。
再生中にdiscを取り出した場合は、
CD-DA読み取りエラーで再生が停止した後、500 ms周期のpollingが再開して
`NO_DISC`を確定する。

## 状態と観測

`MediaObservation`は一回のhardware確認結果である。

|観測|意味|
|---|---|
|tray_open / no_disc|メディアがないことをドライブが明示|
|not_ready|回転開始、トレイ動作中、一時的な復旧など。取り出しとは断定しない|
|audio_disc|再生対象のAudio CD|
|unsupported_disc|データCD、mixed modeなど現在再生しないメディア|
|unknown|情報不足。以前の状態を変更しない|

`MediaStateTracker`は観測を次の状態へ変換する。

|状態|PlayerControllerへの反映|
|---|---|
|NO_DISC|discを取り除き、再生状態も`NO_DISC`にする|
|LOADING|準備中。直前のdisc状態を保持する|
|AUDIO_READY|TOCを非同期に取得し、成功後`STOPPED`にする|
|UNSUPPORTED|discを再生対象から外し、`NO_DISC`にする|

`unknown`は状態を維持する。同じ観測を繰り返しても状態は変わらない。
LOADING後に同じTOCが戻った場合は、短いdrive recoveryとみなしtrackと位置を保持する。
異なるTOC、または取り出し後の挿入ではTrack 1の`STOPPED`として読み込む。

## エラーと終了

同じmedia probe errorの連続ログは抑制する。成功すれば抑制状態を解除する。
workerはdetachせず、終了時に進行中のioctlが完了するまでjoinする。このためUSB driveが
kernel内で応答しない場合は終了が遅れる可能性がある。強制的なthread cancellationは
deviceやC++ objectの寿命を壊すため行わない。

## 実機試験

低メモリのRaspberry Pi 3ではビルドを`cmake --build build-direct -j1`で行う。
次を起動し、各操作後のログとCEC操作を確認する。

```bash
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct
```

1. trayを空にして起動し、`player: state=NO_DISC`のまま操作を受け付ける。
2. Audio CDを入れ、`AUDIO_READY`、TOC情報、Track 1の`STOPPED`を確認する。
   poll時点によっては、その前に`LOADING`も表示される。
3. TVリモコンで再生、pause、next、previous、seek、stopを確認する。
4. stoppedまたはpausedでdiscを取り出し、`NO_DISC`へ戻ることを確認する。
5. 再生中にも取り出し、読み取りエラー後に停止して`NO_DISC`へ戻ることを確認する。
6. 再びAudio CDを入れ、daemon再起動なしで再生できることを確認する。
7. データdiscがあれば`UNSUPPORTED`となり再生を開始しないことを確認する。

## 2026-09-15のPi停止調査

実装中の`-j2`ビルド時にPiが応答不能となり再起動した。前bootのjournalは永続化されて
いなかったため、OOM、kernel panic、電源断の確定記録はない。再起動後の確認では
905 MiB RAMのうち742 MiB使用、swap 434 MiB使用で、ビルド完了後にはswap使用量が
625 MiBまで増えていた。強いメモリ圧迫とswap I/Oが主な候補である。

現在bootのkernel logにはAudio CDに対するSCSI `READ(10)`の`Illegal mode for this
track`が多数あった。これは本変更のplayerを起動する前に発生した通常block readで、
CD-DA用`CDROMREADAUDIO`の記録ではない。直接の停止原因を示す証拠としては扱わない。
`vcgencmd get_throttled`は実行ユーザーに`/dev/vcio`権限がなく確認できなかった。

再発時の確定診断にはpersistent journalを有効にするか、再起動前にkernel logを別媒体へ
保存する必要がある。当面はPi上のビルドを`-j1`に制限し、再生中のmedia pollingも
止めてdrive commandの競合を避ける。

## 実機確認結果

2026-09-15、direct backendで次を確認した。

- 空のドライブで`NO_DISC`として起動した
- CD挿入後に`LOADING`、`AUDIO_READY`へ遷移した
- 14曲、leadout LBA 242334のTOCを自動取得し、Track 1の`STOPPED`になった
- CECからplay、10秒送り、next、stopを操作できた
- stopでTrack 1、LBA 0へ戻った
- stoppedで取り出すとmediaとplayerの両方が`NO_DISC`になった
- 同じCDの再挿入で`LOADING`、`AUDIO_READY`を経てTOCを再取得した
- daemonを再起動せずTrack 1からCEC再生とseekができた

再生中の取り出しではUSB drive reset後に`ALSA delay: Broken pipe`で再生が停止したが、
再挿入後のPlayで音声が復帰しない事象を確認した。`PcmWorker`が通常のpause/seek用に
保持していた古いreader handleをUSB reset後にも再利用したことが原因候補である。
再生エラーまたは確定したdisc取り出しではreaderをworker thread上で破棄し、次のPlayで
deviceを開き直すよう修正した。通常のpause/seekではreaderを保持する。

### 再挿入後の再生失敗: 再試験

reader再生成の修正後も`ALSA delay: Broken pipe`が再発した。
今回は`NO_DISC → LOADING → AUDIO_READY`とTOC再取得を確認できたが、
次のPlayでも同じエラーで停止した。古いreaderの再利用を根本原因とは断定できない。
USB resetとユーザー操作の正確な時刻対応も未確認である。

前回追加した破棄要求は停止中のworkerを起こさず、次のPlayまでcloseを遅延していた。
待機条件に破棄要求を含め、進行中の読み取りが終わった後、Playなしでもcloseするよう修正。
停止中の破棄と、ALSA delay失敗後の再生再開をhardware非依存テストへ追加した。

失敗時の`player: failure_context`で次を記録する。成功する通常tickではログを増やさない。

- `tick_gap_us`: 前回の再生tickから今回までの時間
- `queued_blocks`: workerに残るPCM block数（通常1 blockは15 CD frames、200 ms分）
- `pending_samples`: engine内の未送信int16 sample数（stereo両channelの合計）
- `submitted_stereo_frames`: 今回の再生範囲でALSAが受理したstereo sample frame数
- `last_read_us`: 最後に完了したCD-DA readの所要時間
- `read_inflight_us`: 現在進行中のreadの経過時間（なければ0）

queueが空でread時間が長い場合はCD供給不足、PCMが残りtick間隔が長い場合は
main loopの遅延を疑う。ただし単一snapshotだけで原因を確定しない。
この診断追加時点では再生エラーで停止しALSAをresetする方針だった。
現在のunderrunに対する復旧方針は末尾の「ALSA underrunの自動復旧」を参照。
実機ではまず取り出し操作をせず通常再生し、失敗すれば前後のログを保存する。
通常再生成功後に取り出し・再挿入を試す。非対応discの実機試験も未完了。

診断追加後、取り出し操作なしでTrack 1のLBA 6556（約87秒）まで通常再生し、
pauseできた。この間にALSA errorは発生しなかった。常時のALSA設定不良よりも、
取り出しやUSB resetに伴うPCM供給停止を疑う結果である。

同じsessionでpauseから再開後にdiscを取り出すと、失敗時は
`tick_gap_us=10084 queued_blocks=0 pending_samples=0 last_read_us=41011
read_inflight_us=2202523`だった。main loopは約10 ms周期を維持していたが、
CD-DA readが2.2秒以上完了せず、worker queueとengine内PCMが空になっていた。
そのためALSA bufferがunderrunして`ALSA delay: Broken pipe`になったと判断できる。
これはCECやmain loopの遅延ではなく、Eject中のdrive I/O停止に伴う結果である。
その後の`NO_DISC → LOADING → AUDIO_READY`と14曲TOCの再取得は成功した。

再挿入後のPlayでは52920 stereo frames（約1.2秒）をALSAへ渡した後、CD-DA readが
1.16秒以上停止し、再びqueueが空になった。従来は1秒分のPCMで再生開始し、queue上限も
2秒分だった。再挿入直後の一時的なdrive stallを吸収するため、再生開始条件を2秒分、
queue上限を4秒分へ変更した。PCM使用量は最大約706 KiBである。通常Playの開始は
従来より最大約1秒遅くなる。この値で不足する場合は`failure_context`の実測を基に再検討する。
この固定バッファ変更後、通常再生と取り出し・再挿入後の再生が実機で成功した。

## ALSA underrunの自動復旧

ALSAの`write`、`delay`、`drain`が`EPIPE`を返した場合だけ`AudioUnderrun`として扱う。
その他のALSA errorやCD-DA errorは従来どおり再生を停止する。

underrun時はALSAをresetし、CD readerをworker thread上で開き直す。ALSAへ送信済みの
stereo frame数から最後のwhole CD frameを求め、そのLBAから再読込する。
PlayerStateは`PLAYING`を維持し、PCMを再び先読みしてから出力を再開する。
再開位置では最大1 CD frame（約13 ms）の重複が起こり得る。

初期先読みは10 block（2秒）。underrunごとに5 block（1秒）増やし、queue上限の
20 block（4秒）で止める。容量を超えてメモリを増やさない。復旧時には次を記録する。

```text
player: underrun recovery=N resume_lba=LBA prebuffer_blocks=COUNT reason=...
```

Ejectでは、進行中のCD readが返るまで一時的に`PLAYING`のままbufferingする可能性がある。
その後CD-DA errorまたはmedia observationによって停止し、`NO_DISC`へ遷移する。
実機では通常の一時stallから音声が再開することと、Eject時に無限復旧しないことを確認する。

2026-09-16、underrun自動復旧実装後の通常再生と、取り出し・再挿入後の再生は正常だった。
この試験ではunderrunが再発しなかったため、実機上の自動復旧経路そのものは未確認である。
`AudioUnderrun`を注入するhardware非依存テストでは、`PLAYING`を維持したreader再生成、
再buffer、再開を確認している。

今後、次の条件で実機評価する。

- 傷や汚れで読み取りが不安定なAudio CD
- USB optical driveのresetまたは一時的な応答停止
- 4秒を超えるread stall
- 複数回のunderrun後に増加したprebufferでの再生継続
- 復旧位置での音の重複、欠落、操作応答時間

評価結果によって、復旧回数の上限、buffering状態の明示、無音または停止への遷移を決める。
## API eject

APIからのejectは再生を停止した後、PcmWorkerがCDDA readerを閉じるまで非同期に待つ。device解放後に
MediaWorkerが`CDROMEJECT`を実行する。成功時は即座に`NO_DISC`へ遷移し、失敗時はdiscを保持して
`EJECT_ERROR`と理由を保持する。CDROMREADAUDIOとCDROMEJECTを別threadから同時実行しないことを
優先する。LOADING中も要求を`EJECTING`として保持し、完了が遅いioctlの後に一度だけ実行する。

ASUS SDRW-08D2S-Uでは、先にdoor lockを解除しない`CDROMEJECT`に対してkernelがSCSI sense
`ILLEGAL REQUEST asc=0x53 ascq=0x2`（medium removal prevented）を記録した。eject処理は
`CDROM_LOCKDOOR(0)`の後に`CDROMEJECT`を実行し、eject失敗時は再lockしてplayer状態を保持する。

一度のAPI要求ではトレイが開かないという報告から、媒体のunloadだけが起きると推測していたが、
この原因は未確認だった。後述のAPI待受によるmain loop停止が判明したため、ドライブ固有の挙動とは
断定しない。現在はioctl成功を完了条件にせず、100 ms周期で最大2秒
`CDROM_DRIVE_STATUS == CDS_TRAY_OPEN`を確認する。開かなければ`CDROMEJECT`をもう一度だけ実行し、
再度最大2秒確認する。2回でもtray openを確認できなければ`EJECT_ERROR`とし、無制限retryは行わない。

診断ログは要求からdevice worker開始までを`eject=started wait_ms=...`、物理tray確認までを
`eject=completed elapsed_ms=...`として記録する。EJECTING中の再送は新しいhardware操作を作らず、
`eject=already_pending`を記録して同じ要求の202を返す。


2026-09-17のログでは`already_pending`の後に`started wait_ms=11588`、
`completed elapsed_ms=20100`を記録した。2回目の要求は新たなejectを発行していない。
調査でlibwebsocketsの`lws_service(context, 0)`が通信待ちでmain loopを止め得ることが判明した。
worker待ち時間だけでなく、main threadが結果を回収するまでの遅延も上記時間に含まれる。
service前に`lws_cancel_service()`でwake-upを予約する修正と、無接続時の回帰テストを追加した。
修正後の一度のAPI要求による実機ejectは未確認。
