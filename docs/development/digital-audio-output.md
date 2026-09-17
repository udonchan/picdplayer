# デジタルオーディオ出力の将来拡張

## 位置付け

この文書は将来のデジタル出力に関する設計候補である。S/PDIF、光出力、coaxial出力、
外部I²S接続を現在の必須機能や実装予定として扱わない。

現在の標準構成はHDMI audioである。Raspberry Pi 3からREGZAへHDMI接続し、TVのARC経由で
Marantz NR1200へ出力する構成を通常CDで実機確認している。現在の確認範囲は
[検証状況と残課題](../development/verification.md)を参照する。

現在の開発スコープは次に限る。

- HDMI audioによるCD再生の完成
- 再生経路の状態を正しく把握して公開すること
- resampling、DSPなどの有無を確認できる場合だけ正しく報告すること

USB S/PDIF、S/PDIF HAT、専用S/PDIF基板、TOSLINK、75 Ω coaxial、外部I²S公開は、
いずれも将来の任意拡張である。

## 現在の音声出力実装

CD-DA PCMは44.1 kHz、16 bit、2 channelである。PiCDPlayer内部では、CD-DA readerが
signed 16-bit host-endianのinterleaved stereo PCMを返し、`PcmWorker`と`PlaybackEngine`を経て
`AudioOutput`へ渡す。

```text
/dev/sr0
  -> CddaReader
  -> PcmWorker queue
  -> PlaybackEngine
  -> AudioOutput
  -> ALSA PCM device
  -> HDMI（現在の既定device）
```

`PlaybackEngine`は`AudioOutput`の`write`、`delay`、`reset`、`drain`だけを利用するため、
engine自身はHDMIを直接知ってはいない。一方で、現行daemonが生成する実装はALSAだけである。
`--audio-device`でALSA PCM名を指定でき、既定値は`plughw:CARD=vc4hdmi,DEV=0`である。

ALSA実装はnonblocking PCMを44.1 kHz、stereo、`SND_PCM_FORMAT_S16`、interleavedで開き、
`snd_pcm_set_params`のsoftware resample指定は無効にしている。PiCDPlayer自身にはvolume、EQ、
DSP、format converterはない。ただし、指定したALSA PCMが`plughw`や別のpluginを経由する場合、
device側に必要な形式変換が存在し得る。現在はALSAが最終的に選んだhardware format、mixer設定、
plugin chainをdaemonが検査・公開していない。

したがって、現在のHDMI再生を含め、PiCDPlayerはbit-perfectを主張しない。正常な試聴、
44.1 kHz指定、software resample無効だけでは、出力端子までの全経路を証明しない。

## 将来のソフトウェア構成

将来もplayer coreがHDMIを前提にしない構成が望ましい。

```text
CD-DA reader
  -> PCM queue / PlaybackEngine
  -> output backend
       +-> ALSA HDMI device
       +-> ALSA USB S/PDIF device
       +-> ALSA I²S S/PDIF device
```

現在は最終段のALSA deviceを`--audio-device`で選べるため、USB Audio Class機器や、
Device TreeによりALSA cardとして登録されたI²S S/PDIF機器は、対応formatが一致すれば
コード変更なしで試せる可能性がある。これは動作保証ではない。device名、対応format、
clocking、mixer/plugin経路、起動時のdevice出現は未確認である。

将来、ALSA以外の出力方式を追加する必要が出た場合は、既存の`AudioOutput`を出力境界として
検討する。現時点では複数backendのfactory、device discovery、format negotiation、
output profile、切替APIは実装していない。この文書のためにコード構造を変更しない。

## S/PDIF実現方式の候補

| 方式 | hardware complexity | physical size | software complexity | USB usage | GPIO / I²S usage | optical output | coaxial output | prototypeへの適性 | 一体型hardwareへの適性 |
|---|---|---|---|---|---|---|---|---|---|
| USB → S/PDIF | 低い。市販interfaceを接続 | 本体外付けになりやすい | 低い。ALSA deviceとして選択 | 使用する | 使用しない | 製品依存 | 製品依存 | 高い | 低い〜中程度 |
| I²S → S/PDIF HAT | 低い〜中程度。既製HATを装着 | HAT高さ・面積を要する | 中程度。overlayとALSA card設定を確認 | 使用しない | 使用する | 製品依存で提供例あり | 製品依存で提供例あり | 高い | 中程度 |
| I²S → 専用S/PDIF基板 | 高い。基板、電源、I/O設計が必要 | rear I/O board等へ小さく統合可能 | 中程度〜高い。対応driver/overlayが必要 | 使用しない | 使用する | 設計可能 | 設計可能 | 低い | 高い |

この表は品質や互換性の優劣を表さない。実際のsample rate、sample format、clocking、
receiverとの互換性は、選んだ機器と構成ごとに確認する。

### USB → S/PDIF

USB Audio Class対応の市販USB S/PDIF interfaceをALSA PCM deviceとして選ぶ方式である。
Raspberry Pi本体への変更がほとんど不要で、試作や利用者による追加に最も取り組みやすい。
例えば、対応していれば次のように既存の起動optionでdeviceを選べる。

```sh
./build-direct/cdplayerd --player /dev/sr0 --cdda-reader direct \
  --audio-device 'plughw:CARD=USB,DEV=0'
```

このdevice名は例であり、実在を前提にしない。USB portを一つ使い、外付け配線・電源・
筐体の見た目を考慮する必要がある。機器ごとの44.1 kHz対応、PCM format、coaxial/optical端子、
mixerまたはDSPの有無を実機で確認する。

