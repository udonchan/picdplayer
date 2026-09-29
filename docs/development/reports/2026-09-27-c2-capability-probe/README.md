# C2 capability probe 実機確認

2026-09-27、Pi 3 Model B と ASUS SDRW-08D2S-U（firmware F601）で取得した
built-in Player の 1920×1080 CDP screenshot である。`drive.c2_supported` は
`YES · DRIVE_REPORTED`、`c2_trustworthy` は `UNKNOWN` と表示されている。

この画面はdriveがC2 error pointerを報告できるという宣言だけを示す。
C2 reportの正確さ、傷discでのC2観測、read integrityは確認していない。
取得条件とAPI検証は[検証状況](../../verification.md#c2-capability-probe7docker確認)を参照する。


## C2 pointer read opt-in（2026-09-29）

`PICDPLAYER_EXTRA_ARGS`に`--direct-c2-pointers`を加え、package deploy後にserviceを再起動した。daemonは`c2_pointers=requested effective=YES support=YES`を記録した。通常14 track Audio CDへloopback APIからplayを送り、8秒後に`PLAYING`、`read_calls=75`、`failed_calls=0`、`latest.c2_status=CLEAN`を取得した。C2 packet failureは観測せず、fallback経路、傷disc、C2 trust、disc全体のintegrityは未検証である。
