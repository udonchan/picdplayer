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

API有効時、Pi自身のloopbackから基本transport操作を要求できる。次は例であり、
`play`、`pause`、`stop`、`previous`、`next`はすべてbodyなしPOSTで受理時は204となる。

```sh
curl --fail --show-error -X POST http://127.0.0.1:8080/api/play
curl --fail --show-error http://127.0.0.1:8080/api/state | python3 -m json.tool
```

204は要求を受け付けたことだけを示す。現在の状態と同じ操作ではsnapshotが変わらないこともある。
直後のGETがまだ操作前のsnapshotを返すこともあるため、期限を決めて期待する状態更新を待つ。
discなし/EJECTING時などは409、bodyを付けた基本操作は400、GET等の誤methodは405である。
外部peerからの操作POSTは本文の有無に関係なく403となる。通信失敗後に自動でPOSTを再送せず、
stateを再取得してから次の操作を判断する。再生状態の正本はdaemonのsnapshotである。

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

### Device観測と再接続

標準Playerとtechnical statusはdrive付近にnodeの観測contextを表示する。
`drive.observation`のgenerationは同じsession内の失効counterで、物理同一性IDではない。
PRESENTはnode/必要metadataを観測したこと、ABSENTはnodeがないこと、UNKNOWNは未取得/失敗/非対応等を示す。
discやtray状態は別であり、playerのNO_DISCだけからphysical driveが抜かれたと判断しない。

状態/signatureが変わると再生・validated TOC・旧capability/metadata/診断を失効し、ownerのI/O/close終了後に再probeする。
表示のrevalidating/retiring old I/O中は、新contextに対する操作を受け付けない場合がある。
再接続だけでは旧streamを自動再開しない。freshなmedia/TOCを確認してからPlay等を再操作する。
read policy/requested speed/明示offset等の設定は保持するが、以前のsupportやrequest receiptは現状として流用しない。

観測は既定250 ms samplingで、同じsignatureのresetやsample間の交換等は検出を保証しない。
UNKNOWNをNOに変換せず、観測不明でもfreshなmedia確認に基づくlegacy single再生を不必要に禁止しない。
一方、current contextにbindできないC2 reportでC2 pointer packetを有効にしない。
取消は進行中ioctlを中断するものではなく、service終了はownerのjoinを待つ場合がある。

USB切断/reset/over-currentやread errorを新しく観測した場合は追加読出しを止め、状態・時刻・ログと設定を保全する。
物理抜き差し、電源操作、強制reset、sysfs unbind/udev triggerは別の手順・承認で行い、正常再生の試験と混ぜない。
観測の表示や短時間正常再生だけで実機hotplug/resetを確認済みとは扱わない。

### 診断更新と音声優先

標準Playerのdisc mapは`GET /api/disc-map`を使い、詳細履歴128件の周期生成を要求しない。
詳細が必要なclientは従来の`GET /api/read-history`を使える。両者は非同期capture時点の結果であり、初回や世代変更後は503になり得る。
503を再生失敗と読み替えず、短い間隔で連続再送しない。標準Playerは生成待ちを2秒後に再取得する。
同contextでは完成済みの古いJSONが返り得るため、`snapshot.captured_at_unix_ms`とsession/stream/disc世代を確認する。
mapのcaptured時刻（UTC）は診断の取得時点であり、drive health・read完了や音声位置の証明ではない。
履歴eviction・未知の能力・欠落の意味は変えず、JSONの更新を遅らせても音声を優先する。

### 明示read offset

`--player`限定で起動時に`--read-offset-samples SIGNED_INTEGER`を指定できる。
単位はstereo sample frame（左右を合わせた一つの時点）であり、CD frameやchannel別sample数ではない。
符号規約は`logical p <- physical p + offset`。次は構文例であって、このdriveの校正値ではない。

```sh
/usr/local/bin/cdplayerd --player /dev/sr0 --cdda-reader direct --read-offset-samples +1
```

符号付き32-bit十進整数を受け付け、不正値・重複指定・player以外への指定は拒否する。
未指定はnull/UNKNOWNで従来のread経路を維持し、明示0はUSER_CONFIGUREDとして区別する。
運用設定から渡す場合は既存`PICDPLAYER_EXTRA_ARGS`を使い、既存値を上書きしない。
Settings/保存schemaやruntime変更APIはなく、変更にはdaemon再起動が必要である。

