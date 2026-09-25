# ドキュメントの案内

PiCDPlayerのドキュメントは日本語を正文とする。目的に合わせて次の入口を選ぶ。

| 読みたいこと | 入口 |
|---|---|
| 仕組みと設計意図を理解したい | [Guide：はじめから読む](guide/README.md) |
| 現在の仕様を確認・変更したい | [基本設計](design/basic-design.md) → [機能設計](design/functional-design.md) → [詳細設計](design/detailed-design.md) |
| Mac + Dockerで開発したい | [Mac + Docker開発手順](manual/mac-docker-development.md) |
| ビルド・運用したい | [ビルド](manual/build.md)、[操作・診断](manual/operations.md)、[systemd](manual/systemd.md) |
| 画面をカスタマイズしたい | [Custom UI](manual/custom-ui.md)、[将来の設定基盤](development/user-configuration.md) |
| 実装・検証状況と残課題を知りたい | [検証状況](development/verification.md) |
| 読み取り信頼性の要求と実装境界を確認したい | [読み取り信頼性の仕様](design/integrity-design.md) |
| 次に取り組む作業を選びたい | [残課題とIssue一覧](development/backlog.md) |
| 将来の設計候補を調べたい | [デジタル出力の拡張案](development/digital-audio-output.md)、[設定基盤の拡張案](development/user-configuration.md) |
| 設計が成立した過程を知りたい | [History：開発の流れ](history/README.md) |

Guideは概念と設計意図、Designは現行仕様と明示的に区別した未実装の要求、Manualは手順、
Developmentは検証状態・Issueへの入口と採用未決定の設計候補、
Historyは当時の判断・試験を扱う。実装済みと実機確認済みは別の状態として記録する。

要求・責務は基本設計、機能/API契約は機能設計、処理順序・所有権は詳細設計を正とする。
読み取り信頼性の横断的な要求はintegrity仕様に集約し、現在の公開API/型と将来の目標モデルを区別する。
宣言そのものはソースのヘッダーを参照する。実装変更時は該当設計と検証状況を更新し、
Guideや履歴へ仕様表を複製しない。

`development/verification.md`は検証状況と個別実験への入口とし、測定条件・生データ・限界を
伴う詳しい結果は`development/reports/`に置く。現在のkiosk性能測定がこの形式を用いる。
仕様値はDesignを正とし、測定値を現在の保証値として扱わない。