### I²S → S/PDIF HAT

Raspberry PiのGPIO headerに接続するS/PDIF HATを使う方式である。HiFiBerry Digi系は候補例だが、
特定製品を設計要件にはしない。Raspberry Piのaudio boardにはGPIO/I²Sを使う既存例があり、
既製HATではLinux driverとDevice Tree overlayを利用できる構成が期待できる。

この方式はUSBを使わず、optical/coaxialを持つ既製品で出力形式を試しやすいため、
実装検証とプロトタイプに向く。一方でGPIO/I²Sを占有し、HATの面積と高さが専用機内の
スペース効率に適するとは限らない。利用するHATごとにoverlay、ALSA card名、
対応format、GPIO競合、boot時のcard出現を確認する。

### I²S → 専用S/PDIF基板

Raspberry PiのI²SをS/PDIF transmitterへ渡す小型の専用基板を作る方式である。WM8804のような
transmitterを候補例にできるが、特定ICを必須仕様にしない。

```text
Raspberry Pi
  | I²S
  v
S/PDIF transmitter
  +-- TOSLINK optical output
  |
  +-- pulse transformer -- 75 Ω coaxial output
```

rear I/O boardへ統合すれば、opticalとcoaxialを筐体に自然に収められ、USBを消費しない。
一方で、電源、clocking、信号配線、connector、EMI、製造試験を含む基板設計が必要になる。
Linux側では既存driverとDevice Tree overlayを使える構成を優先し、固有driverを追加する場合は
Buildroot移植まで含めて保守範囲を評価する。

coaxial出力は75 Ω伝送路として設計する。コネクタまでの配線、source/loadの扱い、return pathを
含めて評価し、詳細な回路定数をこの文書で固定しない。外部機器とのground loop低減と
galvanic isolationのため、S/PDIF用pulse transformerを用いる構成が一般的な候補である。
実際の絶縁、規格適合、信号品質は基板設計と測定で確認する。

## Audio integrityと状態表示

CD-DA sourceは44.1 kHz、16 bit、2 channel PCMである。しかし、S/PDIF端子を持つだけで
bit-perfectになるわけではない。bit-perfectを表示または主張するには、少なくとも各再生経路で
次を確認する必要がある。

- source format
- ALSA deviceと実際のhardware configuration
- sample format、sample rate、channel count
- software volumeとmixer controls
- DSP、format conversion、resampling
- ALSA plugin layerと外部出力機器までの経路

将来のUIやAPIは、確認できた事実を個別に表せる構造が望ましい。例えば次の表示は、各値を
取得・検証できる場合に限って使用する。

```text
DIGITAL OUTPUT: S/PDIF COAXIAL
SOURCE: CD-DA 44.1 kHz / 16 bit / 2 ch
OUTPUT: 44.1 kHz / 16 bit PCM
DSP: NONE
RESAMPLING: NONE
```

この表示自体はbit-perfectの証明ではない。値が取得できない場合はUNKNOWNとして扱い、
`BIT-PERFECT`と表示しない。CD読み取りのlocal verification、drive cache独立性、
原盤PCMとの一致、出力経路のbit一致は別の事実であり、現在の
[読み取り信頼性の拡張設計案](../development/integrity-design.md)の原則を維持する。

## 外部I²S公開

I²Sを外部DACなどへ直接接続したい利用者はあり得る。ただしI²SはS/PDIFのような
一般的な機器間デジタル接続規格として扱わない。logic-levelのclock/data interfaceであり、
board間接続を主眼とする。

外部へ公開する場合は、clockとdataの信号品質、配線長、電圧level、ground、connector、
接続先とのclock master/slave関係を利用者側を含めて設計する必要がある。PiCDPlayerが
外部I²Sを公式に提供するかは未定である。

## 将来の検討項目

S/PDIFを実装候補として採用する段階で、少なくとも次を決める。

- 対象hardwareとOS/Device Tree overlay、ALSA driverの対応範囲
- 44.1 kHz / 16 bit / stereoを含む実際のPCM capabilityと起動時の検出方法
- ALSA PCM名、mixer controls、volume/DSP/resamplingの状態取得方法
- HDMIと複数出力を同時に扱うか、単一出力を選択するか
- hotplug、起動時のdevice未出現、device消失時のPlayerStateとエラー表示
- coaxial/opticalの実測、電気設計、外部DACとの相互運用試験
- HDMI、USB、I²S出力それぞれについての試聴・format確認・異常系試験

その時点で実機試験を行い、確認済みの範囲を[検証状況と残課題](../development/verification.md)へ記録する。

## 参照

- [Raspberry Pi Audio documentation](https://www.raspberrypi.com/documentation/accessories/audio.html): GPIO audio boardとI²S利用pinの概要
- [Using the I²S peripherals on Raspberry Pi SBCs](https://pip-assets.raspberrypi.com/categories/1259-audio-camera-and-display/documents/RP-009699-WP-1-Using%20the%20I2S%20peripherals%20on%20Raspberry%20Pi%20SBCs.pdf): I²Sのclock/dataとDevice Tree構成の概要
- [Raspberry Pi configuration documentation](https://www.raspberrypi.com/documentation/computers/configuration.html): Device Tree parameter/overlayの概要
- [WM8804 datasheet](https://d3uzseaevmutz1.cloudfront.net/pubs/proDatasheet/WM8804_v4.5.pdf): S/PDIF transmitter候補例
