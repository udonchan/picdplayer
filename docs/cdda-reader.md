# CDDA reader: direct / optional paranoia

既存のLinux media/TOC → DiscToc経路は維持する。
CddaReaderはseek後に連続PCMを取得する。PlayerState・ALSA・threadは追加しない。

## Build

```sh
cmake -S . -B build-direct -DENABLE_PARANOIA=OFF
cmake --build build-direct -j2
ctest --test-dir build-direct --output-on-failure
python3 tests/smoke.py build-direct/cdplayerd
```

OFFではlibcdioやpkg-configは不要。ONではpkg-configのlibcdio_paranoiaを検出し、
不足時には必要パッケージ名を含むconfigureエラー。production defaultは未選定。

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

## paranoia backend

導入済みlibcdio-paranoia 10.2+2.0.2を使用。

```sh
cmake -S . -B build-paranoia -DENABLE_PARANOIA=ON
cmake --build build-paranoia -j2
ctest --test-dir build-paranoia --output-on-failure
```

ONのbinaryからdirect/paranoia両方を選べる。paranoiaはinit時に独自のTOCを
内部利用するが、アプリのmedia/TOC/DiscTocの経路は既存Linux ioctlのまま。
read_cd_tocで範囲を決めた後にreaderを生成し、open時間にlibraryの準備時間を含める。

- cdio_cddap_identify/open → paranoia_init。RAIIでfree → closeの順に解放。
- mode=FULL & ~NEVERSKIP、read_limitedのmax_retries=20を明示。
- 連続readは同じparanoia contextを保持。seekのみ状態を移動。
- 対象版sourceでseekはセクター単位、成功戻り値は旧cursor、-1は失敗と確認。
  headerの「byte offset」という説明とは異なるので注意。
- b_swap_bytes=trueでhost-endian PCMを取得し、次のlibrary read前にコピー。
- callbackのSKIPはread_errorとしてそのセクターを破棄。前の成功フレームのみ有効。
- NULL返却もread_error。errnoが提供されない場合native_error=0でも成功ではない。
- directの追加retry設定はparanoiaへ適用しない（非ゼロ指定はエラー）。

paranoia_eventsは各read内のREAD、VERIFY、FIXUP群、SKIP、READERR、CACHEERR、
その他のcallback件数。実際のretry回数・訂正されたセクター数ではない。
callbackにはuser-data引数がないため、同期呼び出し中だけthread_localで記録先を渡す。
threadの生成は行わない。callback内でI/Oや例外送出はしない。

有限retry設定はwall-clock timeoutではなく、library/driver内で長時間blockし得る。
NEVERSKIPは使わない。skipを検出してもlibrary呼び出しが戻るまでは停止できない。
現在のCECループへ統合せず診断モードに留める。

### ユーザーによる次の実機試験

今回はこちらではCD読み取り・再生を実行していない。
まず音楽CDを入れた状態で1秒分の取得だけ確認する（音は出ない）。

```sh
./build-paranoia/cdplayerd --probe-cdda /dev/sr0 \
  --cdda-reader paranoia --track 1 --frames 75
```

期待: backend=paranoia、status=ok、completed frames=75。
errorやskipがあればそのままログを共有し、試聴・傷CDの評価は別途行う。
同じbinaryで--cdda-reader directに替えれば同じ診断経路で比較できる。
ドライブの温まり・キャッシュ・実行順序で値が変わるため、1回だけで優劣を決めない。
PCM保存は両backend共通の--pcm-outputを使用できる。

参照source: Debian配布libcdio-paranoia_10.2+2.0.2.orig.tar.gzの
lib/paranoia/paranoia.c（seek、read_limited）と、実機/usr/include/cdio/paranoia/。

### paranoiaの実機結果（ユーザー実行）

track 1、LBA 0から75フレーム取得成功。全要求status=ok。
open=6120638us、seek=28us、初回15フレーム=5501810us、
first_block=5502460us、読み取り全体=5502906us。
初回callbackはREAD=98、VERIFY=1、FIXUP/SKIP/READERR/CACHEERR=0。
残り4要求は72/62/64/53usでcallbackなし。
初回に取得・検証したlibrary内部バッファから後続要求を満たしたと考えられるが、
callback件数を物理I/O回数・取得セクター数・retry回数とは同一視しない。
共通ログのretries=0はdirect用の追加retry欄であり、paranoia内部retryなしを示さない。
openと最初のブロック取得の合計は約11.62秒（既存TOC取得時間を含まない）。
以前のdirect試験とは実行時の回転・キャッシュ条件が揃っておらず、優劣は未判断。
ユーザーが後続の保存PCMを再生し、問題なく聞こえることを確認済み。
長時間連続読み取り、seek後の取得、ALSA underrunは未検証。
今回のparanoia出力をPi→HDMI→NR1200で試聴したかは未確認。
正常試聴だけでbit-perfectや傷CDへの優位性を保証しない。
ON構成のCTest 6件、OFF構成の5件、既存停止smoke testは成功済み。
