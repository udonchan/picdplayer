# PiCDPlayer

Raspberry Piで物理CDを再生する家電型プレイヤー。Linux CD-ROM ioctlでTOCと
CD-DA PCMを読み、ALSA経由でHDMIへ連続再生する。REGZAのCECリモコンによる
再生・停止・曲移動・シークと、CEC Playback Deviceとしての登録・応答を実機確認済み。

## Build / run

Linux、C++20コンパイラ、CMake 3.20以上、Linux CEC UAPI headersが必要。
native再生にlibasound（開発時はlibasound2-dev）が必要。Node.js、root権限は不要。

```sh
cmake -S . -B build
cmake --build build -j1
./build/cdplayerd
```

`/dev/cec0`への読み書き権限（この実機ではvideo group）が必要。
foregroundで動作し、標準出力・標準エラーにログを出す。Ctrl-C/SIGTERMで終了コード0。
systemd Type=simpleからの起動とjournalへのログ出力も実装済み。
上の引数なし起動はCEC確認用で、CD再生には`--player`を使う。

```sh
./build/cdplayerd --no-cec
./build/cdplayerd --cec-device /dev/cec0
./build/cdplayerd --cec-diagnostics
python3 tests/smoke.py build/cdplayerd
cec-ctl -d /dev/cec0 --show-topology
```

`build/`・`build-*/`はGit管理対象外。Pythonは開発用試験のみ。

## 現段階の設計

- 単一process。既存daemonモードは単一thread、--playerはCD読み取りとmedia確認の
  workerを各1本追加。
  signalfd + pollで停止を通常のイベントとして扱う。
- CecDeviceにLinux ioctlを隔離。Playback Device登録、CEC応答、リモコン入力を担当する。
- デバイス未出現・Physical Address未確定なら250ms間隔で再確認する。
  固定sleep後に準備完了と仮定せず実際の状態を判定する。待機中も停止可能。
- claimはO_NONBLOCKで開始し、後続の読み取りでLogical Addressを確認する。
  番号8は固定しない。バス上の空き番号をkernelが確保する。
- 同名のPlayback登録があれば再利用。異なる登録は上書きせずエラー終了。
- 終了時はfdを閉じるがadapterの登録は解除しない。ARCへの影響を避けるため。
  登録状態はdaemonの生存を示すものではなく、再起動後の維持も保証しない。
- 権限不足・ioctl失敗はエラー終了。デバイス消失後の再openやclaim失敗時の
  timeout/retry、排他的所有、CECイベントによる待機は今後の課題。
- `GIVE_DEVICE_POWER_STATUS`にはdaemon稼働中のONを返す。TVがPiのPhysical Addressを
  指定する`SET_STREAM_PATH`を送ったときだけ`ACTIVE_SOURCE`をbroadcastし、他機器の
  `ACTIVE_SOURCE`を受けると非activeへ戻す。`REQUEST_ACTIVE_SOURCE`にはactive中だけ
  応答する。起動時に入力を勝手に切り替えない。kernelの標準応答だけでは完全な
  Playback Deviceにはならない。
- PlayerController/PlayerStateはhardware非依存の状態遷移を実装済み。
  native連続再生は端末操作モードでPlay/Stopと正常再生を実機確認済み。CECの
  Play/Pause/Stop/Skip Forward/Skip Backward/Fast Forward/Rewindを
  PlayerControllerへ接続済み。
  systemd unitは実装・自動起動確認済み。API・UIは未実装。

詳細は[実機検証記録](docs/milestone-1.md)。

## 光学ドライブ検出

```sh
./build/cdplayerd --probe-drives
ctest --test-dir build --output-on-failure
```

実機出力:

```text
cd: device=/dev/sr0 vendor="ASUS" model="SDRW-08D2S-U"
```

`--probe-drives`はsysfsからSCSI peripheral type 5（CD/DVD）のデバイスを列挙して
終了する診断モード。CECやdaemonループは開始しない。USB/SATAを区別せず、
複数接続時もすべて表示する。ドライブがなければ明示的に表示して正常終了する。
読み取りエラーは標準エラーへ出して終了コード1。

sysfsはkernelが認識しているデバイスの情報。検出はCDの有無・Audio CDかどうか・
デバイスをopenできることを保証しない。現在/dev/sr0はroot:cdrom 0660、
udonchanはcdrom groupに所属している。診断自体はsysfsの読み取りのみ。
接続構成はsysfsでUSB配下と確認し、vendor/modelはASUS SDRW-08D2S-Uだった。

