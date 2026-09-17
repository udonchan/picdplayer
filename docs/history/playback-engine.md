> 過去の設計・調査・検証記録です。提案や当時の制約を含み、現行仕様ではありません。
> 現行仕様は[ドキュメント索引](../README.md)から参照してください。

# Native連続再生: 端末操作段階

## 構成

- main thread: PlayerController、ALSA output、コマンド受付、CEC登録の状態確認。
- CD worker 1本: CddaReaderを生成・seek・read・破棄。PlayerStateにはアクセスしない。
- media worker 1本: blockingし得るdrive statusとTOC取得を直列実行する。
- queue: 15 CDフレーム（200ms）×最大20ブロック、約705600 bytes。
  別途workerの読み取り中ブロックとmainの送信中ブロック、ALSA bufferがある。
- 再生開始前に10ブロック（2秒）を蓄積。ディスク終端が近ければ短くても開始。
- ALSA underrun後は5ブロック（1秒）ずつ先読みを増やし、20ブロックで上限とする。

queueの上限はbackpressureとして働き、満杯ならworkerがcondition variableで待機する。
Stop/Seekで世代番号を更新し、queue・main送信中PCM・ALSA bufferを破棄。
古いread結果・errorは新しい世代へ反映しない。CD syscall自体の強制中断はしない。

ALSAはS16 native-endian / stereo / 44100Hz、resampling無効、要求buffer時間200ms。
plughwがHDMIの伝送形式へ変換する。writeはnonblockingで部分書き込みとEAGAINを処理。
direct backendの通常再生と固定bufferの安定性は実機確認済み。ALSA underrunでは
送信済み位置からreaderを開き直し、再bufferして`PLAYING`を維持する。
自動復旧経路はhardware非依存テスト済みだが、実機では異常を再現できていない。
詳細と今後の傷ディスク試験は[メディアライフサイクル](media-lifecycle.md)を参照。

再生位置はsubmitted PCM sample frames - ALSA delayからCDフレームへ切り下げる。
TV/ARC/アンプ内部の遅延は含められない。read cursorは再生位置ではない。
自然終了はnonblocking drain後にSTOPPED、最初のトラックを選択。
Pauseは位置を保持して音声をdrop、Playでseekして読み直す。再開にはprebufferが必要。

## Build

ALSA開発パッケージ（Debian: libasound2-dev）とThreadsが必要。
paranoiaは引き続きoptional。

```sh
cmake -S . -B build-direct -DENABLE_PARANOIA=OFF
cmake --build build-direct -j1
ctest --test-dir build-direct --output-on-failure
```

## 実機試験（ユーザーが実行。自動再生しない）

音楽CDを入れ、TV/アンプの音量を控えめにする。

```sh
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct --interactive
```

起動直後は`NO_DISC`で、ALSAをopen/configureしてmedia確認を開始する。
Audio CDが既にあれば非同期にTOCを取得し、Track 1の`STOPPED`になる。
デフォルト出力はplughw:CARD=vc4hdmi,DEV=0。
変更には--audio-device、CEC登録確認を省くには--no-cecを指定する。

`--player`は常駐動作が既定で標準入力を監視しない。手動試験では`--interactive`を
指定し、端末でコマンドを1行ずつ入力してEnter:

|コマンド|動作|
|---|---|
|play|現在位置から再生（既にPLAYINGなら何もしない）|
|pause|位置を保持して停止|
|stop|Track 1の先頭へ戻りSTOPPED|
|next|次の曲へ。再生/一時停止/停止状態を維持|
|previous|曲頭から3秒未満なら前の曲、3秒以上なら現在曲の先頭。状態を維持|
|track 3|トラック番号3を選択|
|seek 10 / seek -10|現在位置から10秒前後へ移動|
|state|再生要求状態、トラック番号、LBAを表示|
|quit / Ctrl-C|出力を停止して終了|

最初の試験は起動→play→数秒試聴→stop→state→quit。
正常なら次にPause/Play、Seek、曲変更を一つずつ確認する。
`--interactive`では入力EOFでも終了する。stderrにエラーが出た場合はログを共有する。

## CEC入力: キーコード観測

