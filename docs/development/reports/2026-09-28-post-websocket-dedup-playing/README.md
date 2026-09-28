# WebSocket重複送信修正候補後の5分PLAYING測定（#83 / #121）

## 目的

Issue #122 の修正候補を導入したPiで、通常Audio CDの再生中に、CDPを接続せず5分間の
定常負荷と温度を記録した。あわせて、過去に記録されたdaemonのmain loop stage warningが
この条件で再現するかを確認した。この測定はIssue #121を解決するものではない。

## 条件

- 2026-09-28、Pi 3、純正ケース・追加冷却なし、室温約25.5°C、TVは通電中。
- PR #123の未マージcommit `a9a5aab` から生成したpackageを導入した。これは測定時点の
  `master`ではない。
- ASUS SDRW-08D2S-U（firmware F601）、通常の14 track Audio CD、direct `SINGLE` reader、
  drive speed request 1x。
- loopback APIで再生を開始し、30秒warm-up後に5秒間隔を目標として300秒収集した。
- CDP、画面キャプチャ、traceは接続しなかった。終了trapでAPIのstopを送った。

raw data: [`playing-no-cdp.jsonl.gz`](playing-no-cdp.jsonl.gz)

## 結果

JSON Linesは58 sampleだが、各sampleの実時間差の合計は300.0秒である。間隔は厳密に5秒ではない。

| 指標 | 結果 |
|---|---:|
| system CPU（全core）平均 / 最大 | 13.27% / 16.66% |
| 温度（開始 / 終了 / 最大） | 64.5 / 66.6 / 66.6°C |
| 現在のthrottling | 0 |
| throttling履歴 | `0x60000` |
| CPU周波数範囲 | 600000–1200000 Hz |
| 最小MemAvailable | 371360 KiB |
| swap in / out差分 | 68 / 0 |
| `cdplayerd` CPU平均 / 最大 | 8.76% / 9.45% |
| Chromium renderer CPU平均 / 最大 | 19.77% / 26.59% |
| Cage CPU平均 / 最大 | 1.38% / 2.12% |
| GPU process CPU平均 / 最大 | 11.19% / 15.63% |

測定区間のjournalには`main_loop_stall`、ALSA underrun、CDDA read error、thermal event、USB
over-current、drive disconnectを検出しなかった。`0x60000`は過去のthrottling履歴であり、現在の
throttlingではない。

## 解釈と限界

このrunでは#121が追跡するstage warningを再現しなかった。直前に行った短い通常CDのAPI操作列
（play/pause/seek/resume/stop）とdaemon restart後の30秒観測でも再現していない。一方、過去の
`control`、`api_service`、`cec_update`のwarningには異なる条件があり、CEC操作もこのrunでは
試していない。従って原因の特定や#121の完了判定には用いない。

過去の測定とはrevision・条件が揃っていないため、この値を#122によるCPU改善率として
比較しない。TVのスクロールバーは同日に目視されたが、DOM overflowの有無だけでは原因を特定できず、
別Issue #124で追跡する。この記録はTV実表示・試聴の確認を含まない。

## 再集計

```sh
python3 scripts/summarize-kiosk.py \
  docs/development/reports/2026-09-28-post-websocket-dedup-playing/playing-no-cdp.jsonl.gz
```
