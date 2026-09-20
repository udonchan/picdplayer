# ドキュメントの案内

PiCDPlayerのドキュメントは日本語を正文とする。目的に合わせて次の入口を選ぶ。

| 読みたいこと | 入口 |
|---|---|
| 仕組みと設計意図を理解したい | [Guide：はじめから読む](guide/README.md) |
| 現在の仕様を確認・変更したい | [基本設計](design/basic-design.md) → [機能設計](design/functional-design.md) → [詳細設計](design/detailed-design.md) |
| ビルド・運用したい | [ビルド](manual/build.md)、[操作・診断](manual/operations.md)、[systemd](manual/systemd.md) |
| 画面をカスタマイズしたい | [Custom UI](manual/custom-ui.md)、[将来の設定基盤](development/user-configuration.md) |
| 実装・検証状況と残課題を知りたい | [検証状況](development/verification.md) |
| 将来の設計候補を調べたい | [読み取り信頼性の拡張案](development/integrity-design.md)、[デジタル出力の拡張案](development/digital-audio-output.md) |
| 設計が成立した過程を知りたい | [History：開発の流れ](history/README.md) |

Guideは概念と設計意図、Designは現在の仕様、Manualは手順、Developmentは開発状態と未実装案、
Historyは当時の判断・試験を扱う。実装済みと実機確認済みは別の状態として記録する。

要求・責務は基本設計、機能/API契約は機能設計、処理順序・所有権は詳細設計を正とする。
宣言そのものはソースのヘッダーを参照する。実装変更時は該当設計と検証状況を更新し、
Guideや履歴へ仕様表を複製しない。旧パスの短い移転案内は、過去のリンクを維持するために残す。

検証記録は現在、一つの文書内で到達点と条件付きの実験結果を辿れる規模に留まる。
今後、driveやdiscごとの比較が増えて現在の確認状況を探しにくくなったら、Development内の
`verification.md`を検証マトリクスと未確認事項の入口とし、個別結果を`reports/`へ分ける。
その際は実験条件・観測・限界を一つのreportに残して入口からリンクし、数値の二重管理を避ける。
現時点ではファイル移動や新しいdirectoryの追加は行わない。
