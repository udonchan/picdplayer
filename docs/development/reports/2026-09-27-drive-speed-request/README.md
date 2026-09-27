# Drive speed request 実機確認（#8 / PR #113）

2026-09-27、ASUS SDRW-08D2S-U（F601）を接続したPi 3 Model Bで確認した。

`/etc/default/picdplayer`の`PICDPLAYER_EXTRA_ARGS`へ`--drive-speed-x 1`を追加して
`picdplayer.service`を再起動した。Audio CDのSTOPPED/AUDIO_READY後、daemonは
`speed_request=accepted requested_speed_x=1 applied_speed=UNVERIFIED`を記録した。
APIでは`requested_speed_x=1`、`speed_request_error=""`、`current_speed_x=null`を確認した。

loopback APIで20秒再生した結果、PLAYING・track 1・position 1468 frame、
`queued_blocks=50`、`dropped_events=0`だった。停止APIは204を返し、STOPPED/AUDIO_READYへ
戻った。該当journalにはALSA underrun、read failure、main loop stall、eject errorはない。

[`player-drive-speed-1x.png`](player-drive-speed-1x.png)はCDPで採取した1920×1080の標準Player表示である。
外部取得のalbum artworkはリポジトリへ含めないため、撮影時だけCDPでcover画像を非表示にしている。
表示する速度値は`1x REQUEST ACCEPTED`と`NOT AVAILABLE`であり、ioctl受理を実測速度へ読み替えない。
CDPの一時変更後にはPlayerを再読み込みし、通常表示を復元した。

この記録はioctl受理と短時間のAPI再生に限る。音質・騒音の主観比較、CEC操作、温度・undervoltage、
長時間再生、throughput、失敗時のdrive状態、既定速度との同条件比較は未確認である。
