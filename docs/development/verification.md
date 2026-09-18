# 検証状況と残課題

更新日: 2026-09-18。実装済み、hardware非依存試験済み、実機確認済みを区別する。
日付付きの測定は当該条件だけの結果である。

## 現在の確認待ち

technical statusのブラウザ確認、傷disc、cache独立性、速度変更、S/PDIF出力は未確認。
S/PDIFは[将来候補](digital-audio-output.md)であり、現在の必須試験ではない。

## 自動試験

CTestはCMakeの有効機能で件数が変わる。基本buildではcontroller、engine、ALSA抽象、CEC変換、
media tracker/worker、TOC、reader、PCM保存、snapshot、CLIなどを検証する。
metadata有効時はDisc ID公式vector、候補0/1/複数と不正JSON、worker/sessionの旧結果破棄を追加する。
API有効時はJSON schema、route、technical status asset、loopback socket、無通信時のserviceがmainへ戻る回帰試験を追加する。
paranoiaにはlibrary呼び出しをwrapした試験がある。

```sh
cmake --build build-direct -j1
ctest --test-dir build-direct --output-on-failure
cmake --build build-metadata -j1
ctest --test-dir build-metadata --output-on-failure
```

Phase 2基礎実装時にdirectの16/16、metadata/API buildのAPI以外21/21、
sandbox外のAPI socket 1/1成功を確認した。
これは全option組合せの保証ではない。socket試験はloopback通信を許可した環境で実行する。
2026-09-18のReadPolicy作業ではmetadata/API buildのAPI以外23件とsocket試験1件、
direct buildの関連4件（playback_engine/read_policy/cdda_cli/player_daemon）が成功した。
policy入力、JSON、reader再生成・region変更を確認したが、実機上の切替試聴を代替しない。
テスト名・登録条件の正規情報は[CMakeLists.txt](../../CMakeLists.txt)にある。

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
| eject待受修正 | 2026-09-17に一回の要求でトレイが開いたとのユーザー確認 |
| integrity Phase 1a | ASUS drive能力、direct read集計、先読み/ALSA再生head、bounded eventを通常CD再生中のAPI snapshotで確認 |
| ReadPolicy runtime切替 | repeatを適用して再生後、SINGLE要求をPLAYING/PAUSED中に保留し、STOPPED境界で適用。APIでrequested/effective/pendingと15 frame single readerへの切替を確認 |
| integrity Phase 1b | Macのbrowserでtechnical statusを表示。停止・通常再生、Live接続、player位置、現在再生PCM、先読みread、統計、event、drive能力を確認 |
| integrity Phase 2 buffer | direct singleで300/150、300/75、300/45、750/45 frameを比較。750/45で通常再生、操作、Mac状態表示を確認 |

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

- [読み取り信頼性の拡張設計案](../development/integrity-design.md)のPhase 1aは実装・通常CDで実機確認済み。
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

## 継続する検証と開発課題

- metadata/API有効の最新service構成で再起動から再生・API操作まで確認する。
- LOADING中・PLAYING中のeject、重複要求、EJECT_ERROR、終了との競合を実機で継続確認する。
- 傷disc・USB reset・4秒超read stallでunderrun復旧、音の欠落/重複、操作遅延を評価する。
  正常試聴では異常を再現できておらず、復旧経路の実機確認は未完了。
- direct/paranoiaの採用、性能、CPU負荷、startup/seek latencyは実測後に判断する。
- pause再開の待ち時間、buffering表示、復旧回数上限を検討する。
- mediaとPCMのdevice access直列化は実装済み。挿抜を含む実機回帰確認を継続する。
- 同じTOCの別disc識別、LOADING後のmetadata再要求を検討する。
- metadata lookup中交換、network切断、複数候補、CAA失敗・redirect修正後の取得を実機確認する。
- cache期限/総容量/破損復旧、候補選択、非1始まりtrack対応、HTTP/JSON制限の強化は未実装。
- CEC device消失後の再open、claim timeout、専有制御を検討する。
- TV向け本番UI・画像binary取得・quiet boot・read-only root・Buildroot imageは未実装。

Piハング時は原因を確定できる前bootログがなかった。メモリ圧迫とswap I/Oは候補であり確定原因ではない。
ビルドは-j1を維持する。障害調査と実機結果の原記録は[履歴](../history/README.md)に保存する。
