# 装置と接続

現在の実機構成はRaspberry Pi 3 Model B、USB光学drive、REGZA TV、Marantz NR1200である。
USBからCDのPCMを取得し、PiのHDMIからTVへ、TVのARCからアンプへ音声を渡す。

```text
光学drive -- USB / CD-DA --> Pi -- HDMI / PCM --> REGZA -- ARC --> NR1200
                              <-- HDMI / CEC -- REGZAリモコン操作
```

音声を運ぶ経路とCECによる制御は役割が異なる。同じHDMIケーブルを使っていても、
CEC登録ができたことだけではPCM出力の正常性は分からない。
この環境ではPiをCEC Playback Deviceとして登録するとARCが復帰した。
これは実機で確認した相互動作であり、すべてのTVの性質として一般化しない。

USB機器の名前と実際の製品名が異なって見える場合もある。初期調査ではUSB IDの名称だけで
driveを断定せず、sysfsや筐体情報と照合した。driveを見つけること、discがあること、
Audio CDであることも、それぞれ別の確認である。

将来はUSB S/PDIF、I²S接続のS/PDIF HAT、専用基板から外部DACへ出す構成も考えられる。
HDMIは引き続き標準構成で、S/PDIFは実装予定を確定していない設計候補である。
外部I²Sはclock/dataを直接扱うため、TOSLINKやcoaxialと同じ感覚で接続できる端子とはしない。

接続候補の比較と制約は[デジタル出力の拡張案](../development/digital-audio-output.md)、
CEC/ARCの初期実測は[第1段階の記録](../history/milestone-1.md)を参照する。
Pi固有の既定device名やGPIO/Device Treeの設定は、再生状態のモデルへ持ち込まない。

前：[物理CDを使う家電として](introduction.md) / 次：[状態を持つdaemon](architecture.md)
