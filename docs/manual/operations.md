# 操作・診断手順

ビルド・deployは[Mac + Docker手順](mac-docker-development.md)、常駐運転は[systemd手順](../manual/systemd.md)を参照する。
以下はdeploy済みのPi上で実行する。Linux単体ビルドでは実行ファイルのパスを置き換える。手動playerやdrive診断の前に既存serviceを停止する。
トレイ操作と試聴は人間が行い、daemonと診断でdriveを同時使用しない。

## 対話操作

```sh
/usr/local/bin/cdplayerd --player /dev/sr0 --cdda-reader direct --interactive
```

`play pause stop next previous track N seek SECONDS state quit`を改行で入力する。
`seek 10`は10秒先、`seek -10`は10秒前。対話CLIにeject commandはない。
標準入力を使わない運転では--interactiveを外し、CECまたはAPIで操作する。

先読みbufferはCD frame単位で指定できる。75 frameが1秒、値は15の倍数、容量上限は2250 frame。
省略時は容量750 frame（10秒）、開始45 frame（0.6秒）。次は開始を4秒へ増やす例である。

```sh
/usr/local/bin/cdplayerd --player /dev/sr0 --cdda-reader direct \
  --read-buffer-frames 750 --startup-buffer-frames 300
```

実験用の反復一致読み取りは次で有効にする。既定75 CD frame区間を最大3回読み、PCM全体が2回一致した
候補だけを再生する。既定は`single`。repeatの通常CD再生と、起動中のpolicy切替は確認済みだが、
傷disc評価は未実施である。確認範囲は[検証状況](../development/verification.md)を参照する。

```sh
/usr/local/bin/cdplayerd --player /dev/sr0 --cdda-reader direct \
  --read-verification repeat
```

### C2 error pointerの明示的な確認

`--direct-c2-pointers`はdirect readerだけで使う実験的な明示opt-inである。起動後のdrive probeが`c2_supported=YES`と報告した場合だけMMC `READ CD`でC2 error pointerを要求する。`NO`なら通常readを続けつつ`c2_status=NOT_AVAILABLE`、probe未完了・UNKNOWN・flag未指定なら`NOT_CHECKED`である。packetが失敗した区間は通常readへfallbackし、`UNKNOWN`として表示する。

C2 pointerが全てzeroでも、C2 trust、原盤一致、bit-perfect、disc全体の正常を保証しない。まず通常CDで短時間確認し、傷discや別driveの評価は#146で扱う。systemd標準設定にはこのflagを入れない。

```sh
sudo systemctl stop picdplayer-kiosk.service picdplayer.service
/usr/local/bin/cdplayerd --player /dev/sr0 --cdda-reader direct --direct-c2-pointers \
  --audio-device plughw:CARD=vc4hdmi,DEV=0 --api-port 8080 --no-cec
```

`GET /api/state`の`read.latest.c2_status`を確認し、確認後は手動daemonを終了してからsystemd serviceを再開する。

### 任意のドライブ速度要求

`--drive-speed-x`はLinux `CDROM_SELECT_SPEED`へCD倍速を一度だけ要求する診断的な設定である。
指定可能な範囲は1〜255、未指定時はドライブ既定設定を変えない。Audio CDを認識した後のSTOPPEDまたは
PAUSED中に、kernel capabilityが`speed_control=YES`と報告した場合だけ、PCM readerとejectを直列化した
MediaWorkerから実行する。

```sh
/usr/local/bin/cdplayerd --player /dev/sr0 --cdda-reader direct --drive-speed-x 1
```

成功は要求のioctl受理であり、実際の速度、物理的な回転、騒音低下、読み取りthroughputを測定または
保証しない。`GET /api/state`の`drive.requested_speed_x`はその成功した要求、
`drive.speed_request_error`はioctl失敗または能力非対応/不明により要求を適用しなかった理由、
`drive.current_speed_x`は現行実装では常にnullである。失敗は再生開始を止めない。速度変更時の実機比較は#8の未完了範囲である。

起動、seek、track変更後には次のログが出る。`wait_ms`はPCM先読みが再開条件に達するまでの時間で、
HDMI、TV、ARC、アンプの出力遅延は含まない。

```text
player: prebuffer_ready wait_ms=... queued_blocks=... target_frames=...
```

同じ値は`GET /api/state`の`read.last_prebuffer_wait_ms`と`read.prebuffer_target_frames`でも取得できる。

ALSA出力bufferの要求latencyは既定200 msである。main loopの一時停止に対する余裕を比較する場合は
100〜2000 msを指定できる。CD先読みbufferとは別の設定である。

```sh
/usr/local/bin/cdplayerd --player /dev/sr0 --cdda-reader direct \
  --audio-latency-ms 500
```

### 読み取りpolicyのruntime変更

API有効buildでは、loopbackから現在のpolicyを確認できる。

