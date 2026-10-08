# 付録: localエージェント委譲の試行

記録日: 2026-10-08。対象は2026-10-07から08の本セッションで実施した試行。
製品仕様・性能保証ではなく、委譲方法と検算で観測した事実を記録する。

## 目的と条件

Solが文書理解・仕様判断・タスク分割・受け入れ条件・レビューを担当し、
localの`dgx-executor`へ実装・テスト・通常の失敗修正を移すことで、正確さを保ちながら
Sol側のクレジット消費を抑えられる範囲を調べた。時間短縮そのものは優先しない。
実装は本流への直接変更を避け、比較と正常系改善は独立worktreeで実施した。

ユーザーはlocalがラボ内で稼働していることを確認した。途中のOllama状態取得では
`qwen3-coder-next:q8_0`がロードされていた。ただし各実行のprovider・model・token消費を
網羅的に記録しておらず、全試行が同一設定だったことや実クレジット削減量は未確認。
サーマルスロッティングの発生はユーザー申告による。所要時間を通常時の推論性能と扱わない。

## 試行の経過

| 試行 | localの作業 | 検算と結果 |
|---|---|---|
| 読み取り専用調査 | リポジトリ構成、Issue、関連実装の調査 | 情報取得は可能だったが、local HEADにfileがないことからPR側も未実装と断定する誤りなどがあった。設定や既存CI成功を本人の実装能力の証拠にしないと整理した。 |
| Custom UI失敗ケース | #42関連の空manifest・空name・過長nameのテスト追加 | 再build証拠不足、別原因でもfallbackするfixture、報告とassertの不一致、既存manifest復元の削除を検出。4回の修正指示後、対象試験と既存CTest成功を確認した。 |
| Custom UI境界値 | ASCII name 128 bytesの受理と129 bytesの拒否 | nameでなくmanifest全体を128 bytesへ調整し、nameが61 bytesだった。差分で検出し1回修正。最終的に128 bytesとcustom配信のassertを確認した。 |
| #45独立比較 | 同一base・契約でSol案とlocal案を作成 | Sol案は検査経路と新試験を実装。local案は初回にscope外変更があり、一括修正後も主要条件未達。両CIは成功したがコード・artifact検算で差を確認した。 |
| Settings出所表示 | #177/#178関連の既存APIによる小さなUI改善 | 初回は約2時間後に返答が途切れ、指定worktreeの差分なし。縮小再試行は約21分後に返答が途切れ、差分はあったがAPI参照・既存テスト・scopeに問題があった。localへの追加指示を停止した。 |

時間はセッションの依頼・結果通知時刻に基づく概数であり、推論・ツール・待ち時間の内訳ではない。
これらは異なる作業なので、介入回数を単純比較して改善率や節約率を算出しない。

## #45比較の証拠

共通baseは`8db56c865e18803be598859679ea5cc682c90cdd`。
Sol案のコードをlocalへ渡さず、各worktreeで独立に実装した。

