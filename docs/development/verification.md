# 検証状況と残課題

更新日: 2026-10-01。実装済み、hardware非依存試験済み、実機確認済みを区別する。
日付付きの測定は当該条件だけの結果である。

現在の到達点は[実機確認済み](#実機確認済み)、次に取り組む作業と進捗は
[残課題とIssue一覧](backlog.md)を参照する。末尾の継続課題は検証上の根拠として残す。
その間の日付付きの節は、条件ごとの個別実験記録である。過去のbuffer値や試験件数を
現在の仕様として使わず、設定の正本は[機能設計](../design/functional-design.md)で確認する。

## 現在の確認待ち

傷disc、cache独立性、速度変更の効果、S/PDIF出力は未確認。
Now Playingのcold boot後TV表示、停止中metadata・画像表示は確認済み。CEC操作後の画面追従や異常時表示は
[Now Playing実機確認結果](#now-playing実機確認結果)に残る範囲を記す。
S/PDIFは[将来候補](digital-audio-output.md)であり、現在の必須試験ではない。

## 複数metadata候補の選択経路（#15、Docker自動試験）

同じDisc IDに複数候補がある場合、未選択ではAudio CD fallbackを維持する。作業branchでは
`enrichment.selection`へ表示用候補とsession/disc/metadata世代を載せ、loopback限定の選択POSTで
現行discの候補だけを適用する。選択後のcover artは別workerで取得し、世代またはrelease IDが変わった結果を
適用しない。再挿入時の自動再選択は行わない。

Debian Trixie/aarch64 DockerでbuildとCTest 45件が通過した。候補選択・古い世代と画像結果の拒否、
provider固有IDを公開しないPresentation Model、API入力境界を確認した。標準PlayerのCEC候補picker、
実機『The Slip』での候補選択、実ネットワークからの選択後CAA取得は未検証であり、それぞれ#166と
#15の実機・統合確認として残る。

## Metadataとartworkの段階配信（#50、Docker自動試験）

runtimeのMusicBrainz lookupはCAAを待たずにmetadataを先に返し、単一候補または明示選択後の
artworkを別workerで取得する。metadata AVAILABLE時点のartworkはNOT_REQUESTEDで、後続の
AVAILABLE/UNAVAILABLE/ERRORはmetadataのstatusを変えない。古い世代・別releaseの画像結果は適用しない。
Docker Debian Trixie/aarch64でbuildとCTest 45件が通過した。metadata-only lookupでCAAを呼ばないこと、
後続artwork失敗と古い世代の拒否をfixtureで確認した。Pi上のnetwork遅延下での更新順序と
WebSocket観測は未確認であり、#25の統合時に確認する。

## Bounded stopped-idle drive stop（#144、Pi確認）

通常Audio CDを認識したPiで、従来のSTOPPED/PAUSED中15秒ごとの`CDROMSTART`要求を廃止した。
TOC取得後のSTOPPEDが連続5分に達すると、DriveAccessCoordinatorを通した`CDROMSTOP`要求が一度だけ
発行される。Pi journalでは`stop_command=accepted elapsed_ms=10 rotation=UNVERIFIED`を確認した。
TVでdrive spinが停止したことはユーザーが目視・聴取したが、daemonは回転状態を観測しないため、
ioctl受理と物理停止を同じ保証として扱わない。

停止要求後、loopback `POST /api/play`は204を返し、5秒後にtrack 1の`PLAYING`とposition進行を確認した。
同じrunで`POST /api/eject`は202を返し、約1秒後に`NO_DISC`へ遷移した。CD再挿入後は
`AUDIO_READY → STOPPED`、14 tracks、track 1へ復帰した。daemon/kioskは全工程でactiveだった。
この確認は通常CD、direct reader、短時間の再生復帰に限る。pause、unsupported/error、長時間STOPPED、
drive/USB bridgeごとの差異は、物理媒体・drive横断の後続検証 #146 で扱う。

## Bounded ALSA underrun recovery（#34、Docker自動試験）

同一streamでのALSA XRUN復帰を最大3回に制限した。各復帰では最後にALSAへ提出したCD frame境界から
readerを再生成し、prebuffer targetを容量上限まで増やす。4回目のXRUNでは
`underrun recovery_limit_exhausted limit=3`を記録してerror停止し、reader再生成を続けない。
play、seek、pauseなど意図したstream切替は回数をリセットする。これはread retry、read error、
Integrityの`RECOVERED`とは別の出力経路である。

Docker Debian Trixie/aarch64で`playback_engine_test`を含むCTest 44件を実行し、3回の復帰、
4回目の停止、既存の終端underrun停止を確認した。Piで意図的にXRUNを起こす確認、傷disc/長いread stall、
read error後の復旧方針、buffering UIは未実施であり、#146および#34の残作業として扱う。

同packageをPiへdeployして通常再生を開始したが、約3.45秒後に`usb 1-1-port2: over-current change`、
ASUS USB driveのdisconnect/reset、`CDDA read failed ... errno=5`を同時に観測した。これはALSA underrun
復帰に到達する前のphysical device resetであり、通常再生の成功や本変更の失敗を示すものではない。再enumeration後は
`AUDIO_READY → STOPPED`へ戻った。根拠とphysical lifecycleの追跡は#88に記録し、連続再生による再現試験は行っていない。

## Metadata JSON入力境界（#38、Docker自動試験）

MusicBrainzとCover Art ArchiveのJSON本文は既存の2 MiB/512 KiB HTTP受信上限に加え、parser callbackで
nestingを32、keyを含む文字列を4096 bytesまでに制限する。上限超過と構文不正はmetadata lookupの例外となり、
MetadataWorkerがERROR結果へ変換するため、CD再生を待たせない。Docker Debian Trixie/aarch64で
`metadata_parser` testを実行し、既存の正常/不正入力に加え、33段のnestingと4097 byte文字列の拒否を確認した。

429/503ではlibcurlが解釈したRetry-After秒数を最大15秒まで待ち、値がない場合は1.1秒待つ。
retry policy unit testで値なし、0、4秒、上限超過、負値を確認した。実際のHTTPS response headerを使う
`http_security_policy` testではCAA初期URLとredirect host、相対redirect、IPv4/IPv6のpublic/private/link-local
判定を確認した。実際のHTTPS redirect headerを使う統合試験、network切断、実機のmetadata lookup挙動は未確認である。

## Metadata cache lifecycle（#37、Docker自動試験）

metadata/CAA JSONとcover imageのcacheは、更新から30日を過ぎるとnetwork取得を優先し、取得に失敗した場合だけ
期限切れの有効entryをoffline fallbackとして使う。cache全体は64 MiBに制限し、書込み前に最古entryから削除する。
temporary file→renameによる更新を維持し、残ったtemporary fileは次の書込み時に削除する。サイズ超過、画像形式不正、
cache hitのJSON parse失敗は無効化し、JSONはnetworkから一度再取得する。

Docker Debian Trixie/aarch64で`metadata_cache` testを実行し、fresh/stale判定、容量到達時の古いentry削除、
上限超過entryの非保存、読み取り上限超過entryの無効化、残存temporary fileの削除を確認した。実HTTPを使う
stale cacheのoffline fallback、JSON破損後の再取得、書込み不能、CAA失敗、Pi上のcache挙動は未確認である。

## Metadata HTTP fixture（#135、Docker自動試験）

通常のlookupは`HttpClient`を使う。testだけは`MetadataOptions::http_get` callbackで応答を注入でき、productionの
provider選択やdaemon CLIには露出しない。Docker Debian Trixie/aarch64で`metadata_lookup` testを実行し、
MusicBrainz 429（Retry-After 0）および503（Retry-Afterなし）からの再試行、CAA JSON routing、cancel時の待機中断を
確認した。さらに、注入したconnection failureが`MetadataWorker`を通って同じgenerationの`MetadataStatus::error`と元の
error文字列に変換されること、古いgenerationのERROR結果を`MetadataSession`が適用しないことを確認した。
`http_client` testは空けたloopback TCP portへ実際にHTTPS接続し、libcurlのconnection failureが
`HTTP request failed:`例外として返ることも確認する。この試験は外部networkへ接続しない。

実HTTPS responseを使うtimeout・redirect header、Pi上の通常metadata/CAA lookupは未確認である。

## Optional paranoia license warning（#66、Docker自動試験）

`PICDPLAYER_ENABLE_PARANOIA=ON ./scripts/build-container.sh`で、対応containerのDebian
`libcdio-paranoia`がGPL-3-or-laterであること、生成binaryの再配布前にライセンス互換性と
source-offerを確認すること、通常releaseはOFFであることをCMake configure warningとして確認した。
paranoia有効構成ではpackageに`libcdio-cdda`と`libcdio-paranoia`のruntime dependencyが入り、
CTest 45件が成功した。標準のparanoia無効構成では44件が成功した。本体ライセンスの採用と
直接依存の監査文書は未確定である。

## 非1始まりTOCのmetadata対応付け（#39、Docker自動試験）

MusicBrainzの`medium.tracks[].position`はmedium内の順序である。parserは1からの連続性を検証し、
`lookup_musicbrainz_disc`は候補の曲数が実TOCと一致する場合だけ、TOC順に物理track番号へ対応付ける。
Docker Debian Trixie/aarch64で`metadata_lookup` testを実行し、track 3から始まる2曲TOCが
position 1/2の曲名をtrack 3/4へ対応付けること、曲数不一致の候補はERRORとなり選択されないことを確認した。
実機で先頭trackが1以外のAudio CDは未確認である。

## Bounded drive speed request（#8、Docker確認と通常Pi deploy）

`--drive-speed-x 1..255`を指定したdaemonは、Audio CDを認識したSTOPPEDまたはPAUSED中に、
`drive.speed_control=YES/KERNEL_REPORTED`のときだけLinux `CDROM_SELECT_SPEED`へ一度だけCD倍速を
要求する。要求はMediaWorkerとDriveAccessCoordinatorを通るため、PCM reader、media/TOC、ejectと
同時にdevice ioctlを実行しない。成功時は`drive.requested_speed_x`へ要求値を記録する。これは
ioctl受理だけを表し、適用速度・物理回転・騒音・throughputを測定しない。能力がNO/UNKNOWNならioctlを
発行せず現在の設定を維持する。ioctlが失敗した場合も成功後の状態を推測せず、`drive.speed_request_error`へ
理由を残して再生を継続する。
`drive.current_speed_x`は現行では未観測でnullである。

Linux/aarch64 Dockerで`./scripts/build-container.sh`とCTest 38件を実行した。範囲外値、transport失敗、
MediaWorkerの速度要求、JSONのnull/成功値、標準Playerの表示を自動試験した。2026-09-27に.debをPiへ
導入し、速度オプション未指定の通常設定でdaemon/kioskがactiveであること、APIが
`requested_speed_x: null`、空の`speed_request_error`、`current_speed_x: null`を返すことを確認した。
この通常deployでは速度ioctlを発行していない。

同日に`PICDPLAYER_EXTRA_ARGS`へ`--drive-speed-x 1`を追加してdaemonを再起動した。ASUS SDRW-08D2S-Uは
`speed_control=YES/KERNEL_REPORTED`を返し、Audio CDのSTOPPED/AUDIO_READY後に
`speed_request=accepted requested_speed_x=1 applied_speed=UNVERIFIED`を記録した。APIの
`requested_speed_x`は1、`speed_request_error`は空、`current_speed_x`はnullだった。その後loopback APIで
20秒再生し、PLAYING・track 1・position 1468 frame、`queued_blocks=50`、`dropped_events=0`を確認した。
停止APIは204を返し、STOPPED/AUDIO_READYへ戻った。該当journalにはALSA underrun、read failure、
main loop stall、eject errorを検出しなかった。外部album artworkを含めないCDP表示キャプチャと手順は
[drive speed request実機記録](reports/2026-09-27-drive-speed-request/README.md)に保存した。

この結果はioctl受理と短時間のAPI再生を示すだけである。音質・騒音の主観比較、CEC操作、temperature・
undervoltage、長時間再生、throughput、失敗時のdrive状態は未確認であり、既定速度との同条件比較を含めて
#8の完了条件として残る。

## Repeat overlap and cache evidence（#9、Docker/Pi通常CD確認）

repeat verifierは、最初のblock以外で直前に採用した15 CD frameを先行overlapとして再読し、出力へ
重複させずに直前PCMとの連続性を確認する。stream開始/明示seek直後は比較対象がないため
`STREAM_BOUNDARY`、一致しないoverlapまたはbackendが要求開始LBAと異なる結果を返した場合は
fail-closedでPCMを採用しない。反復一致・overlap一致ともcacheを無効化しないため、公開値は
`CACHE_POSSIBLE`であり、物理的に独立した再読を示さない。

Linux/aarch64 Dockerで`./scripts/build-container.sh`とCTest 38件を実行した。fake readerにより、同一候補、
候補不一致、read error、時間上限、最大8候補、開始LBAずれ、連続blockのoverlap一致、overlap不一致時の
PCM非出力を確認した。2026-09-28にはPi 3上で通常14 track Audio CDへrepeat policy（75 frame、2-of-3）を
適用し、15秒間に19 read / 38 candidate attempt、`MULTIPLE_MATCH`、15 frame `MATCHED` overlap、
`CACHE_POSSIBLE`、read error・verification failureなしをAPIで確認した。停止後はsingle policyへ復元した。
条件、同bootで試験開始前に観測した74,989 µs main-loop stall、確認範囲は
[通常CD実機記録](reports/2026-09-28-repeat-overlap-normal-cd/README.md)を参照する。TV実表示・試聴、傷disc、
cache軽減効果、物理的な再読込の保証、性能・長時間安定性は確認していない。

## Deterministic Integrity observation fixture（#110、Docker自動試験）

`tests/integrity_fixture.hpp`のtest-only `ScriptedCddaReader`を追加し、明示したLBA、要求/取得frame、
`ReadStatus`、native error、direct retry、backend event、local verificationを既存`PcmWorker`へ渡す。
Docker Debian Trixie/aarch64で`./scripts/build-container.sh`とCTest 39件を実行し、clean、direct retryによる
UNCERTAIN、backend fixupまたはcandidate mismatch後のRECOVERED、read error、disc mapのaccepted/unaccepted区間、
history/eventの有界evictionを確認した。fixtureはproduction daemon、CLI、package、実drive I/O、ALSA、CECを
変更せず、C1/C2、物理再read、cache独立性、read speed、傷の物理形状を再現しない。Player UIを通常rendererまで
通すlocal review harnessは#111で扱うため、今回の成功は実機異常mediaやUI表示の確認ではない。

## Local Player Integrity scenario harness（#111、Docker自動試験）

`integrity_scenario_harness`は#110のtest-only readerを、silent audio output、固定の有効TOC、固定drive
capabilityと組み合わせ、loopbackの標準Playerと`/api/state`、`/api/read-history`を公開する。通常の
`cdplayerd`、systemd、package、ALSA、CEC、実driveには分岐を追加しない。`clean`、`retry`、`recovered`、
`uncertain`、`mixed`、`read-ahead`、`transition`を選べる。`transition`はCLEAN、UNCERTAIN、RECOVEREDの
順で観測を進め、`mixed`はclean/retry/recoveredのdisc map領域を作る。これらは観測データのfixtureであり、
C1/C2、物理的な再読、cache独立性、read speed、傷の物理形状、bit-perfectを再現・保証しない。
`read-ahead`はfixture PCMをsilent outputで保留し、latest readとcurrent playback evidenceが異なる表示を
確認する条件である。正常に読み取りを継続するscenarioは同じ`PlaybackEngine`経路でfixture PCMを消費する。
`uncertain`はread errorを表示するため、通常runtimeの停止処理を進めない。

Docker Debian Trixie/aarch64でharnessを起動し、各scenarioについて標準`/player` asset、snapshot、
on-demand history/disc mapをNodeのHTTP testで取得した。これはproductionと同じ
PcmWorker → PlaybackEngine → PresentationModel/serializer → ApiServerのAPI配信経路を確認するが、
browser rendererの実行、Custom UI、Piの表示・音声・異常mediaは確認していない。manual/CDPの起動手順と
対象外は[Integrity仕様](../design/integrity-design.md#local-player-integrity-scenario-review-harness111)を参照する。harnessは
起動時にdevelopment-only性とscenario名を標準出力へ記録する。

## 通常runtime API操作とdaemon再起動（#4、Pi確認）

2026-09-28、通常14 track Audio CDを認識したdirect single readerへ、loopback APIでplay、pause、+10秒seek、
resume、stopを順に送り、すべて204を確認した。PLAYINGではpositionと50 queued blocksが進み、PAUSED/STOPPEDでは
queueが0へ戻った。`picdplayer.service` restart後にはdaemon/kioskともactiveで、APIはSTOPPED状態を返した。
各snapshotの`dropped_events`は0で、run中のjournalにはread error、ALSA underrun、eject errorを検出しなかった。
詳細と同bootで試験前に一件あったmain-loop stallは
[通常runtime API実機記録](reports/2026-09-28-normal-runtime-api/README.md)に保存した。

これはSSH/API経路の確認であり、TV実表示・試聴・CEC・Custom UI・cold boot・長時間運転・eject/drive消失は
確認していない。#4をこれだけで完了扱いにしない。

## C2 capability probe（#7、Docker確認）

起動時のread-only capability probeにMMC `GET CONFIGURATION`のfeature descriptor一覧にあるCD Read Feature（0x001e）を追加した。
CD Read Featureが不在、またはC2 Flagsを受け取れた場合だけ`drive.c2_supported`をDRIVE_REPORTEDのYES/NOとして公開する。
これはC2 error pointerの対応をdriveが報告したという意味に限り、実際のC2取得、reportの正確さ、
read integrity、bit-perfect再生を示さない。失敗または不正応答はUNKNOWNと`probe_error`に残し、
再生可能化の条件にはしない。hotplug/reset後の失効・再probeは#88の未実装範囲である。

Linux/aarch64 Dockerで`./scripts/build-container.sh`とCTest 38件を実行し、C2 FlagsのYES/NO、
Feature不在のNO、command error、不正応答でUNKNOWNを維持するunit testを確認した。

2026-09-27にPi 3 Model B上のASUS SDRW-08D2S-U（firmware F601）へ.debを導入した。daemon起動時の
read-only probeは512 byteのfeature descriptor一覧を取得し、`drive.c2_supported`を
DRIVE_REPORTED/YES、`drive.c2_trustworthy`をUNKNOWN、`probe_error`を空として公開した。
service再起動後もdaemon/kioskはactiveで、通常CD（14 track）の認識まで確認した。この結果はdriveの
C2 error pointer対応宣言を確認しただけであり、C2 reportの正確さ、傷disc上のC2観測、trust、read integrityは未確認である。

2026-09-29、`--direct-c2-pointers`を明示し、同じASUS driveと通常14 track Audio CDでAPI経由の短時間再生を実行した。起動時logは`c2_pointers=requested effective=YES support=YES`を記録した。8秒後のAPI snapshotは`player=PLAYING`、`read.stats.read_calls=75`、`failed_calls=0`、`read.latest.c2_status=CLEAN`だった。C2 packet failureへの通常read fallbackはこのrunでは発生しなかった。CLEANは読んだ75 read callのlatest区間でC2 pointerが報告されなかった観測であり、C2 trust、傷discでの検出性能、disc全体の完全性、bit-perfectを示さない。

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
[`summarize-kiosk.py`](../../scripts/summarize-kiosk.py)はplain `.jsonl`と、保存用の`.jsonl.gz`の両方を
直接受け入れる。

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

2026-09-28には、Issue #122の修正候補を導入したPiで、通常14 track Audio CDのCDP未接続PLAYINGを
30秒warm-up後に300秒測定した。system CPU平均は13.27%、最高温度は66.6°C、現在のthrottlingは0だった。
raw data、process別CPU、測定条件、限界は[5分PLAYING記録](reports/2026-09-28-post-websocket-dedup-playing/README.md)に保存した。
このpackageは測定時点で未マージのPR #123候補であり、revision・条件が過去の基線とそろわないため、
CPU改善率の比較には用いない。同runでmain loop stage warningは再現しなかったが、過去に異なるstageで
観測したwarningの原因を否定するものではない。TV実表示・試聴はこの測定の確認対象外である。

同日に、同候補で通常CDのSTOPPEDとPLAYINGを各60秒測定し、CDP未接続時の状態と10秒間CDP接続時の
WebSocket配信を[短時間のcurrent kiosk記録](reports/2026-09-28-current-kiosk-baseline/README.md)へ保存した。
修正前のSTOPPEDでは477 frame・2.16 MBを受信したのに対し、修正後はSTOPPEDで0 frame、PLAYINGで40 frameだった。
この観測は接続ごとの同一generation重複送信が抑止されたことを示すが、revision・測定時間・CDP自体の負荷が異なるため、
過去のCPU基線との定量比較には用いない。

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

2026-09-28に`PICDPLAYER_ENABLE_PARANOIA=ON ./scripts/build-container.sh`でDocker Debian Trixie/aarch64の
packageを生成し、追加の`paranoia_reader`を含むCTest 39件に成功した。生成packageは
`libcdio-cdda`および`libcdio-paranoia`のruntime依存を持つ。これはlibrary結合と自動試験の確認であり、
Pi上のparanoia再生・性能比較・試聴の確認ではない。

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

## Debian package lifecycle（#44、Docker自動試験）

`scripts/test-package-lifecycle.sh`は、`build-container.sh`が生成した唯一の`.deb`をread-onlyで
使い捨てのDebian Trixie/aarch64 containerへ渡す。同container内で旧版`picdplayer` packageを作成して
installし、旧package所有file、package非所有file、現行packageを区別する。

現行artifactのupgrade後に旧package所有fileが削除されること、非所有fileが残ること、同一artifactの
reinstall後も`install ok installed`であること、purge後に現行package所有のfile/symlinkが残らず
非所有fileが残ることを確認する。標準のPull Request CIはbuildとCTestの後にこのscriptを実行する。
このcontainer test単体ではboot済みsystemd host、稼働中serviceのrestart/stop、Piのfilesystem、失敗したdpkg操作からの
実機復旧を確認しない。後者の手順は[Mac + Docker開発手順](../manual/mac-docker-development.md#packageの更新削除を確認する)へ記録する。

2026-09-28、通常Audio CDを認識したPiでdaemonとkioskをともにactiveへ戻し、同版`0.1.0` packageを
`scripts/deploy.sh`でreinstallした。postinstのrestart後も両serviceは`active/running`、
`dpkg-query`は`install ok installed 0.1.0`、loopback `/api/state`は4516 bytesのJSONを返した。
不正なarchiveを同じremote pathへ置くと`dpkg-deb`が展開前に拒否し、既存packageと両serviceはactiveのまま
維持された。その後、正常artifactを同scriptで再deployし、両serviceとAPIが復帰した。

これは同版reinstallと展開前失敗からの復旧だけを確認する。異version upgrade、展開後またはmaintainer script途中の
失敗、power loss、package removeをPiで実行したものではない。これらの破壊的条件は、安全に隔離した実機で行う
後続検証 #147 へ移管する。

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
- pause再開の待ち時間とbuffering表示、傷disc/長いread stall時の復旧方針を検討する。同一streamの
  ALSA underrun復帰上限は3回としてDocker自動試験済みだが、実機異常系は#146で未確認である。
- mediaとPCMのdevice access直列化は実装済み。挿抜を含む実機回帰確認を継続する。
- 同じTOCの別disc識別を検討する。LOADING後に同じTOCへ戻った場合のmetadata再要求は実装・確認済み。
- 現在の独自AsyncLoggerは要件を満たしている。spdlog等との比較、Buildroot package化、binary size、
  非同期queueの満杯時挙動、runtime level変更、追加sink、rotation、ライセンスを調査し、必要性が確認できた
  段階で置換を検討する。現時点では再生経路へ影響する変更を行わない。
- metadata lookup中交換、network切断、複数候補、CAA失敗時の扱いを実機確認する。
- stale cacheのoffline fallbackと破損JSON再取得の統合試験、書込み不能、候補選択、非1始まりtrack対応、
  実HTTPS response headerを使うRetry-After/redirect統合試験、
  network切断とPi上metadata lookupの確認は未完了または継続確認とする。
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

## #35 / #36 実装済み範囲と後続検証（2026-09-26）

PR #86/#87はmasterへマージ済み（8b84acb）。#35/#36は実装済み部分と実施済み検証を
完了範囲として整理し、未完了の異常系・非干渉要件を#89/#90へ移管した。
移管時点では親#12はOpen、Player統合#24はDraftだった。移管は検証成功を意味しない。
#24の復元契約検証待ちは#90へ引き継ぐ。

### 自動試験で確認した範囲

Docker/aarch64 build/package生成とCTest 34/34成功。

- repeatの最大8試行、A/B/B、全不一致、失敗後の一致、未観測時の空配列/null、JSON投影。
- coverageの重複・overlap・隣接区間、失敗read、容量超過時の下限値、policy/stream切替。
- 128件履歴の140 read時のeviction（12件、sequence 13〜140）、通常statusの履歴コピー抑制、resetとdisc世代更新。
- fake readerによるUNCERTAIN保持、正常read/event消費後の保持、cancelによる解除。
- 旧stream event除外、window公開、JSの古いrevision拒否、gapのUNKNOWN表示、stream/session変更時の解除。

### Piで確認した範囲

[provenance実機結果](reports/2026-09-26-read-provenance/README.md)に条件とrawを保存。
通常CDで履歴上限、stop/reset、repeat候補、詳細履歴API、service再起動後のsession変更を確認した。
CDPでは診断画面のPLAYING/session一致、page reload、stop後の旧event消去、daemon restart後の
新sessionへの自動再接続を確認した。両サービスは確認終了時に停止した。

通常snapshotから詳細regionsを分離した後、stateは16,437 bytes、詳細は128件取得できた。
CDPなし25秒のCPU平均16.85%、現在throttlingなし。先行測定とは条件が異なるため改善率は確定しない。
負荷の継続評価は#83。TV実表示・試聴・長期運転、実機でのUNCERTAIN誘発は未確認。

### 移管した残課題

- [#89](https://github.com/udonchan/picdplayer/issues/89): 実際のworker queue overflow/drop、遅い診断consumer、
  eviction後のcurrent_playback根拠保持、世代切替を組み合わせた自動試験と必要な修正。
- [#90](https://github.com/udonchan/picdplayer/issues/90): UNCERTAINを伴う再接続、実dropからUNKNOWNまで、
  旧session応答等の順序境界、slow HTTP/WS client・ログsink遅延/障害時のaudio非干渉を検証する。
- #88: 物理hotplug・能力失効（Pending）。#35のreader-open観測世代とは別責務。

完全replay、停止後も残す永続障害台帳は実装していない。診断が音声を待たせないという要件は
維持しており、現在の正常系成功やqueueの固定容量だけで非干渉を証明したとは扱わない。

## #92 メッセージ契約の静的照合

master 53ddd4dの公開Presentation Model、診断serializer、API route、session側のpublish、
対応する既存テストを照合し、[現行メッセージ契約](../design/message-contract.md)へ整理した。
公開serializerのJSON key名の掲載漏れと文書の相対ファイルリンクを機械確認した。
これは型・意味の全自動検証ではなく、コードを読んだ照合と組み合わせた確認である。
コード変更・Docker再ビルド・Pi再測定は行っていない。#89/#90の残検証は維持する。

## #90 異常系診断の追加試験（PR #95）

Docker/aarch64標準build/package生成とCTest36/36成功。以下を追加した。

- 停滞するstreambufをlogger sinkに接続し、sink停止中に100 submitが期限内に完了することと、
  容量8に対して92件がdropすることを確認。書込失敗と例外設定でもshutdownが完了する。
  sinkを解放してからjoinする。無期限に停止したsinkのshutdown完了は保証していない。
- 実際のtechnical status再接続callbackを使い、RESTから警告復元、worker_dropped増加のUNKNOWN、
  古いrevision拒否、新daemon sessionでの警告解除、壊れたJSONを検証。
  旧接続のmessage/close callbackを無視するactiveSocketチェックを追加した。
- loopback HTTP 1接続/WS 7接続の受信窓を小さくし、毎回異なる512 KiBのstateを1,200回公開する。
  旧テストの同一JSON反復は送信省略されるため、継続送信負荷の根拠として扱わない。
  APIとengineを同じthreadで進め、期限内のfake audio出力増加とclient切断後の進行を確認。
  HTTP providerの実呼出しとWS 101応答を観測し、単に未接続だったケースを除外する。
- fake workerで実際にevent dropとUNCERTAINを発生させ、直近64 eventとdiagnostic_fieldsのJSONを
  Node上のUIへ渡し、UNKNOWNとstream警告を確認。event窓検査はPCM queue満杯を同期点とする。

- 実際のengineのprebuffer_readyログをglobal loggerから停止sinkへ流した状態で、API/audioを進行させる。
  queue overflowを観測し、sink解放を自動期限より前に行ったことを確認。解除時に書込失敗へ切り替え、
  audio進行と実際のostream bad状態を確認する。終了時はsinkを解放しloggerをjoinしてから復元する。
- 200回更新後を基準にRSSの最大増加を監視し、残り1,000更新で32 MiB以下という回帰検査を行う。
  今回の直接実行では増加0 bytes。固定client数/短時間の結果で、任意接続数の総メモリ上限保証ではない。
  全7 WSの101応答とHTTP provider呼出しを確認した。
- 詳細履歴を現行UIは取得しないため、Custom UI文書に世代照合の参考実装を置き、実serializer出力で
  同一世代の受理、異なるsession/stream、欠損、included=false、未知schemaの拒否を検証する。
  要求時ではなく応答到着時の最新snapshotに照合し、世代変更で保持済み表示も破棄する契約を明記する。

今回の成功は実機音声の保証ではない。実機音声・長時間運転は#4、長期負荷は#83の検証と区別する。
#24は当時Draftとして維持し、実際のPlayer側の履歴consumerを追加する場合は同じ条件の統合試験を必要とした。
Piへのdeploy・再測定は行っていない。#90の本PRは現行契約に対する再現可能な回帰検証であり、
任意接続数への防御、無期限sink停止時のshutdown完了、real-time性能を新たに保証するものではない。

### 実機異常系の担当と記録の共有

[物理媒体・USB drive実機評価 #146](https://github.com/udonchan/picdplayer/issues/146)で、観測できたread異常と
音声/操作影響、API/警告/再接続の整合を同じrun記録で確認する。backend比較は#6、特殊TOCのmetadata対応・
同一TOC識別は#39/#40、物理hotplugは#88が引き続き担当する。
試験手順・結果はreports配下の同じrunを参照し、本Issueのために媒体試験を重複実施しない。
本書の過去の未確認記述は当時の記録として保持し、実施後に確認範囲と参照先を更新する。
#146は今後の課題であり、#89/#90の自動試験成功や#24の着手条件に実機確認済みという意味を追加しない。

## #89 有界provenanceの容量・overflow検証

fake reader/outputによるplayback_engine自動試験を追加し、標準Docker/aarch64 buildと
CTest 34/34の成功を確認した。実装・メッセージの変更はない。

- 診断eventを消費せずPCMだけを300 read消費し、worker drop=44、残存event sequence=45〜300を確認。
  詳細履歴は128件、evicted=172、read sequence=173〜300、coverage=4500 CD frames。
- 取得済み履歴snapshotをconsumer側で保持してもworkerのreadが完了する。通常statusは詳細をコピーせず、
  PCM queueと詳細履歴の件数上限を維持する。これはHTTP送信やログsink遅延の試験ではない。
- overflow後のcancel/startとdisc世代変更で、drop/eviction/coverageとevent/historyが新streamに切り替わる。
- 許容最大buffer 2250 CD framesで150 read先読みし、最初の22件がevictされた状態からengineが出力を進める。
  current_playbackはread sequence=1、latestは150であり、currentの根拠は履歴から独立して保持される。
  stop後にcurrentが解除されることも確認。

既存のcoverage重複除外・容量超過時の下限値試験、旧世代in-flight read除外も同じCTestで成功した。
ネットワーク/ログ障害とUIへのdrop反映は#90、実機音声・耐久・物理交換は別検証のまま。
今回はPiへdeployせず、fake outputの進行を実機可聴性やreal-time保証と混同しない。

### PR #94 / #95 統合版の通常系Pi確認

d8aaa21のDocker buildとCTest36/36後、既存deploy手順でPiへ導入。
CDPで診断画面のLive・reload・PLAYING、API stop、daemon restart後の新session復帰を確認した。
終了時はdaemon/kioskともinactive。TV実表示・試聴・異常disc・負荷測定は未確認。
条件と結果は[通常系smoke記録](reports/2026-09-26-diagnostic-smoke/README.md)を参照。

### PR #94 / #95 レビュー修正（#97）

technical statusの設定buffer容量が非整除の場合の端数表示をworkerの整数切捨てへ修正し、
欠損counterを0にせず未取得表示とした。90/75→1 block、未取得と0の区別をJS試験に追加。
ログ障害の統合試験はwriter threadの終了で失敗を確定してからaudio進行を測る順序へ修正した。
loggerの既存assertもRelease buildで有効な検査へ置き換えた。
この追加修正はDockerで検証し、上記d8aaa21のPi結果を追加修正後の実機確認とは扱わない。

## #98 公開disc layout

Docker/aarch64 build/package生成とCTest36/36成功。公開Presentation Model試験で非1開始track、
先頭150 LBA、最終曲、TOC不在、LOADING等のnull、disc世代/session変更を確認した。
Piへ導入し通常14曲CDの`--probe-toc /dev/sr0`とREST/WSの全track半開区間・leadoutを機械的照合。
start=0、leadout=242334、disc_generation=1。REST/WSのlayoutは同一でsessionもreadと一致した。
試験後はdaemon/kioskとも停止。再生・試聴・特殊媒体・物理交換・TV表示は今回の検証対象外。
#98はPR #101、#99はPR #102でマージ済み。その後の標準Player側のmap描画は下記#24記録で実装・検証した。

## #99 Disc領域集計

Docker/aarch64 CTest36/36成功。300 readの区間結合、重複/異常/部分失敗、256区間上限と凍結、
STREAM reset後の保持・disc世代変更reset、最大256区間のdisc_map objectのJSONが64 KiB未満であることを検証（応答全体ではない）。
既存slow client/logger障害とfake audio進行試験でもdisc集計を有効にした。
この試験の非受信HTTP経路は/api/stateであり、/api/read-historyの反復取得負荷を検証したものではない。
Pi通常CDで140 read超の集計、API stop後の保持とSTREAM history reset、再開後の更新、
daemon restart後の新sessionと空集計を確認。再起動直後の接続不可は再試行して確認した。
終了時daemon/kiosk停止。試聴・異常disc・物理交換・TV表示は未検証。詳細は
[disc map実機記録](reports/2026-09-26-disc-map/README.md)参照。

## #24着手前の境界条件監査（2026-09-26）

[#103〜#106の監査記録](reports/2026-09-26-pre-integrity-audit/README.md)を参照する。
修正後のDocker build/packageとCTest37/37は成功した。終端の実機試験では
監査中の未コミット変更で146回の末尾復帰を観測して中止した。当時は修正後のPi再生が未確認で、
従前の通常disc_map試験と区別した。drain中の位置は最後の観測値を保持する。

PR #102のmaster取り込み後、修正版でtrack 1の通常再生をAPIから約13秒間確認した。
positionは230から1119 frameへ進み、read error/retry/dropped eventとjournalのunderrunは0だった。
API stop後はSTOPPED、current_playback=null、queue 0へ戻った。daemon/kioskはactive、
Chromium CDPも待受中だった。試験中にユーザーがTVの標準Player表示と音声再生を確認した。
終端drain完走は未確認であり、詳細は
[#24着手前の監査記録](reports/2026-09-26-pre-integrity-audit/README.md)を参照する。

## #24 Player Integrity UI（2026-09-27）

標準Playerへread activity、requested/effective policy、block buffer、current PCM、latest read、
stream warning、STREAM coverage、drive capability、disc read mapを追加した。mapは有効な
`disc.layout`に対して初回と新streamでcurrent PCM根拠を得た時に一回だけ`/api/read-history`を取得し、
Refresh mapで明示更新できる。通常snapshot更新ではpollingしない。map responseは最新snapshotの
session/disc generationと照合し、古いrevision・異なる世代・不正な値を表示しない。

Docker/aarch64 buildとCTest38/38は成功した。Node試験は同値snapshotのDOM更新なし、position更新、
同URLcoverの世代更新、Integrity summary、bounded map、古いrevision拒否、session変更時のmap破棄、
通常snapshotでの詳細API非pollingを確認する。

注: これは2026-09-27時点の挙動。#150の変更後はPLAYING中に2秒に1回を上限として
disc mapを再取得する。WebSocket更新ごとの取得やSTOPPED中の周期取得は行わない。

Piで14曲Audio CDを`AUDIO_READY`として確認し、APIからtrack 1を通常再生した。再生中のAPIでは
position 1203 frame、current PCM LBA 1200–1215、latest read LBA 1965–1980、132 read call、
1980 accepted frame、retry/error/dropped event 0だった。CDP画面でもread mapの1 region/revision 6と
PCM/read markerを確認した。API stop後はSTOPPED、queue 0、current/latest nullへ戻り、直近journalに
underrun、recovery、failure context、ERRORはなかった。CDP接続中の2秒測定でLayout/RecalcStyleは各8件。

これは通常disc・短時間・CDP接続中の確認である。TV目視・試聴、傷disc、物理交換、終端drain、
長期運転、CDP未接続のCPU/温度比較は未確認であり、#146/#4/#83の記録と重複しない。

### Integrity monitor Phase 1（2026-09-27）

#24のレビューで、標準Playerをprimary playbackとcompactなIntegrity monitorへ再配置した。Phase 1では
実使用で情報量を評価するため、取得できるread/drive値を折り畳まず常時表示する。primary（曲・artwork・
再生状態・進捗）、secondary（current/latest、方針、buffer、coverage、map）、diagnostic（LBA、観測窓、
能力値）は文字サイズ・contrast・spacingで区別する。これは最終デザインや常時表示項目の決定ではない。
常時PiCDPlayerロゴを外し、Integrity header内にstate API/WebSocket接続だけを示す`DAEMON · CONNECTED`等を置いた。
`CURRENT READ · CLEAN`はcurrent playback evidenceの分類であり、disc全体・原盤・bit-perfect・daemon healthの
保証ではない。current playbackとlatest readは出力PCM根拠とread-ahead観測として短く区別し、disc mapは
observed clean、retry/repeat、recovered、UNCERTAIN/backend anomaly、unobservedのlegendを持つ。

Docker/aarch64のCTest38/38成功後、Piへdeployした。通常14曲CDの短時間API再生をCDPで1920×1080に撮影し、
read monitor、disc map、drive capabilityを同時に表示した状態で`document.documentElement.scrollHeight`が
viewportと同じ1080 pxであることを確認した。同じviewportへ長いalbum/artist/track文字列を注入した表示確認でも
scrollHeight=1080 pxを維持し、albumは2行で省略された。
これはCSS layoutの確認であり、実metadata取得の網羅試験ではない。TV目視・試聴、傷disc、物理交換、終端drain、
長期運転、CDP未接続のCPU/温度比較は未確認である。

## 9月27日の変更監査（#115 / #116、9月28日再開）

対象はPR #108（UI）、#112（C2能力）、#113（速度要求）、#114（overlap）。コミットc31e49bまでを照合し、未比較overlapのMISMATCHED誤報告と拒否候補へのaccepted識別子設定を修正した。#24完了、速度要求、公開世代/履歴の古い記述を更新した。今回のPi deploy・試聴・傷disc・cache独立性の検証は行っていない。

Docker Debian Trixie/aarch64のbuild/package生成とCTest38/38成功。追加JSON試験も成功。read失敗/部分read/候補不一致でNOT_CHECKED・比較0・採用null、overlap不一致で採用null、無効時NOT_REQUESTED、seek後reset、短い最終blockの出力を確認した。

### CIと入口文書の再照合（#116）

現行CIはpull_request/workflow_dispatchでubuntu-24.04-arm上のDockerを使い、build-container.shによるbuild/stage/.deb生成後にCTestを実行する。標準構成はmetadata/API有効、paranoia無効。ローカルDockerの38/38成功とGitHub Actionsの結果は別記録であり、Pi deploy・音声・表示やparanoia有効構成の検証を意味しない。開発手順の旧34件表記、READMEのoverlap実機未確認とPlayer Phase 1掲載漏れを修正した。過去の日付付き試験件数は当時の結果として保持する。

## #155 capability-based read strategy（2026-09-29）

`--direct-c2-pointers`を要求したdirect backendについて、snapshotの`read.strategy`へrequested/effective
strategy、pending、machine-readable downgrade reasonを追加した。probe前、C2 support=UNKNOWN、support=NO、
probe結果がPLAYING/PAUSED中の既存readerへ未適用という状態を別々に表現する。既存readerはstream途中で
切り替えず、STOPPED境界で再生成する。strategyはoptionalなread mechanismの選択結果であり、C2 trust、
PCM品質、disc全体のread integrityを表さない。

Docker Debian Trixie/aarch64でbuild、stage、`.deb`生成とCTest45/45成功を確認した。strategy resolverの
single/repeat、probe pending、support YES/NO/UNKNOWN、C2非要求、およびsnapshot JSONを自動試験した。
Piでの新しいstrategy JSON確認、C2 packet failure fallback、傷disc、別drive/bridgeは未実施で、#146で追跡する。

## #34 bounded read stall（2026-09-29）

effective ReadPolicyの`time_budget_ms`を、一回のreader callをmain loopが待機し続けないための上限として適用した。
超過時はPCMを代替せずSTOPPEDへ遷移し、進行中ioctlは中断せずworker threadが復帰後にreaderを破棄する。
`read.read_stall`はinflight、timeout、直近timeoutを公開する。ALSA underrun復旧とread integrityの
`RECOVERED`とは別に扱う。

Docker Debian Trixie/aarch64のbuild、stage、`.deb`生成とCTest45/45を確認した。gateで停止するfake readerを
用い、main loopが指定timeout後にSTOPPEDへ移行し、readerの解放を待たないことを自動試験した。Piの長いread stall、
傷disc、別driveでのtimeout値の妥当性と音声への影響は未確認で、#146の物理記録を待つ。
