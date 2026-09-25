# 標準Player描画負荷の改善検証（Issue #53）

測定日: 2026-09-26。Pi 3、純正ケース、ファン・ヒートシンクなし、室温は前回のユーザー申告25.5°C。
今回の室温は再測定していない。TVは起動中、CD挿入済み、標準Playerは1920×1080。
同じPiでの[変更前基線](../2026-09-25-kiosk-baseline/README.md)と比較した。
変更前のinstalled packageは#43版、変更後は#53の同値DOM write削減と進行バーの
`width` transition削除を含むpackageである。両変更の個別寄与率はこの比較では分からない。

Macで`./scripts/build-container.sh`を実行してLinux/aarch64 packageを生成し、
Docker内CTest 32/32件成功後、`./scripts/deploy.sh`でPiへ導入した。
deploy前後はdaemon/kioskともinactiveを維持し、測定時だけ起動した。
`dpkg -V picdplayer`に差異はなかった。Piではコンパイルしていない。

30秒warm-up後、CDPを切断して[採取スクリプト](../../../../scripts/measure-kiosk.py)で
5秒間隔・各5分測定した。全core CPUは4 core合計に対する割合、process CPUは1 coreを100%とする。
温度75°C、現在のpower/thermal bit、期待したPlayer stateからの逸脱を中断条件とした。

| 条件 | 変更前: 全core CPU平均 / 最大 | 変更後: 全core CPU平均 / 最大 | 変更後: 温度開始→終了 / 最大 | 変更後: 現在throttling |
|---|---:|---:|---:|---:|
| Player STOPPED、CDPなし | 1.26% / 3.96%（5分） | 1.17% / 1.92%（5分） | 58.5→58.0 / 59.1°C | 0/59 |
| Player PLAYING、CDPなし | 70.92% / 71.88%（54.6秒） | 7.10% / 7.99%（5分） | 60.7→61.8 / 62.3°C | 0/59 |

変更後PLAYINGの最初の10 sample（51.4秒）はCPU平均7.03%、最大7.77%、温度最大61.2°C。
変更前のPLAYINGは温度上昇のため54.6秒で停止したので、長時間平均同士の比較ではない。
変更後はtrack 1から2へ進み、5分間全sampleでPLAYINGを確認した。`get_throttled`は全sampleで
0x60000であり、bit 17/18のboot以降の履歴だけが残り、現在のbitは0だった。
変更後PLAYINGのprocess CPU平均はrenderer 8.35%、GPU process 7.12%、browser 2.91%、
Cage 1.49%、daemon 4.44%（いずれも1 core=100%）。

同じUIの進行バーtransitionをCDPで一時停止した変更前の36秒区間はCPU平均10.79%だった。
今回の7.10%と近い桁であるため、CSS transitionが主な負荷源だったという仮説と整合する。
ただし一時停止実験は単回・短時間・測定順固定であり、DOM最適化とCSS変更の寄与率や
室温・cache等の影響を厳密には分離していない。

## 短時間のCDP描画指標

5分の無接続測定後、MacからSSH port forwarding経由でCDPへ接続した。
10秒のPerformance metricsと続く6秒のtimeline traceを取得した。
`window.render`の一時wrapperとMutationObserverも挿入しているため、無接続のCPU値とは別条件。

| 指標 | 変更前 | 変更後 |
|---|---:|---:|
| WebSocket受信 / 10秒 | 39 | 40 |
| `render()`呼出 / 10秒 | 39 | 40 |
| DOM mutation record / 10秒 | 429 | 50 |
| LayoutCount差 / 10秒 | 498 | 40 |
| RecalcStyleCount差 / 10秒 | 499 | 40 |
| LayoutDuration差 / 10秒 | 1.56秒 | 0.076秒 |
| TaskDuration差 / 10秒 | 5.80秒 | 0.391秒 |
| trace Paint event / 続く6秒 | 574 | 48 |
| trace Layout event / 続く6秒 | 296 | 24 |

WebSocket受信や`render()`は減らさず、DOM/CSSの表示コストが下がった。
trace eventはHDMIの実pixelや視覚上のframe数ではない。CDP screenshotではcover art、曲情報、
再生位置、PLAYINGとLive表示を確認した。screenshotは第三者のcover artを含むため同梱しない。
TVの肉眼確認・音の聴感確認はこのrunでは行っていない。

測定時間帯のservice journalにALSA underrun、CDDA read error、slow_stage、failed/error、
kernel journalに新たなUSB over-current/disconnect、thermal/throttling警告は見つからなかった。
これは検索したログの範囲であり、無故障保証ではない。測定後はkiosk→daemonの順に停止し、
両serviceがinactive、温度60.7°Cであることを確認した。

## 生データと残る条件

`player-stopped-no-cdp.jsonl.gz`、`player-playing-no-cdp.jsonl.gz`、
`cdp-playing.jsonl.gz`をこのdirectoryへ保存する。`gzip -dc`で確認できる。
CPU/process/thread、RSS、memory/swap、frequency、温度とthrottlingの各sampleがrawにある。
URL、album名、API key、full process argsは記録しない。screenshotも同梱しない。

変更前PLAYINGは高温で長期測定できず、厳密な同時間・同室温の反復比較ではない。
Cage単独条件、cold boot後の長期安定性、TVでの肉眼・音声確認、異なるdiscやUI設定での再現性は
未確認。別runtimeへ移行する判断はこの結果からは不要だが、今後また持続的な高CPUや
現在のthrottlingが実測された場合は、同条件のプロファイル後に検討する。
