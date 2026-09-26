# #24着手前の監査（2026-09-26）

## 対象と発見

2026-09-25〜26のcommit履歴を入口に、#98/#99、診断世代・履歴・API、
標準UI、計測・配布手順と関連文書を確認した。監査前HEADは`9847027`。
全ハードウェア条件の保証ではなく、具体的な境界条件を自動試験へ追加する監査である。

| Issue | 再現と修正 |
|---|---|
| [#103](https://github.com/udonchan/picdplayer/issues/103) | 出力失敗後に旧current_playbackが残る。cancel/discard後に旧dropped_eventsが残る。stream終了で両者を破棄する。disc_mapは保持する。 |
| [#104](https://github.com/udonchan/picdplayer/issues/104) | 再接続中のdisc交換でnull coverを見逃すと、同じcover URLの旧画像が残る。session/disc世代を画像identityへ含める。 |
| [#105](https://github.com/udonchan/picdplayer/issues/105) | 最終throttled sampleだけ取得不能だと集計でTypeError。最終値のnullを保持する。 |
| [#106](https://github.com/udonchan/picdplayer/issues/106) | 終端underrunがend-1へ復帰し続ける。終端はエラー停止し、drain開始後の追加delay照会は行わない。 |

## 終端試験の失敗と中止

監査中、未コミットの「drain中にもdelayを照会して位置を更新する」変更をPiへ導入した。
最終trackを選択し末尾約4秒へseekして完走を待ったが、20秒の待機がtimeoutした。
[ログ抜粋](terminal-recovery-excerpt.log)のとおり、LBA 242333（leadout 242334）への
`ALSA delay: underrun`復帰が146回発生した。最初12:34:24.934Z、最後12:34:40.950Z。
正常なseek動作ではなく、実機試験失敗として扱う。サービス停止後は追加の再生試験を行わない。

fake outputでdrain開始後のdelayを利用不能にすると同じend-1復帰を再現できた。
当初のfakeがdrain後もdelay成功を仮定しており、この状態遷移を検出できなかった。
追加delay照会を取り下げ、drain中または全PCM提出後のunderrunをエラー停止に変更した。
これは正常完走の偽装ではなく、音声出力エラーとして上位へ伝える。
一般的な途中復旧の回数上限は#34の範囲であり、今回解決したとはしない。

## 自動検証

修正前にstale evidence、drop count持越し、同URL画像更新漏れ、null集計失敗、
drain後delay失敗による再seekを再現。修正後、既存Docker Linux/aarch64手順で
build・stage・Debian package生成が成功し、CTest **37/37**成功（8.14秒）。
終端drain失敗、最終write直後のdelay失敗、正常drain完了、途中underrun復帰を区別して検証した。
同discのcancel中in-flight readはdisc_mapへ保持し、disc世代変更後の旧readは除外する試験も追加した。
NodeのUI試験とPython集計試験はCTestに含む。

## 限界

修正後のPi再生・試聴・完走は未確認。修正版は`scripts/deploy.sh`で導入成功し、
daemon/kioskとも`inactive → inactive`を確認した。package導入は動作検証と区別する。
CDP試験は遷移前のabout:blankを選んだためAPI取得に失敗しており、画像更新を実機確認した証拠にはしない。
旧正常disc_map試験の結果は[元の記録](../2026-09-26-disc-map/README.md)に残し、今回の失敗と混同しない。
傷disc・特殊TOC・物理交換・長期負荷は既存#33/#39/#40/#96/#4/#83の範囲を維持する。
