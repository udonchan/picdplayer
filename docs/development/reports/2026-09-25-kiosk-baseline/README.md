# Kiosk CPU・描画・温度の基準測定（Issue #52）

測定日: 2026-09-25〜26。Raspberry Pi 3、Debian Trixie arm64、kernel 6.18.50+rpt-rpi-v8。
Piは純正ケース、ファン・ヒートシンクなし。室温はユーザー申告25.5°C、TVは通電・起動中。
CDは挿入済みで、STOPPEDではAPIがAUDIO_READYを返した。標準Playerを1920×1080で表示し、
CDP screenshotでcover art、曲情報、STOPPED、Live接続を確認した。
電源アダプタの定格、測定中の室温推移、network・metadata cacheの詳細は未記録。

標準packageは#43の修正版を使用し、#53のDOM更新変更はPiへ導入していない。
#53の負荷改善に関するbefore/afterはこのrunでは判定できない。

[計測スクリプト](../../../../scripts/measure-kiosk.py)をPiのホームへ転送して使用した。
各条件は30秒warm-upの後に5分、5秒ごとにサンプル採取。CDPは原則接続しない。
`system_cpu_pct_all_cores`は全4 core合計に対する割合、process CPUは1 coreを100%とする。
停止状態にもsamplerを動かして、計測負荷を含む条件にした。
監視値が78°C以上または現在のpower/thermal bitを示す場合は採取を中断する設定。

| 条件 | 全core CPU平均 / 最大 | 温度開始→終了 / 最大 | 現在throttlingのsample |
|---|---:|---:|---:|
| 両service停止 | 0.29% / 0.82% | 53.2→52.6 / 53.7°C | 0/60 |
| daemonのみ、STOPPED | 0.72% / 1.35% | 52.6→53.7 / 53.7°C | 0/60 |
| 標準Player、STOPPED、CDPなし | 1.26% / 3.96% | 55.3→54.8 / 56.4°C | 0/59 |
| 同じChromiumを静的HTMLへ切替、CDPなし | 1.08% / 1.43% | 55.8→56.4 / 56.9°C | 0/59 |

標準Playerは静的ページより全core CPU平均が0.18ポイント高かった。ただし順番固定の単回測定で、
室温変動・cache・network等の条件も完全には制御していない。支配的な負荷差とは断定しない。
起動直後に62.3°Cを一度読んだが、30秒warm-up後は上表の範囲に戻った。定常測定に起動peakを混ぜない。
全条件の`get_throttled`は0x60000で、bit 17/18はboot以降の履歴。low bitsは各sampleで0だった。
過去のfrequency capとthermal throttlingを、今回の継続的なthrottlingとは扱わない。

静的ページ条件はCDPの`Page.navigate`で同一Chromium targetを黒背景の`data:text/html`に切り替え、
HTTPのtarget一覧で切り替わったことを確認した。その後CDPを切断し、30秒経ってから5分採取した。
Cageだけの条件はまだ採取していない。static Chromiumでもdaemon/kiosk serviceは起動状態なので、
純粋なChromium単体のCPUとは異なる。

CDPはMacのSSH port forward経由で短時間だけ接続した。標準Player STOPPEDの15秒区間では
`Performance.getMetrics`のlayout count差0、Chrome Network domainのWS受信event 0、
8秒のtimeline traceでPaint/Layout等のeventは0だった。これはCDP接続を伴う別条件であり、
長時間の無接続CPU値へ足し合わせない。1920×1080のscreenshotは画面確認に使用したが、
第三者のcover画像を含むためリポジトリへ同梱しない。

### 標準Player PLAYING（CDP未接続）

loopbackの`POST /api/play`は204を返し、5秒後のAPI stateはPLAYINGで再生位置が進んでいた。
30秒warm-up後の先頭10 sample（約54.6秒、すべてdaemon/Cage/Chromium稼働中）では、
Pi全体のCPU平均70.92%、最大71.88%、温度72.0→74.7°Cだった。Chromium rendererは
1 core換算平均108.38%、GPU processは92.91%、browserは32.30%、Cageは18.61%、
daemonは8.71%。CPU値の合計は多coreのため100%を超え得る。周波数は1.1～1.2 GHz、
現在のthrottling sampleは0/10、履歴は0x60000だった。

温度が上昇したため5分を待たずにkiosk→daemonの順で停止した。停止前のAPIはPLAYING、
position_frames=7173を示した。停止後の温度は61.2°Cまで低下した。
元の`player-playing-no-cdp.jsonl.gz`には停止後の冷却sampleが7件続く。
PLAYINGの集計には先頭10件のみを使い、5分の定常平均とは扱わない。

高負荷はSTOPPEDや静的HTMLでは再現しなかった。再生中のrenderer/GPU/Cageとdaemonの増分が
大きいことは観測できたが、原因がJavaScript、CSS transition、Chromium合成、ALSA/driveの
どれかはこの区間だけでは特定できない。

