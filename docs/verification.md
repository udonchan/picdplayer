# 検証状況と残課題

記録日: 2026-09-17。実装済み、hardware非依存試験済み、実機確認済みを区別する。
今回の文書整理では追加の実機操作を行っていない。

## 自動試験

CTestはCMakeの有効機能で件数が変わる。基本buildではcontroller、engine、ALSA抽象、CEC変換、
media tracker/worker、TOC、reader、PCM保存、snapshot、CLIなどを検証する。
metadata有効時はDisc ID公式vector、候補0/1/複数と不正JSON、worker/sessionの旧結果破棄を追加する。
API有効時はJSON schema、route、loopback socket、無通信時のserviceがmainへ戻る回帰試験を追加する。
paranoiaにはlibrary呼び出しをwrapした試験がある。

```sh
cmake --build build-direct -j1
ctest --test-dir build-direct --output-on-failure
cmake --build build-metadata -j1
ctest --test-dir build-metadata --output-on-failure
```

前回コミット時にdirectの13/13成功を確認済み。
metadata buildは前回レビューでAPI socket試験を除く18件成功の記録がある。
これは全option組合せの保証ではない。socket試験はloopback通信を許可した環境で実行する。
テスト名・登録条件の正規情報は[CMakeLists.txt](../CMakeLists.txt)にある。

## 実機確認済み

| 対象 | 確認範囲 |
|---|---|
| CEC | Playback登録、REGZA/MarantzのARC復帰、remote操作、power/active source応答 |
| drive/TOC | ASUS SDRW-08D2S-U、tray開・空・Audio CD・取り出し、14曲TOC |
| PCM | direct/paranoiaの保存PCM正常再生。条件を揃えた性能比較は未実施 |
| native再生 | HDMI出力、pause/再開、曲移動、曲境界seek、stopで先頭へ戻る |
| lifecycle | 空起動、挿入、自動TOC、取り出し・再挿入後の再生 |
| systemd | 自動起動からCEC再生。metadata有効版のservice起動・cache hit・play/pause |
| metadata | 14曲Disc ID、候補1件、AVAILABLE、cache miss/hit、並行CEC処理 |
| API | Macから外部GETによる状態照会、eject要求とトレイ動作 |
| eject待受修正 | 2026-09-17に一回の要求でトレイが開いたとのユーザー確認 |

14曲CDのleadout LBAは242334、Disc IDは6JTbUgqHL29gzUyOH5ir60K3hz0-。
数値はこの試験discの結果であり、実装の固定値ではない。

## 次の確認と残課題

- [読み取り信頼性の拡張設計案](integrity-design.md)をレビュー後、Phase 1aの観測モデルから追加する。
  同文書の試験計画は未実装であり、現行CTestの検証済み範囲には含めない。
- metadata/API有効の最新service構成で再起動から再生・API操作まで確認する。
- LOADING中・PLAYING中のeject、重複要求、EJECT_ERROR、終了との競合を実機で継続確認する。
- 傷disc・USB reset・4秒超read stallでunderrun復旧、音の欠落/重複、操作遅延を評価する。
  正常試聴では異常を再現できておらず、復旧経路の実機確認は未完了。
- direct/paranoiaの採用、性能、CPU負荷、startup/seek latencyは実測後に判断する。
- pause再開の待ち時間、buffering表示、復旧回数上限を検討する。
- mediaとPCMのdevice access完全直列化、同じTOCの別disc識別、LOADING後のmetadata再要求を検討する。
- metadata lookup中交換、network切断、複数候補、CAA失敗・redirect修正後の取得を実機確認する。
- cache期限/総容量/破損復旧、候補選択、非1始まりtrack対応、HTTP/JSON制限の強化は未実装。
- CEC device消失後の再open、claim timeout、専有制御を検討する。
- UI・画像binary取得・quiet boot・read-only root・Buildroot imageは未実装。

Piハング時は原因を確定できる前bootログがなかった。メモリ圧迫とswap I/Oは候補であり確定原因ではない。
ビルドは-j1を維持する。障害調査と実機結果の原記録は[履歴](history/README.md)に保存する。
