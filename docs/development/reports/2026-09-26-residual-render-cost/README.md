# 残余Player描画負荷のA/B確認（Issue #61）

測定日: 2026-09-26。Pi 3、標準Player 1920×1080、同じCD、純正ケース・冷却なし。
室温・電源状態の変化は測っていない。#53で同値DOM writeを減らし、進行バーのwidth transitionを
削除した後でも、PLAYINGでは約4 snapshot/秒と約4 layout/秒が残った。
[変更前の詳細](../2026-09-26-kiosk-render-cost/README.md)を出発点とし、
進行バーだけを`width`更新から`transform: scaleX(...)`更新へ切り替えた。
位置は受信したdaemon snapshotから計算し、ブラウザで補間しない。`will-change`や常時animationは追加しない。

## 方法と表示条件

MacのLinux/aarch64 Dockerで両packageを作り、CTest 32/32件成功後に`deploy.sh`でPiへ導入した。
Piではコンパイルせず、各packageの`dpkg -V`に差異はなかった。serviceは各試験中だけ起動し、
終了後はkiosk→daemonの順で停止した。30秒程度warm-upしてからCDP未接続で5秒間隔・5分採取。
samplerは75°C、現在のpower/thermal bit、期待したPLAYING/STOPPED stateからの逸脱で停止する。

試験は二つの表示条件に分かれる。最初のA–B–AではTVが表示していたが、最後のAはユーザーの
中断指示で約3分時点に終了した。再開後のA–BではTVに画面が表示されない状態だった。
後者でもPiのHDMI connectorは`connected`、CDP screenshotは1920×1080のPlayerを示し、
試験前後にSTOPPED/PLAYING、進行バー、曲/coverを確認した。ただしCDP screenshotは
HDMI scanoutやTVの実際の発光を証明しない。TV条件を跨いだ差分を厳密なA/B効果に使わない。
各条件のCDP撮影は無接続CPU採取の前後に限定した。

| 条件 | 表示状態 | 測定時間 | 全4 core CPU平均 / 最大 | 温度開始→終了 / 最大 | 現在throttling |
|---|---|---:|---:|---:|---:|
| #53 `width`、前回基線 | TV表示 | 5分 | 7.10% / 7.99% | 60.7→61.8 / 62.3°C | 0/59 |
| `transform`、STOPPED | TV表示 | 5分 | 1.15% / 1.93% | 59.1→58.5 / 59.1°C | 0/59 |
| `transform`、PLAYING | TV表示 | 5分 | 7.08% / 8.39% | 60.7→61.8 / 62.3°C | 0/59 |
| #53 `width`、逆順確認 | TV表示 | 約3分 | 7.26% / — | 温度最大62.8°C | 0/35 |
| #53 `width`、再開後 | TV画面表示なし | 5分 | 7.23% / 8.62% | 60.1→61.2 / 61.8°C | 0/59 |
| `transform`、再開後 | TV画面表示なし | 5分 | 7.13% / 9.76% | 62.3→62.8 / 63.4°C | 0/59 |

逆順の35件は中断前にPLAYINGだったsampleのみ。STOPPEDや終了後のsampleを混ぜていない。
最後のA–Bは開始温度が一致せず、`transform`側ではswap出入りも多かった。
再開後のprocess CPU平均（1 core=100%）は#53 `width`→`transform`でrenderer 8.41→7.89%、
GPU process 7.15→7.16%、browser 3.01→2.95%、Cage 1.50→1.51%、daemon 4.46→4.44%。
全CPUの0.10ポイント差はこの条件変動と単回比較から有意な節約とは言えない。
温度・現在のthrottlingにも改善は示されなかった。

## CDPは別条件

再開後の両packageで、無接続5分の後に同じCDP scriptを10秒接続し、続く6秒をtraceした。
`window.render`の一時wrapperとMutationObserverを挿入するため、CDP指標を通常運用のCPU平均と
足し合わせない。以下は同じTV非表示条件での短時間観測。

| 指標 | #53 `width` | `transform` |
|---|---:|---:|
| WebSocket受信 / 10秒 | 40 | 39 |
| `render()`呼出 / 10秒 | 40 | 40 |
| DOM mutation record / 10秒 | 51 | 50 |
| LayoutCount差 / 10秒 | 39 | 10 |
| RecalcStyleCount差 / 10秒 | 39 | 39 |
| LayoutDuration差 / 10秒 | 0.074秒 | 0.023秒 |
| ScriptDuration差 / 10秒 | 0.036秒 | 0.037秒 |
| trace Layout event / 続く6秒 | 24 | 6 |
| trace Paint event / 続く6秒 | 48 | 12 |

TV表示時の別runでも`transform`はlayout 11/10秒、paint 12/6秒だった。
この局所変更はlayout/paintの回数を減らしたが、WebSocket/JS処理とPi全体のCPU・熱は
ほぼ同程度だった。CDP event件数は物理的な画素出力数ではない。

## 他候補と判断

`player.js`は全snapshotで`render()`を呼ぶが、同値DOM writeは#53で抑制済み。
CDPのScriptDurationは約0.04秒/10秒で、今回JS実行が支配的という証拠はない。
coalescingや配信間引きは実装しない。短命な状態、再接続、Integrity値を失う危険に見合う
効果は未確認である。

daemonは250 msごとにPresentation Modelを構築・比較し、値が変わった時だけpublishする。
今回のPLAYING daemon CPUは約4.4%（1 core換算）だが、JSON生成・比較・WebSocket送信・
CD読み取り/ALSAの内訳は分離していない。daemon/APIの更新頻度やschemaは変更しない。
将来のIntegrity/背景UIでCPUが再上昇したら、stage別の低頻度計測を先に追加する。

進行バーの`transform`は小さい変更でCDP上のlayout/paintを一貫して減らし、画面キャプチャで
位置・状態に目立つ差はなかったため採用する。ただしCPU/thermalの改善としては評価しない。
別runtime検討の条件は現時点で満たさない。TVの肉眼・音声、異なるdisc、長期運転は未確認。
今回の測定時間帯（UTC 22:00以降）のservice journalにALSA underrun、CDDA read error、
slow_stage、kernel journalに新たなUSB over-current/disconnectやthermal warningは見つからなかった。
同じbootの過去にはUSB over-currentとdrive disconnectがあるため、今回の検索結果を
システム全体の無故障保証とはしない。

## 生データ

このdirectoryの`*.jsonl.gz`がsampler/CDPのraw。`gzip -dc`で確認できる。
先頭の#53基線は[前回reportのraw](../2026-09-26-kiosk-render-cost/README.md)を参照する。
中断した逆順runはPLAYINGの35 sampleだけを抽出した。Screenshotは第三者のcover artを含むため
repoへ同梱しない。rawにはURL、album名、API key、full process argsを含めない。
