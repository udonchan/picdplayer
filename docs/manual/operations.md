# 操作・診断手順

ビルドは[ビルド手順](../manual/build.md)、常駐運転は[systemd手順](../manual/systemd.md)を参照する。
以下はリポジトリrootから実行する。手動playerやdrive診断の前に既存serviceを停止する。
トレイ操作と試聴は人間が行い、daemonと診断でdriveを同時使用しない。

## 対話操作

```sh
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct --interactive
```

`play pause stop next previous track N seek SECONDS state quit`を改行で入力する。
`seek 10`は10秒先、`seek -10`は10秒前。対話CLIにeject commandはない。
標準入力を使わない運転では--interactiveを外し、CECまたはAPIで操作する。

先読みbufferはCD frame単位で指定できる。75 frameが1秒、値は15の倍数、容量上限は2250 frame。
次は容量10秒・再生開始4秒の例。省略時は容量300 frame（4秒）、開始150 frame（2秒）。

```sh
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct \
  --read-buffer-frames 750 --startup-buffer-frames 300
```

実験用の反復一致読み取りは次で有効にする。既定75 CD frame区間を最大3回読み、PCM全体が2回一致した
候補だけを再生する。既定は`single`。repeatの通常CD再生と、起動中のpolicy切替は確認済みだが、
傷disc評価は未実施である。確認範囲は[検証状況](../development/verification.md)を参照する。

```sh
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct \
  --read-verification repeat
```

起動、seek、track変更後には次のログが出る。`wait_ms`はPCM先読みが再開条件に達するまでの時間で、
HDMI、TV、ARC、アンプの出力遅延は含まない。

```text
player: prebuffer_ready wait_ms=... queued_blocks=... target_frames=...
```

同じ値は`GET /api/state`の`read.last_prebuffer_wait_ms`と`read.prebuffer_target_frames`でも取得できる。

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
外部listenを使うdebug構成でも、policy変更を含む操作APIはloopbackからだけ受け付ける。

開始閾値は容量以下でなければならない。大きなbufferは短いread stallへの余裕を増やす一方、
起動・seek後の待ち時間とmemory使用量を増やすため、production既定値は実機比較後に決める。

## 一回実行の診断

```sh
./build-direct/cdplayerd --probe-drives
./build-direct/cdplayerd --probe-media /dev/sr0
./build-direct/cdplayerd --probe-toc /dev/sr0
./build-direct/cdplayerd --probe-cdda /dev/sr0 --cdda-reader direct --track 1 --frames 75
```

probe-cddaは既定でPCMを捨て、再生しない。保存には`--pcm-output /tmp/track1.pcm`を追加する。
保存形式はraw S16_LE・44.1 kHz・stereo。既存ファイルを上書きしない。framesは1〜750。
paranoia比較時はENABLE_PARANOIA=ONのbuildで`--cdda-reader paranoia`を指定する。

```sh
./build-metadata/cdplayerd --probe-disc-id /dev/sr0
./build-metadata/cdplayerd --probe-metadata /dev/sr0 --metadata-cache /tmp/picdplayer-cache
./build-metadata/cdplayerd --lookup-disc '6JTbUgqHL29gzUyOH5ir60K3hz0-' --metadata-cache /tmp/picdplayer-cache
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
GETまたは`ws://127.0.0.1:8080/api/events`でmedia.stateを確認する。
EJECTINGなら要求を保持している。EJECT_ERRORならmedia.errorを確認してから再試行する。

別PCからの状態照会は`--api-listen 0.0.0.0 --api-port 8080`を追加し、
`http://PI_ADDRESS:8080/api/state`へ接続する。外部からの操作POSTは403であり仕様どおり。
WebSocketは接続時と変化時に同じschemaを送る。定期heartbeatとしての配信はしない。

### Technical status画面

APIを有効にしたplayerへブラウザから次のURLで接続する。

```text
http://PI_ADDRESS:8080/debug/status
```

画面は読み取り専用で、player状態、現在位置、現在再生中と最新先読みのread evidence、集計、
drive能力と根拠、disc/metadata、直近8件の観測を表示する。NO DISCではcurrent PCMをCLEANとせず
NO DISCと表示する。右上がLiveならWebSocket接続中。切断時はReconnectingとなり、1.5秒後に
GET stateで現在値を復元して再接続する。ここから再生操作は行わない。

これはPhase 1bの診断画面であり、TV向け本番UI、ジャケット表示、kiosk起動ではない。

## ログの読み方

| ログ | 意味 |
|---|---|
| cec: command | キーを受信して意味的操作へ変換。EJECTING中は適用しない |
| media: state | media観測またはeject状態遷移 |
| eject=started wait_ms | API受付からworker投入まで。main loopの遅れも含む |
| eject=completed elapsed_ms | API受付から完了結果の回収まで。トレイ確認を含む |
| eject=already_pending | 重複要求を受理したがhardware操作は追加しない |
| player: failure_context | tick間隔、残PCM、CD read所要時間・進行時間 |
| player: underrun recovery | reader再生成・先読み増加による復旧開始 |
| metadata: stale_result_discarded | 世代不一致の古い結果を破棄 |

CECの遅延診断には--cec-diagnosticsを使う。queue空・read_inflight_us増大は供給不足の手掛かり、
tick_gap_us増大はmain loop遅延の手掛かりだが、単独の値で原因を断定しない。
shutdownの「waiting for outstanding drive I/O」はworker join待ちを示す。
