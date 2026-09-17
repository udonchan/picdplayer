# Guide：PiCDPlayerを理解する

現在動作するプレイヤーを題材に、目的から音声・状態・表示情報へ順番に読む。
将来候補はその旨を示し、製品としてすべて完成したとは扱わない。

1. [物理CDを使う家電として](introduction.md)：何を作り、何を優先するか。
2. [装置と接続](hardware.md)：音声とリモコン信号が通る経路。
3. [状態を持つdaemon](architecture.md)：mainとworker、独立した状態の理由。
4. [CD-DAが音になるまで](playback.md)：読み取り、先読み、出力、再生位置。
5. [読み取り結果について言えること](integrity.md)：観測、一致、不確実性。
6. [TOCから曲名と画像へ](metadata.md)：再生を待たせない追加情報。

現在の仕様は[設計書](../README.md)、試験の到達点は[検証状況](../development/verification.md)、
判断の経緯は[History](../history/README.md)を参照する。