```sh
curl --fail --show-error http://127.0.0.1:8080/api/read-policy | python3 -m json.tool
```

次はrepeat policyを要求する例である。`region_frames`は15 frame刻みでread buffer容量以下、
`required_matches`は2以上かつ`maximum_attempts`以下、`time_budget_ms`は1〜60000である。

```sh
curl --fail --show-error -X POST http://127.0.0.1:8080/api/read-policy \
  -H 'Content-Type: application/json' \
  --data '{"mode":"repeat","region_frames":75,"required_matches":2,"maximum_attempts":3,"time_budget_ms":10000}'
```

再生中に変更すると`requested`だけが更新され、`pending: true`となる。現在のPCM streamは変更せず、
STOPPEDまたはNO DISCになった時点でeffectiveへ反映し、次回playからreaderを作り直す。
PAUSED中は保留する。停止中またはNO DISC中の変更は直ちにeffectiveとなる。
technical status画面の`Read policy`は適用済みmodeを表示し、保留中は
`REPEAT → SINGLE (pending)`のように適用済み値から要求値への遷移を示す。
外部listenを使うdebug構成でも、policy変更を含む操作APIはloopbackからだけ受け付ける。

`--settings-file /var/lib/picdplayer/settings.json`を明示した場合だけ、受理前にRead Policyの全5項目を
version付きJSONへ保存し、次のdaemon起動時に読み戻す。保存済みpolicyは起動引数の
`--read-verification`から作った初期値より優先する。保存失敗時はPOSTを拒否して現在値を維持する。
破損・非対応version・範囲外の保存値は採用せず、起動引数側へ戻してwarningを記録する。
この機能はAPI有効buildの`--player`で使い、親directoryは事前にservice userが書ける状態にする。
指定しなければ従来どおりruntime変更は再起動後に残らない。設定画面と背景ON/OFFは未実装。
`GET /api/read-policy`の`persistence_configured`は保存先を指定したかだけを示し、個々のPOSTの
保存成功はHTTP応答で確認する。falseのとき変更は現在のdaemon sessionだけに有効である。

開始閾値は容量以下でなければならない。大きなbufferは短いread stallへの余裕を増やす一方、
memory使用量が増える。開始閾値を増やすとplay・seek後の待ち時間も増える。
現行の既定750/45 frameは通常CDでの比較から採用しており、傷discでの評価は継続する。

## 一回実行の診断

```sh
/usr/local/bin/cdplayerd --probe-drives
/usr/local/bin/cdplayerd --probe-media /dev/sr0
/usr/local/bin/cdplayerd --probe-toc /dev/sr0
/usr/local/bin/cdplayerd --probe-drive-start /dev/sr0
/usr/local/bin/cdplayerd --probe-cdda /dev/sr0 --cdda-reader direct --track 1 --frames 75
```

`probe-drive-start`はLinux `CDROMSTART`を一回要求し、受理結果と所要時間を表示する。PCMは読まない。
`rotation=UNVERIFIED`は、ioctl成功だけでは実際の回転開始や継続時間を確認できないことを示す。
常駐serviceとdrive操作が競合しないよう、実機比較時はserviceを停止する。

player modeではAudio CDのTOC取得後、STOPPEDが5分連続した場合にLinux `CDROMSTOP`を一回だけ
background要求する。PLAYING、disc未準備、LOADING、eject中には要求しない。成功時はINFO levelで次を記録する。

```text
INFO drive: stop_command=accepted elapsed_ms=... rotation=UNVERIFIED
```

これは停止要求の受理を記録するだけであり、driveが実際に停止したという状態表示ではない。次の再生、eject、
service restartを対象driveで確認する。

probe-cddaは既定でPCMを捨て、再生しない。保存には`--pcm-output /tmp/track1.pcm`を追加する。
保存形式はraw S16_LE・44.1 kHz・stereo。既存ファイルを上書きしない。framesは1〜750。
paranoia比較時はENABLE_PARANOIA=ONのbuildで`--cdda-reader paranoia`を指定する。

```sh
/usr/local/bin/cdplayerd --probe-disc-id /dev/sr0
/usr/local/bin/cdplayerd --probe-metadata /dev/sr0 --metadata-cache /tmp/picdplayer-cache
/usr/local/bin/cdplayerd --lookup-disc '6JTbUgqHL29gzUyOH5ir60K3hz0-' --metadata-cache /tmp/picdplayer-cache
```

lookup-discはdrive不要だが実TOCとの曲数照合は行わない。候補やcache hit、画像URL状態を確認する。

## API操作

ENABLE_API=ONのbuildでplayerに`--api-port 8080`を追加する。Pi上で以下を実行する。

