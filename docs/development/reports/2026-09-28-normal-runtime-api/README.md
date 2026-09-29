# 通常runtime API操作とdaemon再起動確認（#4）

2026-09-28、Pi 3上で通常の14 track Audio CDを認識したdirect single readerに対し、SSH経由の
loopback API操作を確認した。対象はmasterのpackageであり、daemon/kioskは開始時からactiveだった。

## 実施内容と結果

| 操作 | API結果 | 観測結果 |
|---|---:|---|
| play | 204 | 7秒後にPLAYING、position 474 frame、queued blocks 50 |
| pause | 204 | PAUSED、position 508 frame、queued blocks 0 |
| seek +10秒 | 204 | PAUSEDのままposition 1,258 frame |
| resume | 204 | 7秒後にPLAYING、position 1,766 frame、queued blocks 50 |
| stop | 204 | STOPPED、position 0、queued blocks 0 |
| `picdplayer.service` restart | 成功 | daemon/kioskともactive、API復帰後はSTOPPED |

全snapshotで`effective_strategy=direct-single-read`、`dropped_events=0`だった。このrunのjournalには
read error、ALSA underrun、eject errorを見つけなかった。同bootには試験より前の
`main_loop_stall stage=control duration_us=74989`が一件あるため、通常操作中に新規発生しなかったことと
boot全体でstallがないことは区別する。

## 限界

SSH/API観測だけを行った。TV実表示、HDMI/ARC音声の試聴、CEC入力への画面追従、Custom UI、cold boot、
NO_DISC/LOADING、metadata/画像失敗、長時間運転、ejectやdrive消失は確認していない。これらは#4の
残る完了条件または#146/#88/#83の担当範囲である。
