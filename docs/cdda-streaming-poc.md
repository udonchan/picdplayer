# CD-DAの連続読み取り・HDMI出力PoC

## 手順

音楽CDを入れ、TV/アンプの音量を控えめにする。
リポジトリのルートからBashで実行する。

```bash
mkdir -p build/poc
set -o pipefail
cdparanoia -d /dev/sr0 -X -r 1 - 2>build/poc/stream-read.log |
  aplay -v --fatal-errors -t raw -f S16_LE -r 44100 -c 2 \
    -D 'plughw:CARD=vc4hdmi,DEV=0' \
    >build/poc/stream-play.log 2>&1
result=("${PIPESTATUS[@]}")
printf 'cdparanoia=%s aplay=%s\n' "${result[0]}" "${result[1]}"
```

`-r`でlittle-endianのraw PCM、末尾の`-`で標準出力を指定する。
音声データはpipeへ流し、ログだけをbuild/pocに保存する。
rawには形式情報がないためaplay側へ16bit・44100Hz・2chを明示する。
PIPESTATUSは別のコマンドで上書きされるので、pipeline直後に保存する。
両方の終了コードとログ、実際の試聴を合わせて結果を判断する。

## バッファと停止

pipeが満杯になると読み取りprocessの書き込みが待つため、
CD読み取りが再生より速い場合でも際限なくメモリを消費しない。
読み取りが遅れる場合にはALSAバッファが尽きる可能性があり、
`--fatal-errors`とログでunderrunを確認する。
これはPoCのバッファ構成で、最終daemonのバッファ設計は別途行う。

1曲終了後に再度実行し、再生中のCtrl-Cで停止を確認する。
端末のCtrl-Cはforeground process groupへSIGINTを送る。
中断時の非ゼロ終了は通常の再生失敗と区別し、両processが残らないことを確認する。

## 検証状況

準備済み。1曲の完走・試聴・途中停止の実機確認は未完了。
CLI間のpipeであり、cdplayerdへの再生機能追加ではない。