補正可能な完全CD frame範囲だけを再生し、範囲の終端はqueue出力・drain後に停止する。
物理的に取得不能な区間と、frame整列で追加除外する最大587 sample frame（約13.31 ms）を別々に示す。
負offsetでは最初の一部を飛ばし、正offsetでは元の曲長より前に終わる場合がある。
TOC/曲長は変えない。再生可能範囲が空なら操作を拒否し、ゼロ埋めや未補正PCMへの自動切替は行わない。

`drive.read_offset_samples`、`drive.read_offset_source`、`drive.read_offset_detail`、
`read.offset_window`、各evidenceの`offset`と`offset_status`で
入力根拠・除外区間・適用結果を確認する。windowは計算上の範囲であり取得済みcoverageではない。
標準Player/technical statusのdrive付近にも端除外を表示する。
設定値の根拠が不明なら推測して入力せず、vendor/modelや試験値±1を校正済みと扱わない。
CORRECTEDは補正処理の適用であり、物理校正、原盤一致、外部照合成功、bit-perfectの保証ではない。

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
`drive.current_speed_x`は現行実装では常にnullである。失敗は再生開始を止めない。速度要求の実機評価は通常継続運転#184、定量比較#83、非対応drive/失敗時#146へ移管した。

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
指定しなければ従来どおりruntime変更は再起動後に残らない。標準PlayerのSettingsからSINGLE/REPEATを
変更できる。背景設定もAPI由来の利用可否を示す。この開発差分ではmetadataと有効なproject keyが
あれば明示ONを要求できる。key指定だけでは外部取得を開始せず、画像到着・許諾済みも保証しない。
背景の保存は未実装で、Read Policyの保存設定とは独立してsession限りと表示する。
`GET /api/read-policy`の`persistence_configured`は保存先を指定したかだけを示し、個々のPOSTの
保存成功はHTTP応答で確認する。falseのとき変更は現在のdaemon sessionだけに有効である。
`requested_source`と`effective_source`はそれぞれ要求値と適用済み値の出所を示す。
`startup`はbuilt-in値または起動引数、`restored`は起動時に検証して読み込んだ保存値、
`saved`は保存成功後のAPI変更、`session`は保存先なしのAPI変更である。pending中は両者が異なり得る。

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

サービス稼働中に明示的なdisc不在またはejectを経てAudio CDを挿入すると、TOC受理後に先頭trackから
自動再生する。metadata候補が複数でも選択を待たない。daemon起動時に既に入っていたCDは自動再生せず、
STOPPEDでPlay操作を待つ。STOP後のmedia pollやmetadata更新では再生を再開しない。

```text
INFO drive: stop_command=accepted elapsed_ms=... rotation=UNVERIFIED
```

これは停止要求の受理を記録するだけであり、driveが実際に停止したという状態表示ではない。次の再生、eject、
service restartを対象driveで確認する。

probe-cddaは既定でPCMを捨て、再生しない。保存には`--pcm-output /tmp/track1.pcm`を追加する。
保存形式はraw S16_LE・44.1 kHz・stereo。既存ファイルを上書きしない。framesは1〜750。
paranoiaの実験・検証時はENABLE_PARANOIA=ONのbuildで`--cdda-reader paranoia`を指定する。公式配布には採用しない。

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

`/player`は再生情報と基本transport操作を表示するPlayer画面である。album/artist/track titleはselected metadataが
AVAILABLEの場合に表示する。metadataを有効にするには起動時に`--metadata musicbrainz`と必要なら
`--metadata-cache PATH`を指定する。metadataが無い、見つからない、または複数候補の場合も、track番号と
再生位置は表示できる。daemonが取得・cacheした画像をsame-originの`/api/presentation/artwork/cover`から読み、失敗時はCDの
プレースホルダーを表示する。標準Playerはloopback APIで基本transport操作と曖昧metadata候補の選択を送れる。
任意のWPE優先kiosk（Chromium/Cageは復旧選択肢）による
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

## WPEのInspector診断とスクリーンショット

### 診断profileとoverride

applicationのDebugはInspectorを既定ON（`127.0.0.1:9223`）、Release系は既定OFFにする。
packageに記録されたbuild typeと実効Inspectorは別である。launcherの
`picdplayer-wpe --diagnostics`はDRMへ接続せずbuild type/profileと実効Inspector設定を表示する。
private SDKを使う手動実行では`LD_LIBRARY_PATH=/opt/picdplayer-wpe/2.54.0/lib`が必要となる。
通常起動でも同じ識別情報をjournalへ残す。portが実際にlistenできた証明ではない。

