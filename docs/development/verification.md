# 検証状況と残課題

更新日: 2026-09-26。実装済み、hardware非依存試験済み、実機確認済みを区別する。
日付付きの測定は当該条件だけの結果である。

現在の到達点は[実機確認済み](#実機確認済み)、次に取り組む作業と進捗は
[残課題とIssue一覧](backlog.md)を参照する。末尾の継続課題は検証上の根拠として残す。
その間の日付付きの節は、条件ごとの個別実験記録である。過去のbuffer値や試験件数を
現在の仕様として使わず、設定の正本は[機能設計](../design/functional-design.md)で確認する。

## 現在の確認待ち

傷disc、cache独立性、速度変更、S/PDIF出力は未確認。
Now Playingのcold boot後TV表示、停止中metadata・画像表示は確認済み。CEC操作後の画面追従や異常時表示は
[Now Playing実機確認結果](#now-playing実機確認結果)に残る範囲を記す。
S/PDIFは[将来候補](digital-audio-output.md)であり、現在の必須試験ではない。

## 文書・診断API・CDP監査（2026-09-26、#79 / #80 / #81）

現行コードと文書を照合し、画像binary cache / same-origin配信、EnrichmentServiceと
Presentation Modelの経路、SSHでのデプロイ・サービス操作・loopback API操作の記述を修正した。
物理操作・cold boot・TV実表示・試聴は引き続きユーザーによる確認を区別する。

Presentation ModelのJSONで欠落していた`drive`、`read`、`recent_events`を復元し、
technical statusを現行の曲位置・disc・enrichment fieldへ合わせた。
CDP測定は`Tracing.end`応答前に到着するtrace eventも集計し、handshake中の切断で停止する。

Linux/aarch64 Dockerで`./scripts/build-container.sh`とCTestを実行し、34/34件成功。
回帰試験では診断値・eventの公開と差分判定、statusの曲位置・No Disc能力表示、
CDP traceの応答前後の到着順序と未完了、handshake EOF・header上限を確認した。
今回の修正後のPi実表示・再生・性能は再検証していない。

過去のtrace event件数は応答順序によって過少集計された可能性があるため、確定的な件数比較に
使用する場合は修正版で再測定する。過去のrawデータやCPU測定値は書き換えない。
診断値の復元で配信内容・更新頻度が変わり得るため、過去の負荷測定を修正後の保証値としない。

## Kiosk定常負荷の計測手順（Issue #52）

Pi 3でのCage + Chromium kioskのCPU・温度問題は、#52の実測で再生中の
進行バーwidth transitionが有力原因と判明した。測定条件と限界は下記reportに記録する。
過去の`get_throttled=0x60000`はbit 17/18のboot以降の履歴であり、現在throttling中を意味しない。
現在状態はlow bitsを見る。[Raspberry Pi公式のbit定義](https://www.raspberrypi.com/documentation/computers/os.html#get_throttled)を参照。

同じPi、電源、冷却、室温、TV/HDMI、解像度、disc、network、metadata cacheで条件をそろえる。
30秒warm-up後に各条件を5分以上測り、STOPPED/PLAYINGとCDP未接続/接続を区別する。
`ps %CPU`はprocess起動以来の値なので、[計測スクリプト](../../scripts/measure-kiosk.py)は
`/proc/stat`と各taskのCPU tick差分から5秒区間の値を計算する。process別CPUは1 core=100%、
system CPUは全coreに対する割合。温度、現在/過去throttling、各CPU周波数、memory/swap、
thread別上位5件、RSSと時刻をJSON Linesへ記録する。rawにはfull command line、URL、API keyを入れない。

```sh
# Macで測定スクリプトをPiのhomeへ置く。Pi上ではコンパイルしない。
ssh picdplayer-pi 'mkdir -p ~/picdplayer-kiosk-perf/tools'
rsync scripts/measure-kiosk.py picdplayer-pi:~/picdplayer-kiosk-perf/tools/
ssh picdplayer-pi 'python3 ~/picdplayer-kiosk-perf/tools/measure-kiosk.py \
  --condition services-stopped --seconds 300 --interval 5' > stopped.jsonl
python3 scripts/summarize-kiosk.py stopped.jsonl
```

測定中にSoCが78°C以上、または現在のundervoltage・frequency cap・throttling・soft temperature
limitが立てばsamplerは異常終了する。serviceを起動して測る場合は、その終了時に停止するtrapを設定する。
測定によるCPU/温度増分は停止状態にもsamplerを動かして確認する。異常時は継続せず電源・冷却を確認する。
ALSA underrun、daemon slow stage、CDDA errorは同じ時刻の`journalctl`から別途照合する。

CDPはMacからSSH port forwardingで接続し、[短時間の計測スクリプト](../../scripts/measure-kiosk-cdp.py)
でPerformance metricsと任意のtimeline trace・screenshotを取得する。CDP自身がCPU/paintに負荷を
加えるので、未接続の5分測定と混ぜない。`Performance.getMetrics`のLayoutCountなどはrendererの
累積値の差であり、画面に実際に表示されたpixelを保証しない。traceは最大10秒に制限する。

```sh
ssh -N -L 9222:127.0.0.1:9222 picdplayer-pi  # 別terminal
python3 scripts/measure-kiosk-cdp.py --seconds 15 --trace-seconds 10 \
  --screenshot /tmp/picdplayer-player.png
```

### 測定結果

2026-09-25〜26のPi 3実測、raw data、未確認条件は
[kiosk基準測定レポート](reports/2026-09-25-kiosk-baseline/README.md)に記録した。
再生中の標準Playerは全core CPU平均70.92%（54.6秒）、進行バーのCSS transitionだけを
一時停止したPLAYING区間は10.79%（36.0秒）だった。測定時間・順序が異なるため、
長時間の改善率ではない。この基線は#53導入前の条件である。

#52のクローズに向け、[完了判定と制約](reports/2026-09-25-kiosk-baseline/README.md#完了判定の整理2026-09-26追記)を追記した。
既存rawを再集計し、新規測定は行っていない。Cage単独は改善対象の特定に不要として省略し、
過去のtrace件数を定量的な削減率の根拠から外した。現行masterの負荷・残測定は#83、長期運転・実表示・音声は#4で別途確認する。

## 標準PlayerのDOM更新削減（Issue #53）

#53は#52のbaseline測定をhard dependencyとする。最初のDOM write削減は標準Playerの
書き込み経路の再現試験で確認した。
変更前は`f0bf7f6`のplayer.js、変更後は本Issueのplayer.jsを、同じNode VM/DOM mockへ読み込んだ。
[test](../../tests/ui_render_test.js)は同値代入も含めてDOM write呼び出しを数える。

| 入力 | 変更前 | 変更後 |
|---|---:|---:|
| 同一STOPPED snapshot 60件 | 780 | 0 |
| 15 frameずつ進むPLAYING snapshot 60件 | 780 | 72 |

再生位置更新の72回はwidth 60回と秒表示12回であり、受信した再生位置を間引いた結果ではない。
これらは合成入力で、PiのWS受信レート、実paint回数、CPU、温度の測定値ではない。
daemonは既にrevision以外が同じPresentation Modelのpublishを抑制しているため、停止中の高負荷が
この経路で起こるとは断定できない。その後の#52実測では、再生中の進行バーwidth transitionが
高負荷の有力原因となったため、#53でtransitionを削除した。再生位置のsnapshot頻度と表示値は維持する。

再現方法は標準Docker内で`node tests/ui_render_test.js`。変更前の比較には任意のplayer.jsのpathを
第1引数に渡し、環境変数`PICDPLAYER_RENDER_BENCHMARK_ONLY=1`で回帰assertionを省略する。
JSON入力はtest内に定義し、画像はfixture URLだけを使用する。測定はNode mock上でありCDP接続を伴わない。
Docker/aarch64の標準buildとCTest 32/32件が成功し、API・Custom UI・起動telemetryの既存試験も通過した。
packageをPiへ導入し、STOPPED/PLAYINGをCDP未接続で各5分測定した。再生中の全core CPU平均は
変更前70.92%（54.6秒）から変更後7.10%（5分）、変更後の最高温度は62.3°C、現在のthrottlingは
0/59 sampleだった。CDP短時間測定でもWS受信約40件/10秒を維持しながらlayout/paintが減少した。
CPU/process/thread、memory、frequency、温度、CDP指標、journal、raw dataと限界は
[実機改善レポート](reports/2026-09-26-kiosk-render-cost/README.md)に記録した。
CDP screenshotで表示を確認したが、TVの肉眼・音声確認や異なるdiscでの長期反復は未実施。
測定根拠のあるDOM/CSS改善後も持続的な高CPUや現在のthrottlingが再発した場合に、
同条件のprofileを取り、別runtime/UI構成を検討する。現時点で置換は決定しない。

## 残余Player描画負荷（Issue #61、2026-09-26）

進行バーを`width`更新から`transform: scaleX`更新にした比較を
[実機A/Bレポート](reports/2026-09-26-residual-render-cost/README.md)に記録した。
TV表示時とTV画面非表示時を分け、CDP screenshotでChromiumの描画継続を確認した。
後者のCDPではlayout 39→10件/10秒、paint 48→12件/6秒に減った一方、
CDP未接続の5分CPU平均は7.23→7.13%であり、CPU/温度改善とは判断しない。
daemonのJSON生成・送信費用は未分離であり、WS頻度やCustom UI契約は変えていない。

## 自動試験

CTestはCMakeの有効機能で件数が変わる。基本buildではcontroller、engine、ALSA抽象、CEC変換、
media tracker/worker、TOC、reader、PCM保存、snapshot、CLIなどを検証する。
metadata有効時はDisc ID公式vector、候補0/1/複数と不正JSON、worker/sessionの旧結果破棄を追加する。
API有効時はJSON schema、route、technical status/Now Playing asset、loopback socket、無通信時のserviceがmainへ戻る回帰試験を追加する。
paranoiaにはlibrary呼び出しをwrapした試験がある。
logger試験は時刻・level・component、stdout/stderrのlevel別振り分け、終了時のqueue drainを確認する。

標準の再実行手順（Mac、初回はDocker imageを作成）:

```sh
./scripts/build-container.sh
docker run --rm -v "$PWD:/src" -w /src picdplayer-build ctest --test-dir build-container --output-on-failure
```

2026-09-25、[CI導入PR #2](https://github.com/udonchan/picdplayer/pull/2)でPython 3をDockerfileへ追加し、
クリーンな作業コピーとGitHub ActionsのARM64 runnerで全29件が通過した。
内訳にはNode.jsの2件とPythonの3件を含む。PR CIは標準のmetadata/API有効・paranoia無効構成を対象とし、
全CMake optionの組合せやhardware動作を保証しない。以下は過去の構成ごとの結果である。

Phase 2基礎実装時にdirectの16/16、metadata/API buildのAPI以外21/21、
sandbox外のAPI socket 1/1成功を確認した。
これは全option組合せの保証ではない。socket試験はloopback通信を許可した環境で実行する。
2026-09-25のMac上Docker/aarch64 buildではNode.jsによる標準UIとwrapperの動作試験を含む26件が通過した。
Node.jsがない環境ではその2件を登録せず、C++ビルドは従来どおり可能。
2026-09-18のReadPolicy作業ではmetadata/API buildのAPI以外23件とsocket試験1件、
direct buildの関連4件（playback_engine/read_policy/cdda_cli/player_daemon）が成功した。
policy入力、JSON、reader再生成・region変更を確認したが、実機上の切替試聴を代替しない。
テスト名・登録条件の正規情報は[CMakeLists.txt](../../CMakeLists.txt)にある。

## 開発用Debian package deploy（Issue #43、2026-09-25）

Apple Silicon Macの標準Docker/aarch64手順で`picdplayer_0.1.0_arm64.deb`を生成し、
CTest 30/30件、隔離containerでの`dpkg -i`/`dpkg -r`と`/usr/local`配置を確認した。
Piでは限定sudoers ruleを使い、`scripts/deploy.sh`から`dpkg -i`をパスワードなしで実行した。
`dpkg-query`は`install ok installed`を示し、`dpkg -V picdplayer`は差異を報告せず、
`systemd-analyze verify`も対象unitに問題を報告しなかった。daemon/kioskはCPU負荷の調査のため
意図的に停止されており、deploy前後ともinactiveだった。packageのpostinstは停止中のserviceを
起動しない。その後、ユーザーが手動起動し、TVの標準Player表示を確認した。Piのloopback
`GET /api/state`と`GET /player`は正常に応答し、両serviceはactiveだった。CEC Playで
一度は`PLAYING`へ遷移したが、約2秒後に`CDDA read failed lba=390 errno=19`で停止した。
同時刻のkernel logにはUSB hubの`over-current change`、drive disconnect、再接続があり、
`/dev/sr0`は再作成された。ユーザーの再試行では正常動作と報告され、APIでもTrack 1の
`PLAYING`と再生位置の進行を確認した。packageの配置・起動確認と、一時的なUSB給電/接続事象は
分けて扱う。ユーザーが限定sudoersに各serviceのstart/stop/restartを追加した後、Macから
非対話でkiosk→daemonの順に停止した。変更後の`deploy.sh`で同版packageを再installし、
両serviceがinactiveのまま維持されることを確認した。給電原因の確定と長時間再生は未確認である。

### PR再レビューでの追加確認

#43の再レビューで、失敗したbuildの古いpackage残存、unitなし構成での不要なservice操作、
failed/transitional serviceを正常deployと扱う可能性、remove時のstop失敗の無視を修正した。
Docker/aarch64で再生成し、CTest 31/31件（deploy/build失敗経路とmaintainer scriptの13ケースを含む）が
成功した。unitなし構成でもscript試験が成功し、隔離containerでinstall/remove、同版更新時の
旧package所有ファイル削除を確認した。systemdの有無・command失敗・順序はmockによる確認で、
修正版をPiへ再deployし、両serviceのinactive維持と`dpkg -V`の差異なしを確認した。
実際の稼働中service更新と更新失敗からの復旧は未検証である。
上記のTV表示・再生確認は再レビュー修正前のpackageに対する結果である。

## 実機確認済み

| 対象 | 確認範囲 |
|---|---|
| CEC | Playback登録、REGZA/MarantzのARC復帰、remote操作、power/active source応答 |
| drive/TOC | ASUS SDRW-08D2S-U、tray開・空・Audio CD・取り出し、14曲TOC |
| PCM | direct/paranoiaの保存PCM正常再生。条件を揃えた性能比較は未実施 |
| native再生 | HDMI出力、pause/再開、曲移動、曲境界seek、stopで先頭へ戻る |
| lifecycle | 空起動、挿入、自動TOC、取り出し・再挿入後の再生 |
| systemd | 自動起動からCEC再生。metadata有効版のservice起動・cache hit・play/pause |
| metadata | 14曲Disc ID、候補1件、AVAILABLE、cache miss/hit、並行CEC処理 |
| API | Macから外部GETによる状態照会、eject要求とトレイ動作 |
| Now Playing | Macのbrowserでmetadata、track、CAA cover art、Live接続、停止状態の表示を確認。cold boot後のTV表示とUI telemetry到達も確認 |
| eject待受修正 | 2026-09-17に一回の要求でトレイが開いたとのユーザー確認 |
| integrity Phase 1a | ASUS drive能力、direct read集計、先読み/ALSA再生head、bounded eventを通常CD再生中のAPI snapshotで確認 |
| ReadPolicy runtime切替 | repeatを適用して再生後、SINGLE要求をPLAYING/PAUSED中に保留し、STOPPED境界で適用。APIでrequested/effective/pendingと15 frame single readerへの切替を確認 |
| integrity Phase 1b | Macのbrowserでtechnical statusを表示。停止・通常再生、Live接続、player位置、現在再生PCM、先読みread、統計、event、drive能力を確認 |
| integrity Phase 2 buffer | direct singleで300/150、300/75、300/45、750/45 frameを比較。750/45で通常再生、操作、Mac状態表示を確認 |
| 非同期logger | UTC/monotonic時刻、level、componentをforegroundで確認。CEC操作、再生、technical status、SIGINT時flushに退行なし |
| drive start維持 | CDROMSTARTをTOC直後と停止・一時停止中15秒周期で実行。再生中抑止、長時間停止後の高速Playを確認 |

14曲CDのleadout LBAは242334、Disc IDは6JTbUgqHL29gzUyOH5ir60K3hz0-。
数値はこの試験discの結果であり、実装の固定値ではない。

## ReadPolicy runtime切替の実機確認

2026-09-18、14曲Audio CDとdirect backendで確認した。停止中にrepeat（75 frame、2-of-3）を
要求すると直ちにeffectiveとなり、`direct+repeat-2of3`と75 frame readerで再生した。再生中に
singleを要求するとrequestedだけがSINGLEへ変わり、PLAYINGとPAUSEDの間はeffectiveがREPEAT、
`pending: true`を維持した。再開してstopすると`read_policy: applied mode=SINGLE`を記録し、
APIで`direct-single-read`、15 frame reader、requested/effectiveともSINGLE、`pending: false`を確認した。

この確認は安全な適用境界と状態公開の確認である。傷disc、read error、時間上限超過時のpolicy変更、
各policyの音質・CPU・操作待ち時間の比較は未実施である。

## Phase 1b実機確認結果

2026-09-18、Macのbrowserからtechnical statusを開き、停止中とdirect single再生中の表示を確認した。
WebSocketは`Live`となり、再生中はPLAYER、track、position、current PCM、buffer、latest read、集計、
recent observationsが更新された。current PCMはLBA 2730–2745、latest readはLBA 3045–3060で、
現在再生区間と先読み区間を別に表示した。204 calls × 15 frames = 3060 accepted frames、retry/failureと
dropped eventは0だった。strategyは`direct-single-read`、ReadPolicyは`SINGLE`として分離表示した。

metadataなしで起動したためNOT_REQUESTED、album/artistなしとなることも仕様どおり確認した。
続けてブラウザ再読み込みとnetwork切断後のWebSocket再接続・snapshot復元が正常に動作することを確認した。

## Now Playing実機確認結果

2026-09-19、Macのbrowserから`/player`を開き、MusicBrainzの単一候補metadataでalbum/artist、
track 1のtitle/artist、曲長、STOPPED表示を確認した。`metadata.cover_art.status=AVAILABLE`とCAAの
HTTPS image URLを取得し、CSPの`img-src`許可後にジャケット画像を表示できた。右上はWebSocket接続中の
`Live`を示した。この確認はbrowser表示だけであり、CEC操作による画面追従、NO_DISC/LOADING、metadata
NOT_FOUND/AMBIGUOUS、画像取得失敗時の表示は継続確認項目である。

## Phase 1a実機確認結果

2026-09-17、14曲CDのdirect再生中にAPI snapshotを取得し、次を確認した。

- ASUS SDRW-08D2S-U、firmware F601を取得。speed controlはKERNEL_REPORTED/YES。
- DAE、C2 support/trust、cache、accurate streamはUNKNOWN、offsetとcurrent speedはnullを維持。
- player位置551 LBAはcurrent playback区間[540,555)内、最新先読み区間は[855,870)。
- 58 read calls × 15 frames = requested/accepted 870 frames。retry、failure、backend anomalyは0。
- directの各区間はCLEAN/SINGLE_READ/C2 NOT_CHECKED/offset UNKNOWNとして公開。
- event sequence 1〜58は連続し、stream generation 3で統一、dropped eventsは0。
- queueは20 block上限で動作した。metadata、TOC、player stateも従来どおり同じsnapshotへ含まれた。

この結果は通常CDで観測経路が正しくつながったことを示す。CLEANは原PCMとの一致を証明せず、
傷disc、retry、paranoia recovery、event overflowなどの異常経路は未確認である。

### 再確認手順

常駐serviceとのdevice競合を避けて停止し、API付きbuildをforegroundで起動する。

```sh
sudo systemctl stop picdplayer.service
./build-metadata/cdplayerd --player /dev/sr0 --cdda-reader direct \
  --metadata musicbrainz --metadata-cache /tmp/picdplayer-cache --api-port 8080
```

別terminalから`GET /api/state`を確認する。起動後は`drive`にidentityと能力の根拠が入り、未調査の
DAE/C2/cache/offsetはUNKNOWN/nullのままであることを確認する。通常CDを再生すると`read.latest`、
`read.current_playback`、`read.stats`、`recent_events`が更新される。先読み中のlatestとALSA再生headの
current_playbackは異なるLBAになり得る。通常readのCLEANはSINGLE_READであり、検証済みを意味しない。

```sh
curl --fail --show-error http://127.0.0.1:8080/api/state |
  python3 -m json.tool
```

CECでplay/pause/seek/next/stopを操作し、音声と従来のstate transitionに退行がないことを確認する。
stop後の`read.activity`はIDLE、再生再開後のstatsは新しいstreamについて集計し直される。
確認後はforeground daemonをCtrl-Cで止め、`sudo systemctl start picdplayer.service`で常駐運転へ戻す。

## 次の確認と残課題

- [読み取り信頼性の仕様](../design/integrity-design.md)のPhase 1aは実装・通常CDで実機確認済み。
  ReadResultからのtruthfulなevidence変換、集計、PCM blockへの伝搬、古い世代の排除、read-only能力probe、
  bounded event、ALSA再生head推定、snapshot JSONを自動試験へ追加した。C2取得は未実装。
- Phase 1bのtechnical statusは実装・自動試験済みで、停止・通常再生、ブラウザ再読み込み、
  network切断後のWebSocket再接続とsnapshot復元を実機確認した。NO DISC表示は継続確認項目とする。
- Phase 2の通常CD比較を行い、既定bufferを750 frame、開始閾値を45 frameとした。
  傷disc、長いread stall、memory/CPU、異なるdriveでの評価は継続する。
- drive access直列化後の挿入、TOC、再生、停止中観測、ejectを実機で確認する。
- Phase 3の`--read-verification repeat`はfake readerでA/A、A/B/B、全不一致、read error、時間上限を確認済み。
  15 frame regionの初回実機試験では約359 ms/readとなり、PCM生成が実時間を下回って周期的underrunが発生した。
  75 frame変更後の正常再生と先読み待ちは下記で確認済み。CPU負荷、傷disc、時間予算超過、cache独立性は未確認。
- `last_prebuffer_wait_ms`と`prebuffer_target_frames`をAPI/logへ追加した。single/repeat、およびplay/seek/
  track変更での先読み待ちを下記で比較した。TV/ARC/アンプ側の遅延はこの値の対象外。

### Phase 3 初期実機確認

2026-09-17、14曲Audio CDをdirect backend・`--read-verification repeat`で再生し、75 frame regionへ
変更後は周期的underrunなしで再生、CEC seekとtrack変更を確認した。再生中のAPI snapshotでは86 read call、
6450 requested/accepted frames、172 verification attempt（各region 2回）、86 verified call、mismatch/failure/
direct retryはすべて0だった。latest regionは`CLEAN + MULTIPLE_MATCH`、2 complete read/2 matching read、
time budget超過なしであることを確認した。

この結果は同一driveから同じPCM bytesを2回得たことを示す。drive cacheから独立したread、傷discでの
recovery、原盤PCMとの一致、条件を揃えた反復測定やCPU負荷の評価は未確認である。

### single / repeat 先読み待ち比較

同じ14曲CD、direct backend、150 frameの先読み条件で、2026-09-17に次を測定した。値は
`prebuffer_ready`であり、HDMI以降の出力遅延を含まない。

| 操作 | single | repeat | 差（repeat - single） |
|---|---:|---:|---:|
| 初回play | 1339 ms | 1035 ms | -304 ms |
| CEC seek forward | 596 ms | 768 ms | +172 ms |
| next track | 635 ms | 814 ms | +179 ms |

repeatでは75 frame regionを2回読むため、seek/track変更後に約0.2秒の追加待ちが観測された。一方、
初回playは75 frame単位の連続readが15 frame単位より効率的だった可能性があるが、1回の測定だけで
一般化しない。repeatのAPI snapshotは23 read call、1725 requested/accepted frame、46 attempt、
23 verified call、mismatch/failure/retry 0だった。通常CDでは両modeとも音切れなく再生できた。

### Phase 2 buffer比較

2026-09-18、同じ14曲CDとdirect single readerで、容量/開始閾値をCD frame単位で比較した。
各条件でCECのplay、seek、next、pause/play、stopと通常再生を行い、underrunとread failureはなかった。

| 容量/開始 | 初回play | seek | next | pause→play | 備考 |
|---|---:|---:|---:|---:|---|
| 300/150 | 1040 ms | 493〜557 ms | 674〜689 ms | 599 ms | 従来既定値 |
| 300/75 | 532 ms | 327〜333 ms | 428 ms | 313 ms | 容量を保ち開始量を半減 |
| 300/45 | 3292 ms、再play 412 ms | 227 ms | 369〜400 ms | 219 ms | 初回だけdrive起動とみられる外れ値 |
| 750/45 | 481 ms、再play 394 ms | 210〜229 ms | 246〜422 ms | 159 ms | 通常再生とMac状態表示も正常 |

初回3292 msは同じ起動内の再playでは再現せず、開始閾値だけの効果とは断定しない。
750/45はPCM約1.68 MiB、10秒分の容量と0.6秒分のsingle開始閾値であり、今回の通常CDでは
応答性と連続再生を両立したため既定値に採用した。repeatは75 frame blockなので実際の開始量は
1 block（1秒）へ切り上がる。容量増加が傷discや4秒超stallを救済することはまだ実証していない。

その後の750/45連続再生で一度ALSA underrunを観測した。直前はCD queue 50/50、未送信sample 12592、
read 46.7 ms、read in-flightなしで、CD読み取り不足ではなかった。main loopのtick gapが175.3 msとなり、
既定200 msのALSA latency内にwriteできなかったことが直接の失敗条件である。自動復旧はresume LBA 92086、
開始8 block（120 frame）で成功した。停止したmain処理の特定用に50 ms以上のcontrol、CEC、engine、
API snapshot/serviceを記録する診断を追加し、ALSA latencyを起動optionで比較可能にした。

ALSA latency 500 ms、APIとMacのtechnical statusを有効にして10分超再生した試験では、音飛びと
underrunは発生しなかった。一方、50 ms以上のstallを147回記録し、すべて`api_snapshot`だった。
所要時間は主に約57〜68 ms、最大83.6 msである。変更検出用revision 0と配信用revision付きJSONを
毎回二重生成していたため、revision以外を比較する関数を追加してsnapshot生成を1回へ削減した。
最適化後に既定200 msへ戻し、Macのtechnical statusを接続した状態で再生、前後seek、next、stopを
繰り返した。`api_snapshot`の50 ms超過は1回（58.8 ms）まで減り、音飛び、failure context、underrunは
発生しなかった。この結果から既定200 msを維持し、500 msは環境別の比較optionとして残す。

### 非同期logger実機確認

2026-09-18、metadata/API buildをforegroundで起動し、UTC wall clock、logger起動後のmonotonic経過時間、
DEBUG/INFO level、player/CEC/media/API/drive componentを確認した。direct singleでplay、連続seek、next、
stopをCECから操作し、音声とtechnical statusのLive更新は正常だった。WARN/ERROR、underrun、log dropは
発生しなかった。SIGINTでは`shutdown signal=2`と`output stopped`を順に出力して終了し、queue内の
終了ログが失われないことを確認した。

初回playの先読みは4001 ms、その後のseekは162〜186 ms、nextは257〜271 msだった。初回値は停止状態で
約100秒経過した後の一回だけの測定であり、drive再始動を含む可能性があるためlogger overheadとは断定しない。
この時点ではsystemd/journald経由と意図的なqueue overflowは未確認だった。
後述の2026-09-25起動計測でjournaldの起動ログは確認したが、service停止時flushは確認待ちである。

### CDROMSTART一回診断

2026-09-18、停止していたASUS SDRW-08D2S-Uへ`CDROMSTART`を要求し、2566 msで受理された。
直後にtrack 1先頭15 frameをdirect readerで読むとopen 9.7 ms、read 284.3 ms、first block 284.7 msだった。
以前の停止状態からの同drive試験ではfirst block約3.12秒だったため、待ち時間の大部分をbackgroundの
start命令へ移せる可能性を確認した。この比較は同一条件での反復測定ではなく、ioctl成功だけでは回転継続を
証明しない。

最初の常駐試験では30秒周期でSTOPPED/PAUSED中に要求され、PLAYING中は抑止された。Pause/Stop後の再開、
CEC/API/technical statusにも退行はなかった。約3分停止後のPlay先読みは238 ms、別のPlayは401 msだった。
多くのstart命令は20〜255 msだったが、初回2430 msと途中2510 msの再スピンアップを観測したため、
30秒では物理的な回転維持に長すぎると判断し15秒へ変更した。15秒試験では初回2487 msの後、
2分以上にわたり20〜290 msで完了し、途中の2秒台再スピンアップはなかった。初回をTOC完了の15秒後に
出していたため、その待ち時間中の停止を避ける目的で初回だけ即時要求へ変更した。

即時要求版ではTOC・STOPPEDログの直後にstart命令を開始し、434 msで完了した。その4秒後のPlayは
232 msで先読みを完了した。約46秒のPLAYING中にstart命令は出ず、Pause直後は86 msで要求を完了した。
これにより初回background start、再生中抑止、Pause後再開を確認した。物理回転状態そのものは取得できないため、
ログは引き続き`rotation=UNVERIFIED`とする。

### 2026-09-19 設計回帰確認

direct singleの既定750/45 frame構成で、CECのplay、pause/play、next、previous、seek forward/backward、
stopを実機確認した。各操作後に先読みを完了し、pause/playでは248 ms、track変更では363 ms、
seekでは445〜463 msだった。previousでtrack 2からtrack 1先頭へ戻り、stopはtrack 1先頭へ戻った。

repeat readerでは、75 frame regionに対して容量/開始閾値を90/90 frameとした。実際に保持できるqueueは
1 blockだけであるが、`prebuffer_ready`は`queued_blocks=1 target_frames=75`となって再生、seek、next、
pause/play、stopを完了した。容量をblock単位へ変換した際に、開始待ちが保持可能なblock数を超えないことを
確認した。この小容量設定は境界試験用であり、通常設定の推奨値ではない。

metadata有効時にCDを取り出して同じCDを再挿入し、`NO_DISC`、`LOADING`、`AUDIO_READY`の後にmetadataが
再び`LOADING`から`AVAILABLE`へ遷移することを確認した。cache hitだったため外部問い合わせは発生しなかった。
LOADING中の繰り返し観測では状態遷移時だけmetadata世代を無効化するようにし、同一LOADING状態で不要に
世代を増やさない実装へ修正した。

APIの`offset_seconds`へ`18446744073709551615`をPOSTし、`400 invalid_body`を返してdaemonが継続することを
確認した。unsigned JSON値をsigned seek値へ変換する前に範囲検査する経路である。

## Kiosk導入の確認（2026-09-19〜20）

ユーザーがChromium/Cageを起動し、TVでaddress barやdesktopが見えないことを確認した。
当初はAPIが有効でなく接続エラーになったが、API設定とservice再起動後にページ表示を確認した。
日本語対応fontが未導入だったため`fonts-noto-cjk`の導入を手順へ追加した。
日本語表示の改善、CSS適用後のcursor非表示、CEC操作の画面追従、再bootとdaemon再起動後の復旧は
当時は明示的な確認待ちだった。後述の2026-09-25に日本語曲名とcold boot後の表示を確認した。
CEC操作の画面追従、cursor、daemon再起動後の復旧は継続確認とし、連続kiosk運転の保証とはしない。

レビューでTCP接続自体が待受期限を超えてblockし得る点を修正し、coreutils timeoutで
接続処理を制限した。port/時間の範囲検査と10進数変換も追加した。2026-09-20に既存CTest
25件を通過（API socket試験はsandbox外で再実行）。wrapperは模擬接続による起動引数、
接続失敗、不正設定、先頭ゼロ付き数値を検証した。この時点ではTV上での変更後の再確認は未実施だった。後述のcold boot計測でwrapperログとTV表示を確認した。

## Mac Dockerビルドと起動telemetryの実機確認（2026-09-25）

Mac上のDebian Trixie arm64 Dockerでbuild/stageし、CTest 26件が全て通過した。
`stage/usr/local/bin/cdplayerd`はELF aarch64で、Piのインストール済みdaemon・wrapper・標準UI JSと
MacのstageでSHA-256が一致した。Piではコンパイルしていない。

稼働中のPiでサービスを起動した際、両サービスはactive、kiosk unitは`After=picdplayer.service`、
`Wants=picdplayer.service`、`Conflicts=getty@tty1.service`だった。journalのmonotonic時刻では
service開始が+377432.699秒、wrapper開始が+377434.660秒、TCP readyとCage execが+377434.678秒。
wrapper内の記録は開始0 ms、API待機開始10 ms、TCP ready 20 ms、Cage exec 20 msだった。
Chromiumの`/player`はDevToolsで`readyState=complete`、接続表示`Live`、可視状態`visible`。
DevTools screenshotではアルバム画像・日本語曲名・停止状態が描画されていた。
ユーザーがTVでもNow Playing画面と`STOPPED`表示を確認した。

同一page IDのUI telemetryはscript開始+377458.629秒、DOMContentLoaded+377458.665秒、
描画機会+377460.324秒、WebSocket接続+377460.358秒、UI ready+377460.370秒にdaemonが受信した。
この試行では描画機会がWebSocket接続より先だった。wrapper stdoutはPAM session scopeで記録され、
`journalctl -u picdplayer-kiosk.service`だけでは表示されなかった。

これは長時間稼働中のPiでのservice起動であり、cold boot時間ではない。TVへの画面表示は確認済みだが、
HDMI first pixel、CEC・音声・ディスクの再生準備、再boot後の値はこの記録から保証できない。

## cold bootの起動計測（2026-09-25）

ユーザーがcold bootしてSSH接続後、boot ID `dafb840e-1092-4c20-b0a8-c1c3f4de2984` の
`systemd-analyze critical-chain picdplayer-kiosk.service`と`journalctl -b -o short-monotonic`を取得した。
両サービスはactiveで、kioskの再起動回数は0。Chromium DevToolsでは標準UIの
`readyState=complete`、WebSocket表示`Live`、表示状態`visible`、player状態`STOPPED`を確認した。

|観測点|kernel起動後のjournal時刻|
|---|---:|
|daemon service開始|+15.803秒|
|kiosk service開始|+15.820秒|
|daemon API listen|+16.916秒|
|wrapper開始|+23.637秒|
|TCP接続成功・Cage exec直前|+23.657〜23.658秒|
|systemd起動完了|+26.681秒|
|UI script開始をdaemonが受信|+45.938秒|
|DOMContentLoadedをdaemonが受信|+45.969秒|
|最初の描画機会をdaemonが受信|+47.354秒|
|`ui_ready`をdaemonが受信|+47.411秒|
|WebSocket接続をdaemonが受信|+47.412秒|

`systemd-analyze critical-chain`ではkiosk開始がuserspace +10.958秒で、
`systemd-analyze time`のkernel 4.860秒を足すとjournalの+15.820秒と一致する。
前回のユーザー計測（service開始+16.942秒→+9.914秒）は別boot・別条件の値である。
このbootでwrapper開始までservice開始から約7.8秒を要し、Cage execからUI script受信まで約22.3秒だった。
`ui_ready`はsystemd起動完了から約20.7秒後だった。

同一page IDのbrowser `client_ms`はscript開始2895.7、DOMContentLoaded3073.0、
最初の描画機会4421.5、WebSocket接続4468.8、UI ready4484.4。
WebSocket接続のclient時刻はUI readyより前だが、HTTP telemetryのdaemon受信順は逆だった。
受信順をbrowser内の発生順とみなさない。上表はdaemonへの到着時刻であり、HDMIの
first pixel、TVで実際に見えた時刻、CEC・再生準備完了を表さない。
ユーザーがこのcold boot後のTV表示を確認した。最初に画面が見えた時刻は未計測。

## 起動時間短縮の未解決課題

cold bootでTV表示と`ui_ready`受信は確認したが、起動時間の短縮は未解決である。
今回のbootではkiosk service開始がkernel起動後+15.820秒、wrapper開始が+23.637秒、
Cage exec直前が+23.658秒、UI script開始の受信が+45.938秒、`ui_ready`受信が+47.411秒だった。
特にservice開始からwrapper開始まで約7.8秒、Cage execからUI script受信まで約22.3秒を要した。
この内訳にはPAM/seat準備、Cage/Chromium起動、ページ取得・JS実行などが含まれ得るが、
現時点で各段階の支配要因は確定していない。

次回は同じTV入力・ディスク・ネットワーク条件で複数回cold bootし、
Chrome DevToolsのnavigation/paint情報、journal、必要ならTVを撮影したfirst pixel時刻を対応付ける。
`ui_ready`はHDMI first pixelではないため、体感起動時間の代用として断定しない。
原因を特定してから、Cage/Chromium/ページ側の費用を個別に評価する。
今回のtelemetry追加は観測手段であり、起動高速化の実装ではない。

## 継続する検証と開発課題

### Custom UI第一段階（2026-09-20）

`ui/default/`の6 assetは外部化前の埋め込み内容と一致し、CMake生成物と一時install先の
コピーも一致することを確認した。UI loader/route試験では通常採用、欠損・不正manifest、
version不一致、不正entry、symlink、特殊ファイル、UTF-8、サイズ・件数・深さ制限、
起動後の編集がメモリ内の採用済みUIへ反映されないことを確認した。

実際のdaemonをALSA null・存在しないCD device・CEC無効で起動する9ケースを追加した。
Custom UIなし、正常UI、directory欠損、manifest欠損/不正、entry欠損、不正path、UI/API version不一致で
APIが起動しNO_DISCを返す。不正UIではdefaultの通知、正常UIでは独自HTMLと128 KiB超のbinary asset
配信を確認した。localhostを使うAPI/起動試験はsandbox外で実施する。
ブラウザーによる見た目、実機再生と並行したCustom UI表示は確認待ち。

最終buildでmetadata/API有効版のCTest 27件を通過した。API/metadata無効版も差分buildし、
CLI検証と常駐player試験を通過した。警告修正後のloaderを含めて確認済み。

ビルドはPiのメモリ圧迫を避けるため`-j1`と一時的なコンパイラメモリ制限で行った。
前bootログがないためハング原因は未確定。Samba、設定API、runtime JS検査、hot reloadは未実装。

### その他の課題

- metadata/API有効の最新service構成で再起動から再生・API操作まで確認する。
- 非同期loggerはforegroundとjournaldの起動ログで確認済み。service停止時の終了ログとflushを確認する。
  queue overflowは通常運用では意図的に発生させず、発生時は`logger: dropped=N`を記録する。
- `CDROMSTART`一回診断、TOC直後の初回要求、15秒周期、PLAYING中抑止、Pause/Stop後再開はASUS driveで
  確認済み。別drive、長期運転、ejectとstart命令が重なった場合を継続確認する。
- LOADING中・PLAYING中のeject、重複要求、EJECT_ERROR、終了との競合を実機で継続確認する。
- 傷disc・USB reset・4秒超read stallでunderrun復旧、音の欠落/重複、操作遅延を評価する。
  API snapshot処理の遅延に伴うunderrunと自動復旧は上記で一度観測した。
  傷disc・USB障害・長時間read stallによる復旧経路の実機確認は未完了。
- direct/paranoiaの採用、性能、CPU負荷、startup/seek latencyは実測後に判断する。
- pause再開の待ち時間、buffering表示、復旧回数上限を検討する。
- mediaとPCMのdevice access直列化は実装済み。挿抜を含む実機回帰確認を継続する。
- 同じTOCの別disc識別を検討する。LOADING後に同じTOCへ戻った場合のmetadata再要求は実装・確認済み。
- 現在の独自AsyncLoggerは要件を満たしている。spdlog等との比較、Buildroot package化、binary size、
  非同期queueの満杯時挙動、runtime level変更、追加sink、rotation、ライセンスを調査し、必要性が確認できた
  段階で置換を検討する。現時点では再生経路へ影響する変更を行わない。
- metadata lookup中交換、network切断、複数候補、CAA失敗時の扱いを実機確認する。
- cache期限/総容量/破損復旧、候補選択、非1始まりtrack対応、HTTP/JSON制限の強化は未実装。
- CEC device消失後の再open、claim timeout、専有制御を検討する。
- Now Playingはdaemonが配信するsame-origin artworkを表示する。Chromium/Cage kioskのcold boot後TV表示は確認済み。
  長期継続運転と起動時間短縮を継続確認する。quiet boot・read-only root・Buildroot imageは未実装。

### Enrichment / same-origin artwork（2026-09-25）

Issue #23のPR branchで、Raspberry Pi 3の通常Audio CD（14 track）を用いて確認した。
MusicBrainz metadataは`AVAILABLE`となり、album/artist/trackをPresentation Modelへ反映した。
CAAの画像は`/var/cache/picdplayer/cover-art/<release-id>.image`へ保存され、`GET /api/state`の
`artwork.cover.url`は外部URLではなく`/api/presentation/artwork/cover`を返した。同endpointは
`200 image/jpeg`、60,740 bytesを返し、Chromium/CageのTV画面でcover artが表示された。

初回実機確認ではcache directoryをworker用optionsへmoveしたため、画像は保存済みでもlocal endpointが
見つけられず`artwork.cover`がnullになった。cache directoryをservice内に保持する修正後、上記の表示を
確認した。metadata/artworkのネットワーク障害、破損画像、disc交換中の古い画像、長期cache運用は未確認である。

Piハング時は原因を確定できる前bootログがなかった。メモリ圧迫とswap I/Oは候補であり確定原因ではない。
当時はPiビルドを-j1に制限した。現在はMac + Dockerでビルドし、Piは実機検証だけに使用する。
障害調査と実機結果の原記録は[履歴](../history/README.md)に保存する。

## #35 repeat試行根拠の初期実装（2026-09-26）

wrapperの最大8試行を固定容量で記録し、candidateと閾値到達attemptをlatest/current_playbackへ
追加公開する。A/B/B、全不一致、失敗後の一致、8試行上限、未取得時の空配列/null、JSONへの投影を
自動試験する。Docker/aarch64標準buildとCTest 34/34件成功。Piでの再生・性能は未検証。
coverage・世代別履歴・policy revision・evictionは未実装で、#35は継続中、#24はブロックを維持する。

### #35 続き: stream coverage・世代（2026-09-26）

固定128区間の和集合で採用CD frameを一意に集計し、上限時は下限を固定する。
重複・overlap・隣接区間、失敗read、容量超過、policy更新、旧世代readの除外、cancel後のreset、
JSON公開を試験した。Docker/aarch64でbuild/package生成・CTest 34/34件成功。
Pi実機の再生・メモリ/負荷は未検証。device/disc世代、詳細履歴のeviction/detail_availableは残作業。

### #35 reader/disc観測世代・履歴の追加（2026-09-26）

reader再生成の世代、disc観測世代、128件の固定容量履歴を追加。140 readで12件破棄、
保持sequence 13〜140、通常statusの履歴コピー抑制、cancel/reset、disc世代更新を自動試験した。
Docker/aarch64 build/package生成・CTest 34/34件成功。物理hotplugの完全検出とsession復元は対象外。
Piの再生・CPU/メモリ・JSON転送量は未検証。#35を完了扱いにせず、#24もBlockedのままとする。

### #35のPi実機検証（2026-09-26）

[provenance実機結果](reports/2026-09-26-read-provenance/README.md)に履歴上限、stop/reset、repeat候補、
service再起動と短時間負荷を記録した。session復元は#36で未実装、物理hotplugは#88でPending。
TV実表示・試聴・長期運転は未検証。snapshot約96KBとCDP混在CPU平均20.4%の負荷評価は#83へ引き継ぐ。

### 詳細履歴オンデマンド化（#35/#36）

通常snapshotから履歴regionsを省き、GET /api/read-historyとsession識別を追加。
Docker build/CTest34件成功。Piでstate16437 bytes、履歴128件取得、stop/reset、restartでsession変更を確認。
CDPなし25秒のCPU平均16.85%、現在throttlingなし。条件差があり性能改善率は確定しない。
詳細・rawは[provenance実機結果](reports/2026-09-26-read-provenance/README.md)を参照。
warning/gap復元は#36で未実装、#24はBlockedを維持。

### #36 snapshotによる診断復元（2026-09-26）

stream内の最後のUNCERTAINをactive_warningとして保持し、正常read/event消費で消えず、cancelで
解除されることをfake readerで確認。JSONでは旧stream eventの除外とwindowを確認した。
JS試験は古いrevisionの拒否、gapのUNKNOWN表示、stream/session変更での警告・gap解除を検証。
Docker/aarch64 build/package生成とCTest34/34成功。今回の警告・gap追加後のPi実機再接続は未検証。
完全なevent replay、停止後に残す障害警告台帳、物理hotplugは含まない。

### #36 Pi再接続の確認（2026-09-26、03b7e21）

診断画面のPLAYING/session一致、page reload、stop後の旧event消去、daemon restart後の新sessionへの
自動再接続をCDPで確認した。rawは[provenance report](reports/2026-09-26-read-provenance/README.md)。
UNCERTAINを実機で誘発していないため警告の異常系は自動試験のみ。両サービスは停止済み。
