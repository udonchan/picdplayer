# PiCDPlayer

Raspberry Piで物理CDを再生する家電型プレイヤー。現在はdaemon skeleton・CEC登録・CD状態/TOC診断・内部トラックモデルまで。
CD-DA読み取りとHDMI再生は、外部CLIを使った10秒のPoCとして実機・試聴確認済み。

## Build / run

Linux、C++20コンパイラ、CMake 3.20以上、Linux CEC UAPI headersが必要。
外部runtime library、Node.js、root権限は不要。

```sh
cmake -S . -B build
cmake --build build
./build/cdplayerd
```

`/dev/cec0`への読み書き権限（この実機ではvideo group）が必要。
foregroundで動作し、標準出力・標準エラーにログを出す。Ctrl-C/SIGTERMで終了コード0。
将来systemd Type=simpleから起動し、journalへログを渡せる構成。

```sh
./build/cdplayerd --no-cec
./build/cdplayerd --cec-device /dev/cec0
python3 tests/smoke.py build/cdplayerd
cec-ctl -d /dev/cec0 --show-topology
```

`build/`・`build-*/`はGit管理対象外。Pythonは開発用試験のみ。

## 現段階の設計

- 単一process・単一thread。signalfd + pollで停止を通常のイベントとして扱う。
- CecDeviceにLinux ioctlを隔離。状態確認とPlayback Device登録のみを担当する。
- デバイス未出現・Physical Address未確定なら250ms間隔で再確認する。
  固定sleep後に準備完了と仮定せず実際の状態を判定する。待機中も停止可能。
- claimはO_NONBLOCKで開始し、後続の読み取りでLogical Addressを確認する。
  番号8は固定しない。バス上の空き番号をkernelが確保する。
- 同名のPlayback登録があれば再利用。異なる登録は上書きせずエラー終了。
- 終了時はfdを閉じるがadapterの登録は解除しない。ARCへの影響を避けるため。
  登録状態はdaemonの生存を示すものではなく、再起動後の維持も保証しない。
- 権限不足・ioctl失敗はエラー終了。デバイス消失後の再openやclaim失敗時の
  timeout/retry、排他的所有、CECイベントによる待機は今後の課題。
- Power Status、Active Source、SET_STREAM_PATH、リモコン処理は未実装。
  kernelの標準応答だけでは完全なPlayback Deviceにはならない。
- PlayerState、CD、ALSA再生、API、UI、systemd unitはまだ実装しない。
  PlayerState導入時にはdaemonを唯一のauthoritative ownerにする。

詳細は[実機検証記録](docs/milestone-1.md)。

## 光学ドライブ検出（次段階の最初のステップ）

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
実機の接続状態で実施。実機のUSB抜き差しや常時監視は未実施。
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
O_NONBLOCKはioctlの完了時間を保証しないので、現段階ではdaemonループに組み込まない。
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
NO_DISCへ戻ることを確認できた。一回実行の観測であり、自動挿入検出や
操作直後のNOT_READY遷移は未検証。音楽CDであることはユーザーの操作情報で、
診断コードによる判定ではない。
常時監視は未実装。TOCの一回実行診断は次節を参照。
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
PlayerStateや再生位置はまだ導入しない。

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