| 設定 | 効果 |
|---|---|
| `PICDPLAYER_INSPECTOR=off` | 最優先でWPEの両Inspector環境変数を消去。Chromium debug portもOFF |
| `PICDPLAYER_INSPECTOR=on` | Debug/Releaseとも明示ON。HTTP address未指定なら`127.0.0.1:9223` |
| toggle未指定 + `WEBKIT_INSPECTOR_HTTP_SERVER=127.0.0.1:PORT` | 既存drop-inとの互換性を維持して明示ON |
| toggle未指定 + 空のHTTP変数 | WPE Inspectorを明示OFF |
| いずれも未指定 | buildの既定に従う |

toggleの未知値、非loopback・不正port（1〜65535以外）、別protocolの非空
`WEBKIT_INSPECTOR_SERVER`はWPE起動前に拒否する。値そのものはerrorに転載しない。
DebugでもLAN/`0.0.0.0`へ公開できない。Chromiumはtoggle/build既定だけを使用し、
有効時は既存の`127.0.0.1:9222`を使う。WPEのHTTP変数でChromiumを有効化しない。

`/etc/default/picdplayer-kiosk`や専用drop-inへtoggleを指定できる。systemdでは
`EnvironmentFile=`の値は`Environment=`より優先し、複数fileでは後で指定したfileが優先する。
既存の`90-wpe-source.conf`等が別のEnvironmentFileを追加している場合はそちらも確認する。
package更新はこれらを削除しないため、Releaseへ戻しただけでは明示ONは解除されない。
復元は今回追加した専用設定だけを戻すか、実効EnvironmentFileで`PICDPLAYER_INSPECTOR=off`を指定する。
service変更・再起動には実機状態の確認と具体的な承認が必要で、daemonを診断目的で再起動しない。
秘密情報が混ざり得る全環境・EnvironmentFileの内容をそのままlogや成果物へ保存しない。

WPE主runtimeには`scripts/inspect-wpe.py`を用意している。これはChromium CDPとは
別のWebKit target routingを使い、pageのtitle・readyState・viewport・接続表示を読み取る。
`--screenshot PATH`を指定すると、実WPEの現在のviewportを`Page.snapshotRect`でPNGへ保存する。
標準packageはWPEを供給し、開発Piでは利用者が初回表示・再生を確認した。
2026-10-14にはこのtoolのPageClientを再利用して実WebKit接続、Runtime評価、`Page.snapshotRect`のPNG取得に
成功した（[fanart.tv検証記録](../development/verification.md)）。tool追加やmock試験だけを実機確認とは扱わない。

実機のWPEを起動するsudo操作は利用者が行う。既存の隔離rootfsでの一時試験には
`scripts/test-wpe-kiosk.sh`があるが、通常serviceのWPE移行試験とは別であり、この診断toolは
serviceの停止・起動やsudoers変更を行わない。InspectorはWPEの起動時に
`WEBKIT_INSPECTOR_HTTP_SERVER=127.0.0.1:9223`でloopback限定にする。
`WEBKIT_INSPECTOR_SERVER`は別protocolなので、このHTTP/WebSocket toolの設定として代用しない。
Release系のkioskで一時的に有効化する場合は、Pi上で専用runtime drop-inを作る。既存の同名fileがある場合は
上書きせず、別名を選ぶ。ほかのdrop-inや`/etc/default/picdplayer-kiosk`を置換しない。

```sh
sudo mkdir -p /run/systemd/system/picdplayer-kiosk.service.d
sudoedit /run/systemd/system/picdplayer-kiosk.service.d/90-wpe-screenshot.conf
```

この新規fileには次の設定だけを記載する。API keyは記載しない。

以下は実効設定に`PICDPLAYER_INSPECTOR=off`や空のHTTP変数がない場合の例である。
明示OFFがある場合、このHTTP設定だけでは有効にならない。所有者と元の値を確認し、承認を得て
専用の設定箇所だけを一時的に`PICDPLAYER_INSPECTOR=on`へ変更する。
EnvironmentFileの値はdrop-inのEnvironmentより優先するため、上書き関係を確認する。
撮影後は今回変更した値だけを元へ戻し、既存file全体を削除しない。

```ini
[Service]
Environment=WEBKIT_INSPECTOR_HTTP_SERVER=127.0.0.1:9223
```

```sh
sudo systemctl daemon-reload
sudo systemctl restart picdplayer-kiosk.service
```

これはkioskの再起動を伴うため、利用者が実機状態を確認して実行する。daemonや再生操作は変更しない。
既にInspectorを有効化したWPEが動作している場合だけ、Macで別terminalからSSH転送する。

```sh
ssh -nNT -o ExitOnForwardFailure=yes -L 127.0.0.1:19223:127.0.0.1:9223 PI_HOST
```