TVが送る`USER_CONTROL_PRESSED`を受信し、PlayerControllerへコマンドとして渡す。
Play、Pause、Stop、Skip Forward、Skip Backwardをそれぞれ`play`、`pause`、
`stop`、`next`、`previous`へ対応付ける。Fast ForwardとRewindは押下ごとに
10秒（750 CDフレーム）の前後シークとして扱う。
それ以外のUIコードは16進数で`ignored`として記録する。releaseイベントは無視する。
再生操作以外のCECメッセージは、後述の電源状態・Active Source関連処理へ渡す。

TVリモコンで誤操作による再生を避けるため、この観測段階はCDを入れなくてもよい。
CEC登録済みで次を実行し、リモコンの再生系キーを一度ずつ押す。

```sh
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct
```

期待する出力は例えば`cec: command=play source=0`に続く
`player: state=PLAYING ...`。`source`は送信元Logical Addressであり、REGZAでは
通常TVの0になる。状態が変わらないキーは音声engineを再同期しない。CECのkey
releaseや長押しの扱いは未実装。

CEC UAPIではopen直後のfile handleはfollowerではない。CecDeviceは
`CEC_MODE_INITIATOR | CEC_MODE_FOLLOWER`を明示してからLogical Addressを扱う。
これがないとPi宛のメッセージはCEC frameworkにより破棄され、`CEC_RECEIVE`には
届かない。exclusive followerにはしないため、`cec-ctl`による通常の確認は可能。

### CEC入力遅延の切り分け

`--cec-diagnostics`を付けると、受信したUI commandに
`kernel_to_daemon_us`と`receive_ioctl_us`を出力する。前者はkernelが
`cec_msg.rx_ts`に記録した受信時刻からdaemonが取り出した時刻まで、後者は
`CEC_RECEIVE` ioctl自体の所要時間。いずれもLinuxの`CLOCK_MONOTONIC`基準である。
250msごとのCEC状態確認が10ms以上かかったときは`cec: update_us=...`も出す。

```sh
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct --cec-diagnostics
```

`kernel_to_daemon_us`が小さければ、リモコン押下からkernel受信まで（TVまたは
CEC bus側）が遅い。大きければdaemonが受信キューを読むまでが遅い。
受信キューは一度のPOLLINで空になるまでdrainするため、他のCEC trafficが
キーを後ろへ押しやることを防ぐ。`update_us`が大きければ、状態確認ioctlが
main loopを止めている。これらの実機結果を得るまでは、待受方式の追加変更を
決めない。

## 現段階の制限

- CECリモコンのPlay/Pause/Stop/Skip Forward/Skip Backward/Fast Forward/Rewindは
  接続済み。`GIVE_DEVICE_POWER_STATUS`にはONを返す。Fast Forward/Rewindの長押し・
  連続加速は未実装。
- ALSA設定は起動時に同期実行する。media確認とTOC取得はmedia workerで実行する。
- 既存の読み取り中I/Oが戻るまではworkerをjoinできず、process終了が待たされる。
  終了前にはALSAをdropするが、kernel/driverの停止時間は保証しない。
- seek後の音が出るまでには旧readの終了と新しい読み取り・prebufferが必要。
- main loopは10ms poll。CEC状態確認は250ms間隔。実測後にpoll descriptor統合を検討。
- 自動media検出と途中交換後のTOC再取得を実装済み。PLAYING中はstatus pollingを
  新規発行せず、読み取りエラー後に再開する。完全なdevice access直列化は未実装。
- worker errorsはSTOPPEDにし、次のPlay時にreader再生成を試みる。
- 状態のPLAYINGは要求状態で、prebuffer待ちも含む。
- direct backendの実機連続再生は確認済み。paranoia backendの連続再生と比較は未試験。

## hardware不要の確認

偽reader/outputで世代切替・queue上限・partial write・進捗・Pause・自然終了・
通常error停止・ALSA underrun後のreader再生成と再bufferをテスト。
ALSA null pluginでconfigure/write/drain/resetも確認。

## 実機確認: Play / Stop