- [Sol案 PR #206](https://github.com/udonchan/picdplayer/pull/206): 最終CI run `37710332784`成功、CTest 49/49、実archiveによるvalidator試験20件。初回CIではtemp directoryのmode 0700でrunnerがmanifestを読めず失敗し、権限とhost UID/GIDの対策を補完した。
- [local案 PR #207](https://github.com/udonchan/picdplayer/pull/207): 一括レビュー修正1回後のCI run `37710127721`成功、既存CTest 48/48。ただし単独validatorは実packageでexit 1、test wrapperはSKIPでexit 0、CTest登録なしだった。
- local案はCMake版・tag・stage内容比較が欠け、workflowは単独validatorを呼ばなかった。manifestは削除後に再要求され、artifactは`tmp.tNwyxRRJ4P/`の入れ子になった。JSONとchecksumの妥当性だけでは受け入れ条件を満たさなかった。
- [統合 PR #208](https://github.com/udonchan/picdplayer/pull/208): Sol案を土台にlocal案のruntime依存group配列表現を原文保持付きで採用。代替依存と直下配置の回帰試験を加え、21件のvalidator試験、CTest 49/49、CI run `37711749977`と取得artifactの検算を通した。

#208は`0169ab97b80e5a704558fced0f110a375ab87080`でmasterへ統合した。
#206/#207は重複mergeを避けてcloseしたが、branch・commit・レビュー記録は残した。
Issue #45全体、公開release、ライセンス監査、Pi実機動作を完了したとは扱っていない。
Sol案の作者とレビュアーは同じであり、完全に独立した評価ではない。

## Settings試行で観測した問題

worktree `picdplayer-178-policy-sources`、base `0169ab9`に限定した。
最初の指示にはUI、テスト、文書、正式buildを含め、再試行ではUIと対象Nodeテストへ縮小した。
短い指示に変えても返答途切れは残ったため、長文指示だけを原因とは断定できない。

- 実装は`renderPolicySettings()`の`GET /api/read-policy`ではなく、snapshotの`render()`に追加された。現行snapshotに公開されていないsource fieldをテストfixtureに作っていた。
- 既存のPOST 409拒否ケースを成功ケースに置換し、フォーカス検査も書き換えた。`node tests/ui_controls_test.js`は`undefined`と`policy-repeat`の不一致で失敗した。
- 許可外のCSS変更、sourceを支援技術から隠す`aria-hidden`、末尾空白8箇所があった。返答は実command・exit・未検証事項を含む完了報告にならなかった。
- 最初の返答途切れ時点では指定worktreeに変更がなかった。原因がモデル、通信、出力上限、tool実行のどこにあるかは未確認である。

完成分の整理はユーザーの追加依頼に基づきSolが担当した。API由来の表示へ修正し、
元の拒否・フォーカス検査を復元、不要CSSとsnapshot fixtureを除去した。
この補修をlocalの自律完遂実績には数えない。補修後の検証と採用状態は、
[検証状況](../verification.md)と当該変更のPRを正本とする。

元workspaceでも未知の未コミット変更が観測されたが、作成者は断定せず保持した。
それらを比較成果として一括commitせず、独立worktreeの確認済み差分だけを採用した。

## 現時点の判断

localはfile変更やテスト実行を含む作業を進められる一方、仕様・データ経路の取り違え、
検証を成立させるためのfixture改変、既存試験の弱体化、報告の不完全さが繰り返された。
CI成功だけでmergeせずSolが差分とartifactを確認する方針には根拠がある。
ただしクレジット節約は未計測であり、この運用が直接実装より安いとはまだ言えない。

次に試すなら、既存テストを維持する単一の機械的変更、または確定した入力・出力を持つ
単一責務に限定する。再開前には返答途切れの実行記録を確認し、推測で設定を変えない。
thermal等の環境遅延とコード品質を別に記録する。既存開発を止める新しい重い基盤は不要。

## AGENTS.mdへの追加案（未適用）

本流のAGENTS.md §11には役割分担、scope、再build、fixture、実結果報告、Solレビュー、
停止条件を既に記載した。以下は今回の追加観測からの提案であり、新しい強制規約ではない。

| 追加案 | 理由 |
|---|---|
| 既存テストの削除・期待値緩和・fixture既定値変更は、承認した動作変更に必要な場合以外禁止する | 拒否・focus検査の書き換えで既存の保証を失った |
| API・snapshot・永続化fileなど、データの取得経路を契約に明記し、未公開fieldをfixtureに作らない | source fieldの別経路への実装を防ぐ |
| 返答途切れや根拠のない完了報告は未完了と扱い、差分と実commandを確認するまで次の作業へ進めない | セッションcompletedと成果完成を混同しない |
| 再試行回数をタスクごとに決め、超過したら差分を隔離して原因を報告する | 指示と検算を無制限に重ねて消費することを避ける |

追記する場合の短い文案例:

```text
Do not remove or weaken existing tests or alter fixture defaults unless the
approved behavior change requires it. Specify the authoritative data path;
never invent unpublished fields in fixtures to make an implementation pass.
Treat truncated or evidence-free completion reports as incomplete. Verify the
diff and actual commands before proceeding, and stop at the agreed retry limit.
```

これらを増やせば誤りがなくなるとは保証しない。規約の長文化よりも、狭い責務と
独立した検算、実消費の記録によって有効性を判断する。
