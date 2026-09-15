# CDDA reader 第1段階: direct

既存のLinux media/TOC → DiscToc経路は維持する。
CddaReaderはseek後に連続PCMを取得する。PlayerState・ALSA・threadは追加しない。

## Build

```sh
cmake -S . -B build-direct -DENABLE_PARANOIA=OFF
cmake --build build-direct -j2
ctest --test-dir build-direct --output-on-failure
python3 tests/smoke.py build-direct/cdplayerd
```

OFFではlibcdioやpkg-configは不要。ONはこの段階では未実装の説明付きエラー。
paranoia実装と依存検出は次の変更単位。production defaultは未選定。

## 契約

- constructorでopen、destructorでclose。copy不可のfd所有者を使用。
- 初回・read_error後はseek必須。位置は非負int32 LBA。
- readには1 CDフレーム=1176個のint16_t単位でバッファを渡す。
- 出力は44100Hz、signed 16bit host endian、LR交互の2ch。
- 成功したCDフレーム分だけcursorが進む。終端はDiscTocを使って共通側で制限。
- ReadResultは開始LBA・要求数・取得数・共通status・診断用errnoとretry数。
- partial successでは先頭frames_readだけが有効。エラーをEOFとしない。
- directの追加retryはEIOのみ0〜10回。kernel/drive内部retryは観測できない。
- 1 ioctlは最大75 CDフレーム。失敗した要求のPCMはscratch bufferからコピーしない。

directはMMCドライブのlittle-endian音声を前提としてhost endianへ正規化する。
当該ASUSドライブの出力PCMはユーザーが正常再生を確認した。
他ドライブへの一般化やbit-perfectの保証は行わない。

## ユーザーによる実機試験（まだ実行していない）

音楽CDを入れた状態で以下を実行する。トレイは自動操作しない。

```sh
./build-direct/cdplayerd --probe-cdda /dev/sr0 --cdda-reader direct --track 1 --frames 75
```

期待: backend=direct、各readがstatus=ok、最後にcompleted frames=75。
読んだPCMは破棄するので音は出ない。これは取得成功の確認で、音質検証ではない。
エラーならそのままログを共有し、retry増加は原因確認後に行う。

診断は最大750フレーム（10秒分）を15フレームずつ取得する。
open_us、seek_us、read_us、first_block_us、elapsed_usを表示。
TOC読み取り時間はopen_usに含まない。directのseekはcursor更新であり、
物理的な位置移動の待ち時間は次のreadに含まれる。
first_block_usはseek後の最初の15フレーム取得まで。発音開始時間ではない。

## 制約と試験

O_NONBLOCKでもioctlの所要時間は制限できない。診断はCECループに入らず同期実行。
SIGINT/SIGTERMの即時停止やkernel I/Oからの即時復帰は保証しない。
単一診断の読み取り量上限もwall-clock timeoutではない。
メディア交換によるTOCとPCMの不整合検出は未実装。

hardwareなしのテストでは実際のLinuxIoctlReaderへ偽transportを注入し、
共通interface経由の連続位置・seek・75フレーム分割・失敗バッファ隔離・
部分成功・エラー後の再seek要求・retry上限を検証する。
CLIは未知backend・paranoia無効・数値/モード不正をデバイスopen前に拒否する。

## directの実機読み取り結果（ユーザー実行）

track 1、LBA 0から75フレーム取得成功。追加retry=0、全要求errno=0。
open=6474us、初回15フレーム=3122051us、次=105413us、
残り3要求は約49ms、全体=3374594us。
初回待ちの原因は未特定。seek=8usはcursor更新だけで物理seek時間ではない。
後続の保存PCMはユーザーが正常再生を確認した。連続再生は未検証。

## PCMファイル保存と試聴

`--pcm-output PATH`を指定すると、全読み取り成功後にraw S16_LEを書き出す。
左右交互・16bit・44100Hz・2ch、headerなし。host endianから明示的に符号化する。
既存ファイルへの上書き・標準出力への保存は拒否する。親ディレクトリは事前に作る。
読み取り失敗時にはファイルを作らず、書き込み例外時には今回作った不完全ファイルを削除。
強制終了・電源断時の不完全ファイル除去やディスクへの永続化は保証しない。
終了コード0とsaved/completedログを確認してから再生する。
最大750フレーム分（1764000 bytes）を一時保持する診断であり、streamingではない。
elapsed_usは保存前までで、保存オプション時のバッファへのコピー時間を含む。

### 1. 保存（ユーザーが実行）

```sh
mkdir -p build-direct/poc
./build-direct/cdplayerd --probe-cdda /dev/sr0 \
  --cdda-reader direct --track 1 --frames 750 \
  --pcm-output build-direct/poc/direct-track01-10s.pcm
```

期待: completed frames=750、savedのbytes=1764000、format=S16_LE。
同名ファイルが既にある場合は別名を指定する。Git管理外のbuild-direct以下に保存。

### 2. 試聴（保存成功後、音量を控えめにして実行）

```sh
aplay -v --fatal-errors -t raw -f S16_LE -r 44100 -c 2 \
  -D 'plughw:CARD=vc4hdmi,DEV=0' build-direct/poc/direct-track01-10s.pcm
```

NR1200から正常な速度で10秒聞こえること、異常なノイズ・途切れがないことを確認。
耳での確認だけでbit-perfectや訂正品質を断定しない。
今回は保存・再生コマンドを案内するまでで、自動実行していない。

PCM保存のhardware不要テストでは既知の符号付きサンプルのバイト列、
既存ファイル保護、親ディレクトリ欠落、短い書き込み後のエラーと不完全ファイル除去を確認する。

## 保存PCMの確認結果

ユーザーが出力PCMの正常再生を確認済み。今回は別室のため、
このdirect出力をPiのaplay → HDMI → NR1200で試聴する試験は保留した。
既存のcdparanoia CLIで作成した音声のaplay/HDMI再生は別途確認済み。
この区別を保ったままparanoia backendの実装へ進む。
CTest 5件と既存の停止smoke testは成功済み。