ユーザーがdirect backend、plughw:CARD=vc4hdmi,DEV=0で正常再生を確認。
起動時STOPPED track=1 lba=0、play後PLAYING、stateでlba=1319への進行を確認。
stop後はSTOPPED track=1 lba=0。quitで出力停止・worker終了待ちのログを確認。
提示ログに読み取り/ALSAエラーなし。quit後の終了コード・停止所要時間は未測定。
Pause/再開・Seek・トラック変更・自然終了は引き続き実機未検証。

## 実機確認: Pause / 再開

ユーザーがdirect backendでPause/再開を2回試し、正常との報告。
PAUSED track=1 lba=880 → PLAYING lba=880、続いて
PAUSED lba=1825 → PLAYING lba=1825をログで確認。
その後stopでSTOPPED lba=0。提示ログに読み取り/ALSAエラーなし。
Pause中に時間を置いたstateの比較・再開latencyの測定は未実施。
入力された「1.」はユーザー申告の不要入力で、shellのcommand not foundは
プレイヤーの再生エラーとは区別する。
Seek・トラック変更・自然終了は引き続き実機未検証。

## 残課題: Pause復帰の待ち時間

ユーザーの体感でPause復帰から発音まで約0.5秒の待ち時間がある。
現方式ではPauseでqueue/ALSA bufferを破棄し、再開時にseek・再読込・
2秒分のprebuffer蓄積を行うため、これらとALSA開始待ちが原因候補。
各処理と実際の発音までの時間内訳は未計測であり、原因は未確定。
改善候補は未再生PCMの保持とALSA pause対応の確認（非対応時の代替処理を含む）。
ユーザー合意により当面は現方式を維持し、Seek・トラック選択・CEC接続を優先する。

## 実機確認: 前方シークと曲境界

旧Stop仕様の段階で、ユーザーがdirect backendでseek 10/20/30を実行し、正常再生と曲境界を
越えるシークを確認。seek 30後はPLAYING track=2 lba=18959、
その後stateでlba=19589への進行を確認。
stopでSTOPPED track=2 lba=17607となり、当時の仕様どおり選択中トラックの先頭へ戻った。
提示ログに読み取り/ALSAエラーなし。コマンド間の経過時間は記録されていないため、
表示LBAの差をseek量だけと解釈しない。
負方向のseek、Next/Previous、番号指定、ディスク終端での自然終了は
このログでは未確認。後続試験で確認する。

## 実機確認: 番号指定・Next・後方シーク

ユーザーがdirect backendでtrack 2 → play → nextを試し、
STOPPED track=2 lba=17607からPLAYING track=3 lba=34436への変更を確認。
旧仕様のpreviousでtrack=2へ移動し、seek -30でtrack=1 lba=16147へ戻った。
stopでtrack=1 lba=0。正常動作との報告で、提示ログに読み取り/ALSAエラーなし。
prebiousは入力の誤字によるunknown command。

この試験後、Previousを曲頭から3秒を境に前曲／現在曲の先頭へ戻る仕様に変更。
境界と状態維持はhardware不要テストで確認し、新仕様の実機試験は未実施。
ディスク終端での自然終了も引き続き未確認。

## 実機確認: CEC入力遅延の切り分けと受信queue drain

リモコン操作後の`cec: command`出力に遅延があるとの報告を受け、CEC fdが
POLLINになったときに受信queueを空になるまでdrainするよう変更した。変更前は
一度のpoll iterationで一つだけdequeueしていたため、Pi宛の他のCEC messageが
ある場合には後続のキーが待たされ得た。

direct backendで`--cec-diagnostics`を付け、Play、Pause、Stop、Skip、
Fast Forward、Rewindを実機操作した。`kernel_to_daemon_us`は通常31〜209µs、
最大値は約1.1msだった。`receive_ioctl_us`は3〜20µsであり、
10ms以上の`cec: update_us`は観測されなかった。従ってこの試験では、
kernelが受信済みのCEC commandをdaemonが処理するまでに目立つ待ち時間はない。

ユーザーの体感では変更後の入力応答はかなり改善し、許容できる水準となった。
この結果により、現在のnon-exclusive follower + poll + queue drainを維持する。
TVリモコン押下からkernel受信までの遅延、または音声切替後の発音遅延はこの計測の
対象外であり、問題が再発した場合は別の計測として扱う。

