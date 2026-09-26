# Read provenance実機確認（#35 / PR #87）

2026-09-26、revision 3f379cbのDocker/aarch64 packageをscripts/deploy.shでPiへ導入。
導入前後はdaemon/kioskともinactive、dpkg -Vの差異なし。Piでのcompileは行っていない。
既存CD、ASUS SDRW-08D2S-Uを使用。今回の室温・冷却・TV実表示は再確認していない。
API/SSHで操作し、CDP screenshotで1920×1080、PLAYING、Live、曲情報・coverを確認した。
screenshotは第三者画像を含むため保存対象外。音声の聴感・HDMI実表示の保証ではない。

## 観測

- single再生9 sampleで履歴は最大128件、最後のevictedは159、unique coverageは4305 CD frames。
  current/latestは異なる位置で、stream=3、device=1、disc=1、policy=1を確認。
- stop後にhistoryとcoverageが空/0、再playでstream=6。readerを維持するためdevice/disc=1は維持。
- repeatでは75 frame、2試行がcandidate=1に一致し、accepted_attempt=2、policy_revision=2を確認。
- daemon restart後はSTOPPED、空履歴、既定SINGLEへ戻る。session_idは未公開。
  これは初期化確認であり、再起動を跨ぐwarning/gap/session復元（#36）の検証完了ではない。
- 04:14 UTC以降の対象daemon journal検索でunderrun、slow_stage、read failed、failure_contextは見つからなかった。
  抜き差し・異常disc・長期試験は行っていない。最終的に両serviceのinactiveを確認。

## 負荷と制約

snapshot最大95593 bytes。CPU採取は5秒間隔5件・25秒で、CDP接続/撮影/traceと一部重複する。
全4 core CPU平均20.40%、最大30.45%。daemonは1 core換算平均20.04%、renderer22.42%。
CPU採取中の温度最大61.2°C、API検査sampleでは最大61.8°C。現在throttlingは0、
0x60000はboot以降の履歴。RSS最大daemon15.0MiB、renderer158.3MiB。
長時間平均でも無接続baselineでもなく、過去の7%前後との厳密な差分比較はできない。
履歴を含むsnapshotの転送・JSON処理量が増えているため#83の独立条件で評価する。
CDPは2秒にWS7件、trace2秒にLayout2/Paint4。物理pixel数ではない。

最初のrepeat試験はAUDIO_READYだけを待ち、TOCのcontroller反映前にplayして409で終了した。
手順をSTOPPEDかつtrack_numberありまで待つよう修正し、再実行は成功。失敗runでもfinallyで停止した。
物理hotplug/reset・能力失効は#88へ分離してPending。

## Raw

runtime.jsonl.gz、repeat-restart.jsonl.gz、cpu.jsonl.gz、cdp.jsonl.gzに匿名化した観測を保存。
CPUはscripts/measure-kiosk.py、集計はscripts/summarize-kiosk.pyを使用。
API検査はplay、5秒間隔9回のstate、stop、再play、stop。別runでread-policy repeat、play、
8秒後state、stop、systemctl restart、STOPPED/track認識後stateを照合した。
Pi再起動・cold boot・物理disc交換は行っていない。

## 詳細履歴分離後の確認（同日、PR #87追加変更）

通常snapshotからregionsを除き、GET /api/read-historyで取得する版を同じDocker手順でbuild/deploy。
stateは16437 bytes、詳細responseは79410 bytes（128件、evicted=61）。session/stream一致を確認。
stop後の履歴消去、service restart後のsession ID変更もAPIで確認した。これは#36全体の復元試験ではない。

CDP未接続の25秒・5 sampleは全core CPU平均16.85%、最大18.96%、daemon1core平均9.96%。
温度最大61.2°C、現在throttlingなし。前runはCDP接続あり、周波数・温度・タイミングも異なるので
CPU低減率の確定比較にしない。snapshotサイズの縮小は実測できたが、定常性能は#83で継続評価する。
終了時は両service inactive。終了直後3分のdaemon journal検索でunderrun/slow_stage/read failed/
failure_contextは見つからなかった。on-demand-runtime/cpu.jsonl.gzにrawを保存。
TV実表示・試聴は行っていない。runtime試験は30秒再生後にstate/履歴を取得し、35秒後にstop/restart。
