# CD-DA / HDMI CLI PoC

## 目的と境界

CD読み取りと音声出力を別々に確認する。今回だけ既存CLIを使用し、
cdplayerdからsubprocessを起動する実装は追加しない。
将来はCD読み取りをlibcdio-paranoia等、出力をlibasoundへ置き換える。
WAVが今回の両者の受け渡し形式。リアルタイム連続再生やseekは未実装。

開発用ツール: cdparanoia、alsa-utils（aplay）。daemonのruntime依存ではない。
必要権限はcdrom/audio group。以下の読み取り・再生はroot不要。

## 読み取り

リポジトリのルートで実行。出力先の既存WAVは上書きされる。

```sh
mkdir -p build/poc
cdparanoia -d /dev/sr0 -X -w '1[0]-1[9.74]' build/poc/track01-10s.wav
```

第1トラックのセクター0〜749（終端を含む）を取得する。
1秒=75 CDフレームなので750フレーム=10秒。
-Xで訂正不能な読み飛ばし発生時は中断する。終了成功を確認してから再生する。
出力WAVは16bit little-endian、44100Hz、2ch。
CDの1フレーム（セクター）は、ステレオPCMの588サンプルフレームに相当する。

## 再生

TV/アンプを再生可能な状態にし、音量を控えめにして実行する。

```sh
aplay -v --fatal-errors -D 'plughw:CARD=vc4hdmi,DEV=0' build/poc/track01-10s.wav
```

数値card番号ではなくALSAのcard名を指定する。
plughwは必要ならサンプル形式等をALSA側で変換するため、
-v出力の実際のhardware設定を確認する。サンプルレートの維持は実測で判断する。
TVのELDでは2ch LPCM、44100Hz、16bit対応を確認済み。
音声経路はPi HDMI → REGZA → ARC → NR1200。
正常終了だけでは音が聞こえることの確認にならないので、ユーザーの試聴も必要。

## 検証状況

- cdparanoia 10.2でセクター0〜749を読み取り、終了コード0。
- Python waveで2ch / 16bit / 44100Hz / 441000サンプルフレームを確認（10秒）。
- aplayは終了コード0。実行ログにunderrun等のエラーなし。
- ALSA入力: S16_LE、2ch、44100Hz。
- ALSA hardware: IEC958_SUBFRAME_LE、2ch、44100Hz。
  plugによるHDMI向けサブフレーム変換を確認。サンプルレート変換は発生していない。
- ユーザーがNR1200から正常な速さで、ノイズ・途切れなく聞こえることを確認済み。

今回は10秒のファイルを読み終えてから再生した試験。
CD読み取りと再生の同時実行、連続再生、pause/seek、長時間の安定性は未検証。
音声ファイルはbuild/以下でGit対象外。コミットには含めない。