同じ試験で、CECのPlay/Pause/Stop/Skip Forward/Skip Backward/Fast Forward/Rewind
による状態変更を確認した。Stop後はTrack 1、LBA 0へ戻り、次のPlayもTrack 1から
始まった。Fast Forward/Rewindの一回の受信はそれぞれ10秒前後の相対seekとして
処理され、長押し時にTVが繰り返し送るcommandも受信した。長押しの加速規則は
現時点では持たず、受信ごとの固定10秒seekである。

## CEC Power Status 応答

TVからの`GIVE_DEVICE_POWER_STATUS`（0x8f）を受信したら、受信メッセージの
destinationを送信元へ折り返し、`REPORT_POWER_STATUS: ON`（0x90, 0x00）を
送る。PiCDPlayerにstandby状態はまだないため、daemonが動作中でCEC adapterが
登録済みならONとする。送信結果はnonblockingのCEC transmit queueへ返るため、
その後の結果メッセージは受信queue drainで消費される。

実機試験では`cec-ctl --monitor`等でTVからの0→8 `GIVE_DEVICE_POWER_STATUS`と、
Piからの8→0 `REPORT_POWER_STATUS: ON`を確認する。PiCDPlayerの標準出力には
`cec: power_status=on source=0`が出る。TVのPower/入力遷移を自動実行せず、
ユーザー操作で問い合わせが発生したときだけ観測する。

REGZA環境でdaemon起動後に実機確認した。TV（source 0）からPower Status
問い合わせが4回届き、毎回`cec: power_status=on source=0`を確認した。
問い合わせの回数はTVの実装によるため、PiCDPlayerは重複を抑制せず各問い合わせへ
応答する。この操作によるARCの状態変化は報告されていない。

## CEC Playback Device: active source routing

PiCDPlayerは起動時に`ACTIVE_SOURCE`を送らない。CD再生daemonの開始だけでTVの
表示入力を奪わないためである。代わりに、TVがbroadcastする`SET_STREAM_PATH`の
Physical AddressがPi自身の値と完全一致した場合だけactive状態にし、Logical Address
8から`ACTIVE_SOURCE`をbroadcastする。標準出力には次が出る。

```text
cec: active_source=4.0.0.0 reason=set_stream_path
```

他機器の`ACTIVE_SOURCE`を受信した場合、PiCDPlayerのactive状態はfalseになる。
そのためbroadcastの`REQUEST_ACTIVE_SOURCE`には、active状態のときだけ応答する。
これは複数のPlayback Deviceが同時にTV入力を主張することを避ける最小の規則である。
Physical Addressが異なる`SET_STREAM_PATH`もactiveを解除する。Power Statusと同様、
CEC transmitはnonblockingで、送信結果はreceive queue drainが消費する。
状態が変化したときは`cec: active_source=... reason=... path=...`を記録する。

最終実機試験では、REGZAでPiのHDMI4入力を選択し、`SET_STREAM_PATH 4.0.0.0`と
Piからの`ACTIVE_SOURCE 4.0.0.0`を確認する。次に別のHDMI入力を選び、別機器の
`ACTIVE_SOURCE`後にTVからの`REQUEST_ACTIVE_SOURCE`へPiが応答しないことを確認する。
TVの入力切替はユーザーが実行し、daemonは自動で実行しない。

REGZAで最終実機試験を実施し、`SET_STREAM_PATH 4.0.0.0`に対して
`active_source=4.0.0.0 reason=set_stream_path`を確認した。CECリモコンから
Play、10秒前方seek、NextでTrack 4まで移動し、StopでTrack 1 / LBA 0へ戻った。
複数のPower Status問い合わせにもONで応答した。ユーザーが他ソースへの自動切替も
正常と確認し、提示ログにCEC送受信・CD読み取り・ALSAのエラーはなかった。

CEC受信とPlayback Device応答は`--player`だけでなく、引数なしのdaemon modeでも
同じCEC fdをpollして処理する。daemon modeにはPlayerControllerがないため
リモコンcommandはログのみだが、Power StatusとActive Source routingは応答する。
Physical Addressが無効化・変更された場合はcached active状態を破棄し、古い経路を
`ACTIVE_SOURCE`として応答しない。device消失後の再openは引き続き未実装。
