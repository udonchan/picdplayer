# Disc map通常系確認

2026-09-26、feature/99-disc-read-mapの作業ツリーをDocker build/package後にPiへ導入。
既存CD、kiosk停止、API play/stopとdaemon restartで確認。
[結果](results.json)はAPIから必要fieldを抜粋したもの。140 read超の保持、stop/resume、
新sessionでのresetを機械的に検査。両サービスを停止して終了した。
実際の可聴性・TV表示・異常媒体・物理交換は確認していない。

実機確認後の最終レビューで、例外時revisionの上限処理のみ補強した。
最終版はDocker36/36を再確認したが、この上限分岐をPiで再現したものではない。