### 再生中の切り分け

| 条件 | 測定時間 | 全core CPU平均 / 最大 | 温度開始→終了 / 最大 | 現在throttling |
|---|---:|---:|---:|---:|
| daemonのみ、PLAYING | 90秒 | 1.40% / 1.93% | 58.0→58.0 / 58.5°C | 0/18 |
| 静的Chromium + Cage + daemon、PLAYING | 90秒 | 2.12% / 3.89% | 59.6→58.5 / 59.6°C | 0/18 |
| 標準Player + Cage + daemon、PLAYING | 54.6秒 | 70.92% / 71.88% | 72.0→74.7 / 74.7°C | 0/10 |

daemon単独でも静的Chromium表示でも再生を継続した。高負荷は再生しながら標準Playerが
snapshotを表示する条件でのみ現れた。静的ページ条件は同じkioskのCDP `Page.navigate`で
切り替え、接続を閉じてから測定した。この単回比較はPlayerページの更新・描画経路を
優先して調べる根拠だが、JavaScript・CSS・合成の寄与率をまだ分けていない。

再生中Playerの短時間CDP測定では、10秒にWS frame受信39件、`render()`呼出39件、
DOM mutation record 429件。`Performance.getMetrics`のlayout count差498、style recalc差499、
TaskDuration差5.80秒、ScriptDuration差0.079秒、LayoutDuration差1.56秒だった。
続く6秒の`devtools.timeline`ではPaint event 574件とLayout event 296件を数えた。
CDP接続とMutationObserver挿入による摂動があるため、これらの件数を通常稼働時の値と
同一視しない。paint eventもHDMI scanoutの実pixel数ではない。

短時間CDP時のPi温度は最大73°C台で停止し、現在のthrottling bitは立たなかった。
測定区間のservice journalにALSA underrun、CDDA read error、slow_stageは見つからず、
kernel journalにも新たなUSB over-current/disconnect、thermal warningは見つからなかった。
ログ検索の対象はこのbootの測定時間帯であり、絶対的な無故障保証ではない。

### 進行バーtransitionの一時停止による切り分け

インストール済みの標準Playerは変更せず、CDPで`#progress`のinline `transition`を`none`に設定し、
computed styleが`0s`になったことを確認してCDPを切断した。その後APIから再生を開始し、
CDP未接続でPLAYINGの7 sample（36.0秒）を採取した。Pi全core CPUは平均10.79%、
最大11.14%、温度は61.2→62.3°C、現在throttlingは0/7だった。rendererは1 core換算
平均17.25%、GPU processは11.00%、Cageは0.91%だった。

通常Player再生の70.92%に比べて大幅に低く、再生中に繰り返し更新する進行バーの
CSS width transitionが主な負荷源である可能性が高い。ただし前後を交互に反復した比較ではなく、
CDPによる一時変更と実行順の差もある。自動停止watchdogの期限に達した8件目では
すでにserviceが停止しAPI stateが取得できなかったため、比較集計から除いた。
36.0秒を5分の定常値として扱わない。
#53のコードはこの比較には入っておらず、#53の改善率を示す測定でもない。
#53の現候補は同値DOM writeを省くが、再生位置が変わるsnapshotごとのwidth更新は続く。
したがってこのtransition起因の負荷が#53で消えるとはまだ言えない。

## 生データ

各JSON Lines rawはこのdirectoryの`*.jsonl.gz`にある。`gzip -dc`で確認できる。
CDP metricsは`cdp-{stopped,playing}.jsonl.gz`に保存した。`player-playing-no-cdp`は停止後の
冷却sampleも含み、比較には最初の10件だけを用いた。`player-no-transition-playing`は
watchdog停止直前までの7件と停止時の1件を含む。比較には前者のみを用いた。
各sampleにPiのUTC epoch/monotonic時刻、区間長、CPU、process/thread、RSS、memory/swap、
frequency、温度、throttling current/historyを保存した。URL、album名、API key、full process argsは
記録しない。`python3 scripts/summarize-kiosk.py <raw.jsonl>`で表の値を再計算できる。

STOPPEDの最初のrunではChromiumがprocess titleのargv[0]内に`--type`を置く形式をcollectorが
認識せず、Chromiumの各PIDをすべて`browser`と分類した。PID別CPU値とsystem CPU/温度は有効。
分類を修正した後の静的ページ/PLAYING runではbrowser、renderer、GPU process等を分けて記録した。
Chromium processのRSSはshared pageを含むため、単純合計を実メモリ消費量としない。

## 未確認と判断

PLAYINGの5分定常測定は温度上昇を避けて実施していない。Cageのみの条件、測定順を入れ替えた
反復、#53導入後の同条件比較も未実施。今回の結果からは進行バーtransitionの修正を#53で
検討する価値が高いが、改善の有無と温度余裕は修正版をPiへ入れて測り直すまで確定しない。
