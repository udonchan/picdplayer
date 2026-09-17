# CD-DAが音になるまで

TOCはtrackの開始位置とdiscの終端を教える。曲名や音声そのものではない。
readerはその位置を使ってPCMを取得し、PlaybackEngineは取得済みPCMをALSAへ渡す。
CD frameと音声のsample frameは異なる単位なので、境界で換算する。

driveの読み取りは一定速度で完了するとは限らない。先読みqueueにPCMを蓄えると、
短い停止があっても音声を供給できる。queueが満杯ならworkerを待たせるため、
disc全体を無制限にメモリへ読み込むことはない。
ただし平均的な供給速度が再生速度を下回る場合、有限bufferを増やすだけでは解決しない。

再生位置は、最新のread位置ではない。workerは未来の音声を読んでおり、
ALSAへ渡した音声にも未再生分が残っている。engineは送信量とALSA delayから位置を推定する。
TVやアンプの内部遅延はこの推定に含められない。

seekでは古いPCMが混ざらないようqueueと出力bufferを破棄し、新しい位置を読み直す。
そのため操作を受理してから音が出るまでに先読み待ちがある。
現在のpause再開にも同じ種類の待ちがある。設定値や計測結果はGuideへ固定せず、
[機能設計](../design/functional-design.md)と[検証記録](../development/verification.md)を参照する。

ALSAのunderrunは出力するPCMが不足した状態である。再bufferして復旧する処理と、
CDから得たPCMの内容が信頼できるかを確かめる処理は目的が異なる。
音切れが解消しても、原盤PCMとの一致が証明されたことにはならない。

出力はAudioOutputを通す。現在の実装はALSAで、既定deviceはHDMIだが、
PlayerControllerやengineはHDMIの信号処理を直接行わない。
native再生の成立は[engineの履歴](../history/playback-engine.md)、
供給不足の調査は[media lifecycleの履歴](../history/media-lifecycle.md)に記録されている。

前：[状態を持つdaemon](architecture.md) / 次：[読み取り結果について言えること](integrity.md)
