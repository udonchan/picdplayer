# 操作・診断手順

ビルドは[README](../README.md)、常駐運転は[systemd手順](systemd.md)を参照する。
以下はリポジトリrootから実行する。手動playerやdrive診断の前に既存serviceを停止する。
トレイ操作と試聴は人間が行い、daemonと診断でdriveを同時使用しない。

## 対話操作

```sh
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct --interactive
```

`play pause stop next previous track N seek SECONDS state quit`を改行で入力する。
`seek 10`は10秒先、`seek -10`は10秒前。対話CLIにeject commandはない。
標準入力を使わない運転では--interactiveを外し、CECまたはAPIで操作する。

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
