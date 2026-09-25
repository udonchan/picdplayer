> 過去の設計・調査・検証記録です。提案や当時の制約を含み、現行仕様ではありません。
> 現行仕様は[ドキュメント索引](../README.md)から参照してください。

# 第1段階の実機検証（2026-09-15）

以下は第1段階時点の記録。現在の再生機能は[再生engine](playback-engine.md)、
自動起動の実装・確認結果は[systemd常駐運転](../manual/systemd.md)を参照。

## 環境

|項目|観測結果|
|---|---|
|本体|Raspberry Pi 3 Model B Rev 1.2|
|OS|Debian GNU/Linux 13.7 (trixie)、kernel 6.18.34+rpt-rpi-v8|
|architecture|aarch64|
|compiler / CMake|GCC 14.2.0 / CMake 3.31.6|
|実行ユーザー|udonchan、video/cdrom/audio所属|
|CEC|/dev/cec0、root:video、0660、vc4_hdmi|
|光学ドライブ|/dev/sr0、root:cdrom、0660|
|USB ID|0e8d:1806、lsusb表示はMediaTek / Samsung SE-208|
|ALSA|card 0 Headphones、card 1 vc4hdmi device 0|

ユーザー申告のドライブ筐体はASUS。USB IDの名称だけで実製品を断定しない。
ALSA列挙まで確認。PCM再生、CD挿入状態、TOCは未確認。
ツールのsandbox内では/devが隠され、補助groupも異なって見えた。
sandbox外で一般ユーザーとして実機を確認した（root実行ではない）。

## 検証

1. CMake configure/build成功（C++20、警告有効）。
2. hardwareなしの試験でSIGINT/SIGTERM正常終了、未出現デバイス待機中の停止、
   不適切なデバイスへのioctlのエラー終了を確認。
3. 実機で既存登録 physical=4.0.0.0 / logical=8 / PiCDPlayerを再利用。
4. cec-ctl --clearでmask=0、登録数0を確認後、daemonを起動。
   `playback claim requested` → `logical=8 name=PiCDPlayer claim confirmed`を確認。
5. 8秒後のSIGTERMで正常終了。停止時にもCEC登録を保持。

## ユーザーによる追加確認

- CEC登録後、REGZAからNR1200へのARC音声が維持されることを確認済み。
- 暫定picdplayer-cec.serviceを無効化してPiを再起動するとTVスピーカーへ戻った。
- 再起動後にcdplayerdを手動起動し、未登録状態からlogical=8をclaimして
  オーディオシステムに切り替わることを確認済み。
- これはPhysical Address確定後の起動試験であり、f.f.f.fからの遷移試験ではない。

## 残る実機確認

- boot直後のPhysical Address=f.f.f.fからの遷移、HDMI抜き差し、長時間維持は未試験。
- cdplayerdのsystemd登録は未実施。

## 仕組みと参照

Physical AddressはHDMI接続位置で、今回4.0.0.0。daemonは読み取りのみ。
Logical AddressはCEC通信用で、Playbackの候補からkernelがclaimする。
OSD Nameはadapterに設定され、kernelのCEC基本応答に利用される。
新規登録では特定メーカーを名乗らずCEC_VENDOR_ID_NONEを使用する。

- [Linux: Logical Address ioctls](https://cdn.kernel.org/doc/html/latest/userspace-api/media/cec/cec-ioc-adap-g-log-addrs.html)
- [Linux: Physical Address ioctls](https://cdn.kernel.org/doc/html/latest/userspace-api/media/cec/cec-ioc-adap-g-phys-addr.html)
- [Linux: CEC modes / kernelの応答範囲](https://cdn.kernel.org/doc/html/latest/userspace-api/media/cec/cec-ioc-g-mode.html)