```sh
curl --fail --show-error http://127.0.0.1:8080/api/state
curl -i -X POST http://127.0.0.1:8080/api/play
curl -i -X POST -H 'Content-Type: application/json'   -d '{"offset_seconds":10}' http://127.0.0.1:8080/api/seek
curl -i -X POST -H 'Content-Type: application/json'   -d '{"track":2}' http://127.0.0.1:8080/api/track
curl -i -X POST http://127.0.0.1:8080/api/eject
```

pause/stop/next/previousもplayと同じbodyなしPOST。ejectは202が受付、完了は
GETまたは`ws://127.0.0.1:8080/api/events`で`disc.state`を確認する。
EJECTINGなら要求を保持している。EJECT_ERRORならdaemonのjournalにあるmediaエラーを確認してから再試行する。

別PCからの状態照会は`--api-listen 0.0.0.0 --api-port 8080`を追加し、
`http://PI_ADDRESS:8080/api/state`へ接続する。外部からの操作POSTは403であり仕様どおり。
WebSocketは接続時と変化時に同じschemaを送る。定期heartbeatとしての配信はしない。

### ブラウザ画面

`--custom-ui PATH`を指定すると起動時にユーザー編集版を検証・採用する。失敗時の表示通知、
manifestとassetの契約は[Custom UI](custom-ui.md)を参照する。`/builtin/player`は常にdefaultを表示する。

APIを有効にしたplayerへブラウザから次のURLで接続する。

```text
http://PI_ADDRESS:8080/player
```

`/player`は読み取り専用のNow Playing画面である。album/artist/track titleはselected metadataが
AVAILABLEの場合に表示する。metadataを有効にするには起動時に`--metadata musicbrainz`と必要なら
`--metadata-cache PATH`を指定する。metadataが無い、見つからない、または複数候補の場合も、track番号と
再生位置は表示できる。daemonが取得・cacheした画像をsame-originの`/api/presentation/artwork/cover`から読み、失敗時はCDの
プレースホルダーを表示する。この画面は操作を送らない。任意のChromium/Cage kioskによる
tty1への自動表示は[systemd常駐運転](systemd.md#chromiumcage-kiosk)を参照する。

`/debug/status`は以下のtechnical status画面であり、読み取り根拠やdrive能力を確認するために使う。

APIを有効にしたplayerへブラウザから次のURLで接続する。

```text
http://PI_ADDRESS:8080/debug/status
```

画面は読み取り専用で、player状態、現在位置、現在再生中と最新先読みのread evidence、集計、
drive能力と根拠、disc/metadata、直近8件の観測を表示する。NO DISCではcurrent PCMをCLEANとせず
NO DISCと表示する。右上がLiveならWebSocket接続中。切断時はReconnectingとなり、1.5秒後に
GET stateで現在値を復元して再接続する。ここから再生操作は行わない。

これはPhase 1bの診断画面であり、TV向け本番UIやkiosk起動ではない。

## ログの読み方

daemonログは次の形式である。先頭はUTC、`+...ms`はprocess内logger起動からのmonotonic経過時間で、
wall clock補正が起きても処理間隔の比較に使える。

```text
2026-09-18T09:30:12.345Z +1234ms INFO player: state=PLAYING track=1 lba=0
```

DEBUG/INFOはstdout、WARN/ERRORはstderrへ出す。systemd運用では両方を次で確認できる。

```sh
journalctl -u picdplayer.service -f -o cat
```

出力先が長時間停止しても再生loopを待たせないためqueueは有界である。終了時の
`WARN logger: dropped=N`はN件を保存できなかったことを示す。診断CLIの結果はこの形式に変換しない。

| ログ | 意味 |
|---|---|
| cec: command | キーを受信して意味的操作へ変換。EJECTING中は適用しない |
| media: state | media観測またはeject状態遷移 |
| eject=started wait_ms | API受付からworker投入まで。main loopの遅れも含む |
| eject=completed elapsed_ms | API受付から完了結果の回収まで。トレイ確認を含む |
| eject=already_pending | 重複要求を受理したがhardware操作は追加しない |
| player: failure_context | tick間隔、残PCM、CD read所要時間・進行時間 |
| player: underrun recovery | reader再生成・先読み増加による復旧開始。同じstreamでは最大3回 |
| player: underrun recovery_limit_exhausted | 4回目のunderrunで復旧上限に達したためエラー停止。read integrityのRECOVEREDとは別 |
| player: read_stall | 一回のreader callがeffective ReadPolicyの`time_budget_ms`を超えたためSTOPPEDへ遷移。ioctlの中断・PCM品質の判定ではない |
| metadata: stale_result_discarded | 世代不一致の古い結果を破棄 |

CECの遅延診断には--cec-diagnosticsを使う。queue空・read_inflight_us増大は供給不足の手掛かり、
tick_gap_us増大はmain loop遅延の手掛かりだが、単独の値で原因を断定しない。
shutdownの「waiting for outstanding drive I/O」はworker join待ちを示す。
