# systemd実機確認記録

> 過去の実機結果です。現行の確認範囲は[検証状況](../verification.md)を参照してください。



2026-09-16のユーザー報告に基づく。Raspberry Pi 3 Model B上のRaspberry Pi OS Liteで
unitをinstall・enableし、
再起動後に`picdplayer.service`から`cdplayerd`が自動起動することを確認した。
`direct` backend、`/dev/sr0`、HDMI ALSA出力、`/dev/cec0`を使用し、Audio CDの認識後に
TVリモコンからCEC経由で再生を開始して正常に音が出るところまで確認した。

この確認では自動起動から通常再生まで問題は観測されなかった。異常終了からの
`Restart=on-failure`、boot直後にCEC Physical Addressや光学driveの準備が大きく遅れる場合、
傷ディスクによるALSA underrun復旧は、引き続き個別の異常系試験対象とする。

2026-09-16、metadata有効版をsystemdから起動し、Audio CD認識後にPlayerControllerが
先に`STOPPED`となり、metadataが`LOADING`から`AVAILABLE`へ遷移することを確認した。
`CacheDirectory=picdplayer`で用意した`/var/cache/picdplayer`から`cache=hit`となり、
service userでcacheを読めている。metadata処理後もREGZAリモコンのPlay/Pause、CEC power status、
Active Source処理は正常だった。Pi再起動後のmetadata有効service自動起動確認は未実施。
