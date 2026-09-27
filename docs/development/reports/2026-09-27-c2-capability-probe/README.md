# C2 capability probe 実機確認

2026-09-27、Pi 3 Model B と ASUS SDRW-08D2S-U（firmware F601）で取得した
built-in Player の 1920×1080 CDP screenshot である。`drive.c2_supported` は
`YES · DRIVE_REPORTED`、`c2_trustworthy` は `UNKNOWN` と表示されている。

この画面はdriveがC2 error pointerを報告できるという宣言だけを示す。
C2 reportの正確さ、傷discでのC2観測、read integrityは確認していない。
取得条件とAPI検証は[検証状況](../../verification.md#c2-capability-probe7docker確認)を参照する。
