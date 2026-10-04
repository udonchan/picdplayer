# 現行masterの短時間kiosk基線（Issue #83）

測定日: 2026-09-28。#83の残測定のうち、現行masterでの標準PlayerのCDP未接続基線を取得した。
これは#52の旧版基線を置換せず、診断情報配信を復元した後の短時間の追加観測である。

## 条件

- revision: `a496372368c6a069d7f7ba7c7814cae07605c215`（PR #120 merge）
- package: `picdplayer 0.1.0 arm64`。標準の`ENABLE_PARANOIA=OFF` packageを導入した。
- target: Raspberry Pi 3、Debian GNU/Linux 13 (trixie) arm64、kernel `6.18.50+rpt-rpi-v8`。
- drive/disc: ASUS SDRW-08D2S-U / F601、通常の14 track Audio CD、direct single reader、要求速度1x。
- kiosk: daemonとkiosk serviceを起動し、標準Playerを通常のTV出力で稼働した。CDPは接続しない。
- sampler: [`scripts/measure-kiosk.py`](../../../../scripts/measure-kiosk.py) を5秒間隔で60秒実行した。`--max-temp 75`、想定player stateを指定し、現在のpower/thermal low bitで停止する。
- 室温、TV/HDMIの表示状態、解像度、ネットワーク・metadata cacheの状態はこのrunで再記録していない。2026-09-25〜26の測定条件と同一とは主張しない。

`get_throttled`は全sampleで`0x60000`だった。これは過去に発生したtemperature/frequency-capの履歴bitであり、現在の制限を示すlow bitは全sampleで0だった。

## 結果

| 条件 | sample / 時間 | 全core CPU平均 / 最大 | 温度開始→終了 / 最大 | 現在throttling |
|---|---:|---:|---:|---:|
| STOPPED、CDPなし | 12 / 60.0秒 | 8.39% / 9.09% | 65.5→65.5 / 65.5°C | 0/12 |
| PLAYING、CDPなし | 12 / 60.0秒 | 19.33% / 21.56% | 68.8→69.3 / 69.3°C | 0/12 |

process CPUは1 coreを100%とする。主な平均値はSTOPPEDでrenderer 15.21%、Chromium utility 9.14%、daemon 4.73%、PLAYINGでrenderer 32.43%、GPU process 11.27%、utility 11.23%、daemon 10.81%、Cage 1.58%だった。Chromium processのRSSはshared pageを含むため、単純合計を実メモリ消費量とは扱わない。swap outは両条件0、PLAYINGのswap in差分は1 pageだった。

PLAYING測定はAPIから開始し、20秒warm-up後に採取した。独立したPi側プロセスで採取し、終了時のtrapが`POST /api/stop`を送った。終了後にAPIがSTOPPED/AUDIO_READY、両systemd serviceがactiveであることを確認した。測定中に75°C到達、現在のpower/thermal制限、再生state逸脱はなかった。

測定時刻帯を含むboot journalをALSA underrun、CDDA read error、thermal、USB over-current/disconnectで検索したが、該当する新規行は見つからなかった。`main_loop_stall stage=control duration_us=95365`が00:34に一件あったが、この60秒測定より前であり、本測定中の性能値へ含めない。別実行の74.989ms観測と合わせ、原因と再現性は[#121](https://github.com/udonchan/picdplayer/issues/121)で追跡する。

## CDPで見つかったWebSocket重複送信と修正（#122）

上記の無接続基線とは別条件で、MacからSSH port forwarding経由のCDPを10秒接続し、`Network`、`Performance`、一時的な`render()` wrapperと`MutationObserver`を用いて測定した。CDP自体の負荷を含むため、CPU基線やHDMI scanoutと同一視しない。

| 条件 | WS frame / text payload | `render()` / DOM mutation | Layout / Paint |
|---|---:|---:|---:|
| 修正前 STOPPED | 477 / 2,155,563 byte | 480 / 0 | 0 / 0 |
| 修正後 STOPPED | 0 / 0 byte | 0 / 0 | 0 / 0 |
| 修正後 PLAYING | 40 / 758,472 byte | 40 / 379 | 40 / 40 |

修正前もREST stateのrevisionは100ms間隔12回で不変だった。原因はdaemonのpublishではなく、API serverが同じ接続の`LWS_CALLBACK_SERVER_WRITEABLE`ごとに送信済みstateを確認せず同一snapshotを書き出せたことだった。接続ごとに送信済みgenerationを保持し、初回と内容が変化したstateだけを送るよう修正した。PLAYINGの40 frame/10秒は250ms周期の状態更新と整合する。終了後はAPIがSTOPPED/AUDIO_READY、温度65.5°C、current throttlingなしであることを確認した。

payload byte数はCDPが報告する受信text payloadをUTF-8で数えた値であり、WebSocket framing、CDP通信、画面への転送量は含まない。Network domain有効化時の過去eventを測定窓へ含めないよう、baseline metrics応答後にcounterをresetする。集計値は`cdp-websocket-dedup.jsonl`に保存する。

## 解釈と限界

- 現行masterの通常CDの短時間PLAYINGは、#52で記録した旧版の高負荷状態とは異なるCPU・温度範囲だった。ただしrevision、開始温度、表示条件、測定順、計測期間がそろわないため、改善率や一般的な性能保証にはしない。
- sampler自身の負荷を含む。CDP未接続値なので、CDP screenshot/trace/WS測定の負荷を含まない。
- 最初にSSHの実行時間制約で15.6秒・3 sampleのPLAYING測定が中断した。この不完全rawは保存せず、独立プロセスで完走した上表の12 sampleだけを本記録の根拠にする。
- 60秒は長期定常・耐久・TVの肉眼表示・音声品質を評価しない。通常系の継続運転と実表示・試聴は#184の範囲である。
- Cage単独、修正版CDPのWS/render/trace、payload頻度、sampler/CDPの観測負荷、順序を入れ替えた反復比較は#83に残る。

## 生データと再集計

- `stopped-no-cdp.jsonl.gz`
- `playing-no-cdp.jsonl.gz`
- `cdp-websocket-dedup.jsonl`

sampler rawは時刻、CPU/process/thread、RSS、memory/swap、周波数、温度、throttling、想定player stateを含む。CDP rawはURL、album artwork、API key、full process argsを含めない。展開後に次でsamplerの表を再計算できる。

```sh
gzip -dc stopped-no-cdp.jsonl.gz > /tmp/stopped.jsonl
gzip -dc playing-no-cdp.jsonl.gz > /tmp/playing.jsonl
python3 scripts/summarize-kiosk.py /tmp/stopped.jsonl /tmp/playing.jsonl
```