別terminalで、このcheckoutのrootから実行する。追加のPython packageは不要。

```sh
python3 scripts/inspect-wpe.py --list
python3 scripts/inspect-wpe.py --target-url http://127.0.0.1:8080/player
python3 scripts/inspect-wpe.py --screenshot /tmp/picdplayer-wpe.png
```

PNGはscriptを実行したMac等へ保存する。親directoryは事前に存在する必要があり、既存fileやsymlinkは
上書きしない。再撮影は別名を指定する。保存modeは0600、PNGは最大8 MiB。JSON結果にはpath・pixel寸法・
byte数を出し、画像本体のbase64はstdoutへ出さない。PNGのsignature/IHDR CRC/IENDを検査するが、
汎用decoderによる全pixel検証ではない。viewportは3840×2160相当まで、HiDPIのPNGは7680×4320相当まで。
撮影時だけWebSocket message上限を16 MiBへ増やし、既存の観測・Chromium測定は4 MiBのままにする。
`--list`と`--screenshot`は同時に指定できない。target照合・protocol・PNG・書込失敗時は成功として出力せず、
接続を閉じる。途中で生成したfileへの書込に失敗した場合はそのfileを削除する。

一致するtargetが一つだけの場合に接続する。WebSocket接続開始からhandshake・外側ack・page応答まで
25秒のdeadlineを共有し、無応答のsocket操作は最大5秒で失敗する（接続確立は最大15秒）。
targetの破棄・pause状態・protocol error・JavaScript評価例外は失敗として扱い、debuggerのresumeや
画面遷移は実行しない。終了時はsocketを閉じる。SSH転送も確認後にCtrl-Cで終了する。
JSON取得成功はTVのfirst pixel、描画の正しさ、音声、CEC、長期安定性の確認とは区別する。
PNGも実WPEのrendered-page snapshotであり、DRM scanoutや物理TVの撮影ではない。third-party画像が含まれる
場合は権利条件を別途確認し、未確認のPNGをGit/PR/配布物へ追加しない。

撮影後はPi上で今回作成した専用fileだけを削除し、通常kioskへ戻す。既存overrideをまとめて消す操作はしない。

```sh
sudo rm /run/systemd/system/picdplayer-kiosk.service.d/90-wpe-screenshot.conf
sudo systemctl daemon-reload
sudo systemctl restart picdplayer-kiosk.service
```

Release系でほかに明示ONがなければInspector portが閉じる。Debugでは既定ONへ戻るので、
閉じる場合は明示OFFを使う。実効portと通常WPEの稼働を確認し、Mac側のSSH転送も終了する。

## ログの読み方

daemonログは次の形式である。先頭はUTC、`+...ms`はprocess内logger起動からのmonotonic経過時間で、
wall clock補正が起きても処理間隔の比較に使える。

```text
2026-09-18T09:30:12.345Z +1234ms INFO player: state=PLAYING track=1 lba=0
```

DEBUG/INFO/NOTICEはstdout、WARN/ERRORはstderrへ出す。Debugでは全level、Release系では
DEBUG/INFOを既定で抑制し、起動profile・backendと終了のNOTICE、WARN/ERRORを保持する。
明示的なinteractive `state`応答とcommand案内もNOTICEとしてReleaseで維持する。
重複抑制済みのCEC待機・登録状態もNOTICEで残し、CEC入力ごとのINFO/DEBUGとは区別する。
`PICDPLAYER_LOG_LEVEL=debug|info|warning`で起動時のfilterを明示指定できる。NOTICEとWARN/ERRORは
どの指定でも抑制しない。未知値・空値は値を転載せず警告してbuild既定へ戻す。
常駐daemonでは`/etc/default/picdplayer`に設定する。kioskのtoggleとは独立している。
`--cec-diagnostics`やui_boot等のDEBUG/INFOを観測する場合は必要なlevelも明示する。
streamのformattingはfilter判定後に行うが、`<< expensive_function()`等の引数評価そのものは省略しない。
systemd運用では両方を次で確認できる。

```sh
journalctl -u picdplayer.service -f -o cat
```

出力先が長時間停止しても再生loopを待たせないためqueueは有界である。終了時の
`WARN logger: dropped=N`はN件を保存できなかったことを示す。filterで抑制した件数は含めない。
NOTICEもWARN/ERROR同様にqueue内のDEBUG/INFOを優先して押し出すが、飽和・sink障害時の無損失保証はない。
診断CLIの結果はこの形式に変換せず、Releaseでも維持する。

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