検出中に抜くと属性読み取りエラーになることがある。その場合は再実行する。
今回の試験は空・複数・除外対象・除去後のsnapshot・属性欠落の疑似sysfsと、
実機の接続状態で実施。USB device自体の抜き差しは未実施。
`--player`ではdisc状態を常時監視する。
この結果をもとにしたメディア状態の診断は次節を参照。

参照: [Linux sysfs](https://cdn.kernel.org/doc/html/latest/filesystems/sysfs.html)、
[Linux SCSI層](https://cdn.kernel.org/doc/html/latest/driver-api/scsi.html)。

## トレイ・メディア状態の診断

```sh
./build/cdplayerd --probe-media /dev/sr0
```

CDROM_DRIVE_STATUSを読み、DISC_OKの場合だけCDROM_DISC_STATUSで種別を調べる。
fdは正常終了時・例外時とも閉じる。CEC処理は開始しない。
O_RDONLY | O_NONBLOCKで開くため、ディスクがない状態でも問い合わせできる。
O_NONBLOCKはioctlの完了時間を保証しないため、`--player`ではmedia workerで実行する。
トレイ操作やロック設定の変更は行わない。cdrom groupの読み取り権限が必要。

|出力|意味|
|---|---|
|TRAY_OPEN|ドライブがトレイ開を報告|
|NO_DISC|ドライブがディスクなしを報告|
|NOT_READY|準備未完了。回転開始中などの可能性がある|
|DISC_OK|メディア準備完了。Audio CDとはまだ断定しない|
|NO_INFO / UNKNOWN|状態不明。ディスクなしと同一視しない|

rawにはkernelの戻り値を残す。open/ioctl失敗は状態と区別して終了コード1。
ドライブ・bridgeによってトレイ開とディスクなしを区別できない場合もあるため、
実際の操作と出力を比較する。操作中の一回の結果だけで状態が確定したとは扱わない。

ASUS SDRW-08D2S-U（/dev/sr0）で、ユーザーによる操作後に毎回診断を実行し、
以下の結果を確認した。すべて終了コード0。

|実際の操作後の状態|診断結果|raw|
|---|---|---|
|トレイ開|TRAY_OPEN|2|
|空で閉じる|NO_DISC|1|
|音楽CDを入れて閉じる|DISC_OK|4|
|CDを取り出し、空で閉じる|NO_DISC|1|

このドライブではトレイ開・空・メディア準備完了を区別でき、取り出し後に
NO_DISCへ戻ることを確認できた。この一回実行の試験では操作直後のNOT_READY遷移は未検証。
上の表のDISC_OKだけでは音楽CDと判定できない。種別判定の結果は次節を参照。
`--player`の常時監視と自動TOC取得は実装済み。TOCの一回実行診断は次節を参照。
参照: [Linux CD-ROM ioctl](https://docs.kernel.org/userspace-api/ioctl/cdrom.html)。

### ディスク種別の診断

同じ `--probe-media /dev/sr0` で、ドライブ状態に続けてディスク種別を表示する。
DISC_OK以外では `disc_status=NOT_QUERIED` と表示し、種別を問い合わせない。

- AUDIO: kernelが音声のみのCDと分類。
- MIXED: 音声とデータの混在。音声のみのCDと同一視しない。
- DATA_1 / DATA_2 / XA_2_1 / XA_2_2: kernelが報告するデータ形式。
- NO_INFO / UNKNOWN: 判定不能。Audio CDやディスクなしと決めつけない。

raw値も記録し、ioctl失敗は終了コード1で報告する。
状態確認と種別確認は別の問い合わせなので、その間の取り出しには注意が必要。
これは一回実行の診断であり、メディア交換をまたぐ一貫したsnapshotは保証しない。
kernel内部では種別判定のためTOCを参照し得るが、この種別診断ではアプリへのTOC取り込みは行わない。
ASUS SDRW-08D2S-U（/dev/sr0）で次を実機確認済み（終了コード0）。

- 空で閉じた状態: drive_status=NO_DISC raw=1、disc_status=NOT_QUERIED。
- ユーザーが音楽CDを挿入した状態: drive_status=DISC_OK raw=4、disc_status=AUDIO raw=100。

この音楽CDをkernelがAudio CDと分類することを確認した。
データCD・混在CD・読み取り不能メディアの実機試験は未実施。

## TOC診断（音声のみのCD）

```sh
./build/cdplayerd --probe-toc /dev/sr0
```

CDROMREADTOCHDRで最初・最後のトラック番号を読み、CDROMREADTOCENTRYで
各トラックとlead-outをCDROM_LBA形式で取得する。トレイ操作・再生は行わない。
AUDIO以外はエラーにする。混在CD・マルチセッションCDの長さの解釈は今後扱う。

- start_lba: CD上の開始セクター番号。絶対MSF表記とは150フレームの差がある。
- frames: 次のトラック開始（最後はlead-out）との差。
- duration: 分:秒:フレーム（1秒=75フレーム）。末尾はミリ秒ではない。
- span_frames: 最初のトラック開始からlead-outまでの長さ。

この長さはTOC上の開始位置間の区間であり、曲の可聴部分だけの長さではない。
CD-TEXT・曲名・隠しトラック・INDEX 00は取得しない。
全エントリを読み、番号範囲・LBA形式・非負位置・単調増加・音声trackを確認後に表示する。
open/ioctl失敗や不整合は終了コード1。途中までのTOCを完成結果として表示しない。
負のLBAはこの診断では未対応としてエラーにする。

複数ioctl間のメディア交換に対する一貫性保証はまだない。実行中はCDを交換しない。
Linuxの構造体はデバイス読み取り内で使用し、検証済みのDiscTocへ変換して返す。
参照: [Linux TOC ioctl](https://docs.kernel.org/userspace-api/ioctl/cdrom.html)。

### TOCの実機結果

挿入済みの音楽CDで終了コード0、全14トラックを取得。
最初の開始LBA=0、最後のトラック開始LBA=230183、lead-out LBA=242334。
全区間は242334フレーム（53分51秒9フレーム）、第1トラックは17607フレーム
（3分54秒57フレーム）。全開始位置が単調増加し、全トラックで音声属性を確認。
ユーザーがケース等の曲数も14曲であることを確認し、TOCの14トラックと一致した。
既存CTest・停止smoke testと、TOC診断の不適切なデバイス・引数不足・モード競合の
エラー経路も確認済み。空トレイや混在CDでのTOC診断は未実機試験。

## 内部TOCモデル

`include/disc_toc.hpp`はLinuxのヘッダーやデバイスに依存しない。

- Track: number、start_lba、length_frames。
- DiscToc: tracks、leadout_lba。span_frames()で全区間を取得。
- make_audio_toc(): 開始番号・開始位置一覧・lead-outを検証してモデルを生成。
- read_cd_toc(): Linux ioctlで読み、モデルを返す。返却時にはデバイスを閉じている。
- probe_cd_toc(): モデルから従来と同じ診断表示を生成。

モデルは音声のみのCDが対象。音声属性の確認はLinux側で行う。
開始番号は1固定にせず、1〜99の連続番号を扱う。位置は非負で単調増加、
lead-outは最終トラック開始より後であることを検証する。
長さは64bit整数のCDフレーム単位（1秒=75）で保持し、表示時だけ時間へ変換する。
モデルは単純な値構造体で、生成後の直接変更を型で禁止してはいない。
PlayerStateの位置は、--playerモードではALSAの未再生量を差し引いて更新する。

`disc_toc_test`は実測14トラックの変換、長さの合計、非ゼロ開始位置、
トラック99、空・不正番号・位置逆転・不正lead-out等をhardwareなしで確認する。

```sh
ctest --test-dir build --output-on-failure
```

## CD-DA / HDMIのCLI PoC

再現手順と実測のALSA設定は[CD-DA PoC記録](docs/cdda-poc.md)を参照。

## CDDA reader（directの実装段階）

交換可能なCddaReader、Linux CDROMREADAUDIO backend、読み取り専用診断を追加。
[設計・build・実機試験手順](docs/cdda-reader.md)を参照。
ENABLE_PARANOIA=ONでlibcdio-paranoia backendを有効化し、runtimeで選択可能。
OFFではlibcdio依存なし。paranoiaの実機読み取り・保存PCMの正常再生は確認済み。
両backendの条件を揃えた性能比較・連続再生比較は未実施。

## Metadata

既存の`DiscToc`からDisc IDを計算し、MusicBrainz lookup、内部候補model、Cover Art
reference取得、非同期worker、世代による古い結果の破棄、raw JSON cacheを実装した。
metadataは明示的に有効化し、失敗しても再生状態を変更しない。

```sh
cmake -S . -B build-metadata -DENABLE_METADATA=ON
cmake --build build-metadata -j1
ctest --test-dir build-metadata --output-on-failure
./build-metadata/cdplayerd --probe-disc-id /dev/sr0
./build-metadata/cdplayerd --probe-metadata /dev/sr0 --metadata-cache /tmp/picdplayer-cache
./build-metadata/cdplayerd --lookup-disc DISC_ID --metadata-cache /tmp/picdplayer-cache
./build-metadata/cdplayerd --player /dev/sr0 --cdda-reader direct \
  --metadata musicbrainz --metadata-cache /var/cache/picdplayer
```

依存パッケージは`libdiscid-dev libcurl4-openssl-dev nlohmann-json3-dev`。複数releaseは
`AMBIGUOUS`のまま保持し、自動選択しない。単一候補だけCover Art Archiveを問い合わせる。
cacheは書き込み失敗をlookup失敗にせず、DBを使わない。詳細は
[metadata subsystem設計案](docs/metadata-design.md)を参照。
systemd unitは`CacheDirectory=picdplayer`でservice userが書ける
`/var/cache/picdplayer`を作成する。metadataを常駐運転で使う場合は
`/etc/default/picdplayer`の`PICDPLAYER_EXTRA_ARGS`へ上記の`--metadata`引数を設定する。

## プレイヤー実装の進行

backendの採用判断・性能比較は保留中。
[PlayerControllerの状態と操作仕様](docs/player-controller.md)と、native音声engine、
CECリモコン操作、Playback Device応答を実装・実機確認済み。
[再生の構成と実機試験手順](docs/playback-engine.md)を参照。
将来のREST/WebSocket/UIが参照する読み取り用の統一値モデルは
[Daemon state snapshot](docs/daemon-state.md)を参照。

## Local API（読み取りPoC）

`ENABLE_API=ON`でlibwebsocketsを使うloopback限定HTTP serverをbuildできる。
playerへ`--api-port`を明示した場合だけlistenし、`GET /api/state`と`WS /api/events`を提供する。
callbackは既存main loopからserviceされ、別threadからPlayerControllerを操作しない。

```sh
cmake -S . -B build-api -DENABLE_API=ON -DENABLE_METADATA=ON
cmake --build build-api -j1
./build-api/cdplayerd --player /dev/sr0 --cdda-reader direct \
  --metadata musicbrainz --metadata-cache /tmp/picdplayer-cache --api-port 8080
curl --fail --show-error http://127.0.0.1:8080/api/state
```

WebSocketは接続時に現在のstateを1件送り、その後は公開snapshotが変わった時だけ同じJSON schemaを
送る。ブラウザでは`new WebSocket("ws://127.0.0.1:8080/api/events")`で接続できる。
loopback listen時はbodyなしの`POST /api/play`、`pause`、`stop`、`next`、`previous`も利用できる。

```sh
curl --fail --show-error -X POST http://127.0.0.1:8080/api/play
curl --fail --show-error -X POST -H 'Content-Type: application/json' \
  -d '{"offset_seconds":10}' http://127.0.0.1:8080/api/seek
curl --fail --show-error -X POST -H 'Content-Type: application/json' \
  -d '{"track":2}' http://127.0.0.1:8080/api/track
curl --fail --show-error -X POST http://127.0.0.1:8080/api/eject
```

別PCから診断する場合だけ、信頼できる開発用LAN上で明示的に外部listenを有効にする。
認証とTLSはまだないため、インターネットへ公開しない。外部listenでは状態取得とevent配信だけを
許可し、操作POSTは実際の接続元がloopbackの場合だけ許可する。LAN側からのPOSTは403にする。

```sh
./build-api/cdplayerd --player /dev/sr0 --cdda-reader direct \
  --metadata musicbrainz --api-listen 0.0.0.0 --api-port 8080

# 別PCから。PI_ADDRESSはRaspberry PiのLANアドレス
curl --fail --show-error http://PI_ADDRESS:8080/api/state
```

依存packageは`libwebsockets-dev`と`nlohmann-json3-dev`。ejectはPCM readerのdevice handle解放を
待ってからMediaWorkerで実行するため、再生中でもmain loopをblockしない。
JSON schemaとthread境界は[設計記録](docs/daemon-state.md)を参照。

## メディアライフサイクル

空で起動してCDの挿入・取り出しを扱うため、hardware観測とapplication状態を
分離した`MediaStateTracker`と、blocking ioctlをmain loop外で直列実行する
`MediaWorker`を追加した。Audio CDを認識するとTOCを読み、`PlayerController`を
`STOPPED`へ遷移させる。取り出しと非対応discでは`NO_DISC`へ戻る。
PCMは2秒先読みする。ALSA underrunの自動復旧はhardware非依存テスト済みだが、
実機では異常を再現できていないため、傷ディスク等での評価を今後行う。
[状態、設計判断、実機試験手順](docs/media-lifecycle.md)を参照。

## 常駐運転とsystemd

`--player`は標準入力を監視せず、CEC・media・signal eventで常駐する。
端末からコマンドを入力する開発試験では`--interactive`を追加する。
専用ユーザー、起動設定、install、unit検証手順は
[systemd常駐運転](docs/systemd.md)を参照。
Raspberry Pi実機でsystemdのboot時自動起動、Audio CD認識、CEC操作によるHDMI再生まで
確認済み。
