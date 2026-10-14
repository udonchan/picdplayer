# 検証状況と残課題

更新日: 2026-10-14。実装済み、hardware非依存試験済み、実機確認済みを区別する。

## WPEスクリーンショットCLIと表示確認（2026-10-14 23:04 UTC）

利用者から実機での表示確認ができたとの報告を得た。これは利用者の表示確認であり、厳密な切替時間・
CEC全経路・音声欠落・長期負荷の試験と同一視しない。
再利用可能なdebug toolとして既存`inspect-wpe.py`へ`--screenshot PATH`を追加する。
前回成功したWebKit inner targetの`Page.snapshotRect`を再利用し、PNG外形・寸法・8 MiB上限・
新規fileのみの保存・0600・書込失敗cleanupを検査する。serviceやSSH転送・sudoersを自動操作しない。
一時Inspectorの明示有効化/復元と、ローカル保存の使い方は[操作・診断](../manual/operations.md)へ記録する。
当初は新CLIの実機再撮影を未実施としていた。23:33 UTCの失敗は一覧HTTP取得時のConnection resetで、
MacのSSH port19223は待受中だが、Piの9223は接続拒否・専用runtime drop-in不在・kiosk未再起動だった。
agentは読み取りだけで原因を確認し、Inspectorの有効化手順を提示した。23:37 UTCに利用者から
「一旦動作確認はできました」と報告を得た。これは新CLIの基本動作の利用者確認であり、
agent自身が新しいPNG・寸法・復元状態を再取得した結果とは区別する。
Debug/Release profile分離とDebugでの標準remote debug、Releaseでのlog抑制は次セッションへ延期し、
今回の変更ではbuild profile・logger既定・Inspectorの標準起動設定を変更しない。
macOSで`python3 tests/wpe_inspector_test.py`の16件と`python3 tests/measure_kiosk_cdp_test.py`の12件、
合計28件が成功した。同じ2本をnetworkなしLinux/aarch64 containerで実行して28件成功した。
新規PNG/0600、既存file・symlinkの非上書き、不正data URL/base64/PNG/viewport、8 MiB上限、
書込失敗cleanup、CLI成功/失敗時のsocket closeを確認した。既存観測のdeadline等のassertionは維持した。
shared WebSocketは既定4 MiBのまま、明示した16 MiBだけで大きなframeを受理する試験も追加した。
`--help`と`git diff --check`は成功。変更はPython tool/testと文書で、C++ source・WPE launcher・serviceは変更していない。

## fanart.tv PoCのmaster統合判断（2026-10-14 15:05 UTC）

利用者がPR #230のmaster取り込みを明示承認した。ローカルProject keyによる実API取得、
有界な画像配信とOFF失効、実WPEの写真描画、基本再生の利用者確認を根拠とする。
master統合はfanart.tvの正式採用、key同梱許可、個別写真の再掲載許諾や全hardware条件の完了を意味しない。
未確認のCEC詳細・音声欠落・厳密な切替時間・長期負荷はIssue #229へ保持し、実装の統合と検証範囲を分ける。
PRのDraftを解除して最新HEADのCI成功を確認するフローとし、画像本体・key・元workspaceの無関係な変更は含めない。

## fanart.tvの権利確認と利用者による実機確認（2026-10-14 15:00 UTC）

利用者はThe SlipのジャケットへのCCライセンス適用を確認した。一方、fanart.tv由来の背景写真は別の素材で、
個別の権利者・再掲載可能なライセンスは未確認である。公式規約の公開本文をWordPress RESTで確認した。
投稿者へのattribution/royaltyなしの利用文言があるが、著作権は各権利者に残ることと、その権利を侵害する
利用では権利者の許可を確認することも明記されている。投稿者が写真の権利者であるとは推定しない。
公式artistページは403で、写真の原出典・個別許諾まで確認できなかった。API仕様のCC表記も写真へ流用しない。
利用者は背景のある実機画像を重視し、権利未確認のPNGを無理に公開しない方針とした。
PNGはtmpに保持し、PRには画像本体ではなく描画確認の事実・方法・残条件だけを記録する。
この調査ではAPI keyを取得せず、追加の認証付きAPI要求も行っていない。

同日15:00 UTCに利用者が実機で確認し、「再生機能等は問題なさそう」と報告した。
これは基本再生のユーザー確認であり、agentが音声を聴いた結果、全操作経路・CEC・長時間の無欠落試験ではない。
現在のCDでは背景が1種類に見えるとの報告に対し、実機APIのrevision2236でartist「大森靖子」、STOPPED、
`artwork.background.urls`が1件であることを確認した。1枚は静止、2〜3枚は45000 ms滞在・4000 ms crossfadeなので、
今回の静止表示は配信枚数に対して想定どおりである。fanart.tvに登録されている画像自体が1枚なのか、
検証条件や取得失敗で他候補が採用されなかったのかは、この状態APIだけでは判定しない。
Nine Inch Nailsでの実3枚配信・WPE snapshot確認とは分け、複数画像の実機での厳密な時間測定は未実施とする。
CEC詳細・音声欠落・長期負荷と個別写真の再掲載許諾は残条件としてIssue #229をOpenで維持する。
後続のmaster統合承認によりPRのDraftを解除するが、これらを確認済みへ読み替えない。

## fanart.tv実API・Pi配備の追補（#229、2026-10-14 UTC）

利用者がPiへProject key fileを追加し、PoC配備・設定反映・限定診断を明示承認した。
`/etc/picdplayer/fanart-api-key`はroot:picdplayer・0640・通常fileで、内容は端末外へ取得していない。
初期daemonの引数にはfile指定がなく、背景APIは`key_missing`だった。既存extra argsを保持してpathだけを
追加し、変更前の設定は`/etc/default/picdplayer.fanart-poc-backup-20261014T124854Z`へroot専用0600で保存した。
key file・homeの権限変更、sudoers変更、keyのbuild注入は行っていない。

標準`./scripts/deploy.sh`で配備し、daemon/kioskはactive→active、WPE 2.54の専用launcher稼働を確認した。
利用者はTVのpickerでThe Slip / Nine Inch NailsのUS版（2008-07-22）を選択した。
承認済み再配備後には同じTOC・候補の全field一致を確認してその選択だけを復元した。CDはSTOPPEDを維持した。

最初の背景ONは204で受理されたが参照が到着しなかった。端末内のnative診断はAPI応答本文・画像名・keyを
出さずHTTP status・件数・条件一致と承認された親directoryのみを集計した。HTTP 200、背景13件、全件が
正数の寸法・HTTPS公式CDN、artist MBID一致、本文にkey値なしを確認した。全画像は`/fanart/`直下のflat形式で、
従来のArtist MBID/artistbackground階層という仮定が全件を除外していた。music階層だけへの緩和でも0件であり、
実応答のdirectoryを確認してflat形式のみに追加対応した。型とartistはJSONを正とし、URL構造から推測しない。
確認済みflatと既存music階層をparser・image loader・native HTTPで同じ安全検査へ統一した。
HTTP/別host/query/fragment/traversal/percent escape等は拒否し、既存negative試験を弱めていない。

一時診断をservice userでhomeから実行した最初の試行はPermission deniedで停止した。
権限を緩めず、利用者の追加承認後にroot所有0755の`/var/tmp/picdplayer-fanart-poc-check`から実行した。
診断後はそこに置いたexecutableとhomeの転送用executableを削除し、応答本文・実写真・keyをGitへ含めていない。
SSH multiplexのBroken pipeと観測timeoutも発生したため、その試行を合格証跡へ使わず、新接続・unbuffered出力で再検証した。

最終flat修正を標準buildで再build・stage・package生成し、CTest **62/62成功（31.53秒）**、package lifecycle成功、
標準deploy成功を確認した。実daemonからJPEG **3枚、280462/366775/578294 bytes、合計1225531 bytes**を
同一originの不透明URLでHTTP 200取得し、hashは3件とも異なった。modelは45000 ms/4000 ms、
legacyのurlはurls先頭と一致し、全image responseに`Cache-Control: no-store`がある。
OFFで旧3参照を全て404に失効し、再ONで異なる3参照が発行された。最後は背景ON・US版選択・STOPPEDである。

WPE screenshotは当初Inspector無効・一時再起動の承認未確定で保留したが、後続の「tmpへ保存」指示で実施した。
sudo認証の期限切れ時は変更前に停止し、利用者が手元で`sudo -v`を更新した後に続行した。パスワードは取得していない。
一時runtime drop-inでkioskだけへ`WEBKIT_INSPECTOR_HTTP_SERVER=127.0.0.1:9223`を設定し、kioskを再起動した。
SSH転送も`127.0.0.1:19323`限定とし、既存PageClientのinner targetでRuntime評価と`Page.snapshotRect`に成功した。
実機WPEのrendered-page PNGは**1920×1080、1069402 bytes**で、ローカルOSのtmpへmode0600で保存した。
SHA-256は`c1d044a265bb1f5447641eebc28f01750878f85a9f9d62f9f620d7321b3ac7b9`。
PNGでNine Inch Nailsの実写真背景、The Slipのcover・metadata・STOPPED・DAEMON CONNECTEDを確認した。
2画像layerともcomplete/naturalWidth1920/naturalHeight1080で、opacityは0/0.24だった。
これは写真decodeと実WPE page描画の証拠であり、45秒/4秒の厳密な時間測定、全3画像の連続切替試験ではない。
撮影後は一時drop-inと転送用fileを削除、daemon-reloadとkiosk再起動で通常状態へ戻し、port9223閉鎖・
`picdplayer-wpe`稼働を確認した。SSH転送も停止した。daemon PID105812は不変で、再生・key設定には触れていない。
画像はtmpだけに置き、Git・PR・releaseへ含めていない。別browserの再描画やChromiumを証拠に使っていない。
この撮影時点ではDRM scanout・HDMI・TV目視、実音声/CEC継続・長期CPU/memoryは未検証だった。
後続の利用者による実機確認は上の追補に記録し、確認主体と範囲を分ける。
実keyの失効/rotation、公開同梱許可、画像license/creditは引き続きFuture Workとして保持し、
実装PRのmaster統合と、残る検証・配布条件の完了を区別する。

## fanart.tvローカルkey PoCの初期実装（#229、2026-10-14 UTC）

最新master `fb09474`から隔離した`feature/fanart-local-poc`で開発し、原workspaceの既存変更を保持した。
公式API文書の直接取得はCloudflare 403のため、同日に利用者が提供したOpenAPI本文でv3.2/music、
`api-key`、`artistbackground`、429/`Retry-After`を確認した。規約・key案内は公式WordPress REST公開本文を参照した。
仕様根拠、設定、WPE実機手順と配布Future Workは[PoC文書](fanart-tv-poc.md)に記録する。

実装は環境変数key、明示key file優先、既定OFF、有界な複数画像の段階配信、45秒/4秒slideshow、
全参照の失効、provider rate-limitのprocess内待機である。runtimeは空cache pathを維持する。
画像上限は圧縮bytesであり、full decodeのmemory上限や個別写真の許諾を保証しない。

Linux/aarch64では既存のWPE SDKを標準scriptがrecipe照合して再利用し、
`./scripts/build-container.sh`によるapplication build・staging・Debian package生成に成功した。
Enrichment/Presentation/実HTTP・WS transportの3/3試験に成功し、非同期Enrichmentとtransportは
各20回、合計40回の反復に成功した。metadata OFF/API ONのbuildとPresentation/daemonの2/2試験も成功した。
実transportは1→2→3枚のsnapshot revision更新、先頭URLの維持、全画像GET成功、OFF/artist/辞退/disc変更時の
全参照404を確認した。合成componentと画像を使う試験であり、通常daemonの実provider通信とは異なる。

macOSの5本のNode UI試験と構文検査に成功し、render試験は20/20回の反復に成功した。
0/1/複数、shuffle、段階追加、遅延decode・失敗、OFF・旧timer・disc世代、reduced motionを確認した。
同一STOPPED snapshot 60回はDOM write 0、position更新60回は72 writeの既存結果を維持した。
最終のHTML出典表示追補後も5本のNode試験を再実行して成功した。

最初の全CTestは60/62成功で、追加TLS試験のopenssl CLI依存とdaemon試験のasset URL誤りが失敗した。
daemonのURLを実際の`/player.js`・`/player.css`へ修正し、networkなしcontainerでkey/daemon 2/2を再確認した。
有効/不正な環境変数のfixture keyは状態API・HTML/JS/CSS・stdout/stderrに含まれず、No Discは維持した。
TLS fixtureは既存test keyと公開test certificateを使う方式へ変更し、openssl CLI依存を除去した。
巨大な`Retry-After`秒数をlibcurlが失うケースはnative headerでdelta-secondsを保持し、時計上限への
飽和で期限を短縮しないよう補修した。APIとCDNの独立した待機、正値/欠落/不正/0/巨大値、
同時要求の非待機拒否、実TLSの要求数と`api-key` header限定を3/3 focused試験で確認した。

最終sourceで標準buildを再実行し、全CTest **62/62成功（31.29秒）**、Debian package lifecycleも成功した。
API OFF/METADATA ON/WPE OFFのproduction `cdplayerd` buildも成功したが、全option組合せは未確認である。
staging全体のfixture API key文字列scanは一致0件だった。実keyは設定・build注入していない。

```sh
./scripts/build-container.sh
docker run --rm --network none -v "$PWD:/src" -w /src picdplayer-build ctest --test-dir build-container --output-on-failure
./scripts/test-package-lifecycle.sh
```

全差分と関連設計・backlogを確認した。別のread-only reviewでもEnrichment/model/UI/key経路に指摘はなかった。
buildには未変更の`src/api_server.cpp`のmissing-field initializer warningが残るが、今回のHTTP追加warningは解消した。

実API用`FANART_TV_API_KEY`はこの作業sessionに未設定で、実API接続を実施していない。
Raspberry Piへのdeploy、WPE/TVでの実写真表示・crossfade・可読性、取得中の実CD音声/CEC、
長時間CPU/memoryと復旧は未検証。既存のWPE稼働記録を今回の写真表示成功へ読み替えない。
[#229](https://github.com/udonchan/picdplayer/issues/229)はこれらの完了条件が残るためOpenを維持する。

## WPE Issueの完了・置換と残条件移管（2026-10-14 UTC）

利用者の指示により、既存の証拠と最新Issue本文/コメントを照合して整理した。新規実機試験・
code変更・SDK build/cache操作はこの整理では行わない。原workspaceの未commit作業、旧branch/worktreeを保持する。
文書2件をmasterへ統合する追跡Issueは[#227](https://github.com/udonchan/picdplayer/issues/227)で、
このIssueの完了と現行WPEの実機受入・供給・deploy修正の完了を区別する。

| Issue | 整理後 | 完了/置換の意味と残条件 |
|---|---|---|
| #221 | Closed / Completed | 2patch付きTrixie sourceの成立性調査、記録・review・master保全と後続CIを完了。無改変upstream・正式供給・全ABI/sandboxは#220、実機受入は#219 |
| #189 | Closed / Completed | 利用者のdirect DRM採用判断と評価/証拠の後続実装への引継ぎを完了。全runtime/v1.0受入ではない |
| #191 | Closed / Completed | 最小Pi direct DRM PoC成立と残条件移管を完了。当時の判断Bや未確認/失敗、旧checkboxを全成功へ読み替えない |
| #190 | Closed / Not planned | Cage＋Cogのlegacy比較は選択された直接WPEPlatform経路に置換。正常終了/全入力/切断復帰/同条件性能の未実施を消さない |
| #193 | Closed / Not planned | opt-in sid trialは#222の標準source-built runtimeへ置換。旧#194のtimer/SSH/復旧は現行productionの成功ではない |
| #219 | Open | 現行runtimeのcold boot・失敗/終了/復旧・DRM/child・詳細UI/入力/HTTPWS/Inspectorの未確認matrixを集約 |
| #220 | Open | 開発供給を成立済みと区別し、v1.0の依存固定/ABI/sandbox実隔離・権限・更新/正式供給へ残す |
| #226 | Open | deployの約35秒判定とkiosk API待機最大300秒の独立不一致。code・実機遅延再現は未修正/未実施 |

#219/#220本文と移管コメントを先に更新してから旧Issueをcloseした。#221の成立性5条件のみは
確認済みとして本文を更新し、元の32分失敗/2patch/資源・依存記録を保持した。#191等の全受入条件を
黙って削除・checkせず、closure commentへ移管先と限界を残した。固定資料は以下の旧Draft保全表を参照する。

性能・長期安定性は#83、boot/quiet bootは#3/#19、エラーは#188、Custom UI更新は#18/#42と関連付ける。
写真・key・decoded memory・背景保存/slideshowは#25/#49/#50/#51/#177/#178へ残し、WPE Issueへ二重登録しない。
Buildroot#74/#198〜#200は別の保留で、採用を前提にせず、#28のChromium cursorが解決したとも主張しない。
追加復旧検討は必要時まで、供給完全固定/追加ABIはv1.0段階まで保留する利用者方針を維持する。

[新規#226](https://github.com/udonchan/picdplayer/issues/226)はmaster `07bcfa1`のdeploy.sh:119–135と
kiosk wrapper:23–39を根拠に、正当な遅延起動、deadline/fallback/inactive、既存状態の維持を合成fixtureで
有界検証する完了条件を定義した。このIssue作成・整理は修正の実行やtarget設定変更を承認したものではない。

## master側の共通WPE SDK workflow（2026-10-13 UTC）

### GitHub初回seedと別PR PoC（2026-10-14 UTC）

[導入PR #224](https://github.com/udonchan/picdplayer/pull/224)のhead `3834335`は
[通常CI](https://github.com/udonchan/picdplayer/actions/runs/38493515639)が成功し、`6cfd485`でmasterへ
通常mergeした。導入jobは2時間28分33秒、build/stageは2時間27分16秒、CTestは62/62（29.75秒）、
package lifecycle・版付き候補検証/保存も成功した。SDK cache missと8833 taskのWebKit source buildを
ログで確認した。これはcold導入時間で、短縮済みの実績ではない。

master pushにより[WPE SDK producer](https://github.com/udonchan/picdplayer/actions/runs/38507076726)が
head `6cfd4859bc49df9323db3dfd4287eff8620a0bf5`、event `push`で成功した。
jobは03:36:01→05:57:13 UTCの2時間21分12秒。Build/validateは2時間21分7秒、cache saveは2秒だった。
SDKだけの初回source build/exportを完了し、アプリのbuild・試験・packageはこのjobで実行していない。

- cache ID: `8921859496`。
- scope: `refs/heads/master`。
- key: `wpe-trixie-arm64-527a5a4b9b6539ec1e2681975270f28e78b3820aab0cc682c3212a99ecdc8666`。
- size: `56218952` bytes、作成: `2026-10-14T05:57:12.03094Z`。
- restore時のmiss、SDK export成功、明示saveの`Cache saved with key`とAPIのmaster ref登録を確認した。

このseed完了後のmasterから`poc/wpe-sdk-warm-ci`を新たに作り、この記録だけを変更する新規PRで
通常CIのPoCを行う。consumer workflow・SDK recipe・アプリsourceには変更を加えない。
このPR自身のcacheがまだない最初のrunで、master cacheのrestore、SDK marker再利用、WebKitの
再configure/buildなし、アプリclean build・全CTest・package lifecycle・候補検証の成功を確認する。
初回seedが成功しただけではPoC完了・短縮済みとは扱わず、以下の実測で確認した。
既存の元workspace/worktree・branchとSDK/volumeは保持し、Pi・API key・正式releaseには触れない。

[新規PR #225](https://github.com/udonchan/picdplayer/pull/225)のhead
`9f1775a63b0c3997f98ab00e1b67913d3228147a`に対する最初の
[通常CI](https://github.com/udonchan/picdplayer/actions/runs/38520106671)が成功した。
jobは06:04:41→06:08:11 UTCの3分30秒（queue待機を含まないjob時間）。このPR/branchに事前cacheはなく、
workflow・SDK recipe・アプリ・compile/test設定を変更していない。cache一覧にはPR #225の新cacheはなく、
master cache ID `8921859496`のlastAccessedAtが`2026-10-14T06:05:19.644931Z`へ更新された。
ログのrestore sizeは同じ`56218952` bytes、同一key、`Cache restored successfully`、
`Reusing cached WPE SDK`、終了時の`Cache hit occurred on the primary key ... not saving cache`を確認した。

| 測定 | SDK状態 | job全体 | build/stage | CTest |
|---|---|---|---|---|
| #223 cold基準 | miss・WebKit source build | 2時間31分29秒 | 2時間30分12秒 | 61/61 |
| #224導入CI | miss・WebKit source build | 2時間28分33秒 | 2時間27分16秒 | 62/62・29.75秒 |
| master SDK初回seed | miss・SDKだけをsource build/export/save | 2時間21分12秒 | SDK build/validate 2時間21分7秒 | アプリ試験なし |
| #225 first warm CI | master cache hit・SDK検査後再利用 | **3分30秒** | **2分13秒** | **62/62・30.60秒** |

warmのstepはDocker build 36秒、SDK restore 1秒、アプリbuild/stage 133秒、全試験31秒、
package lifecycle 2秒、候補検証1秒、artifact保存3秒だった。cold #223に対しjob時間で
8879秒（2時間27分59秒）、97.69%短縮、43.28倍の速度となった。#223は61試験、#224/#225は
追加SDK契約試験を含む62試験であり、完全に同じcommit/試験数の統計比較ではない。
単回の異なる新規runner比較で、平均・分散・今後の全runで同じ時間を保証する結果ではない。

warmログではアプリ用CMakeを新規生成し各C++ targetをcompile/linkした。WebKitのsource path・
8833 taskのconfigure/ninja buildはなく、restoreしたSDKを使った。全CTest、Debian package
lifecycle、clean tree/版・payload/mode・依存metadataの候補検証を省略していない。
候補artifact IDは`11873392487`、名前は`picdplayer-0.1.0-2c09e586390cd65901f60e0ce175f923b95d9331`。
`2c09e...`はPRのcheckoutされたmerge refであり、PR head `9f1775a...`と混同しない。
artifact zip SHA256は`1e8b5753d9e30a0bbab007b3691f2a0122f1757d9fd96b0bbf70f03c260ff8e1`で、
7日間保存の候補であって正式release公開/承認ではない。

PoCの合格条件（別PRからmaster SDK復元・WebKit再buildなし・全検証成功・実測短縮）は満たした。
結果文書の後続commitのCIはこの最初のcross-PR測定とは別runとして扱う。#225は検証記録を保存する
docs-only PRとして保全し、このPoCを理由に追加のruntime変更や未指示のmergeを行わない。
cache消失/recipe変更でのcold fallback、正式SDK供給、全ABI/byte-identical再現性、Pi/実写真の保証は
引き続き対象外である。

### Local実装と検証（2026-10-13 UTC）

PR #223は最新head `124e07e`のCIが成功し、`ebb1c71`でmasterへ通常mergeした。旧Draft #217/#194/#192は
固有資料を固定commit参照で保全してcloseし、別途mergeしていない。元の未commit workspace、旧worktreeと
SDK cacheは保持した。今回の作業場所は同じ`wpe-primary-runtime`、branchはmasterからの`feature/wpe-sdk-cache`。

利用者が選んだ最小単位として、trusted masterでWPE SDKだけを準備・保存する専用workflowを追加する。
通常CIとcache key/pathを共有し、restore→既存SDK script検査/build→成功時saveに限定する。
master以外のdispatchはjobをskipし、PR由来codeをmaster cache producerとして実行しない。
アプリbuild・CTest・packageはこのproducerには含めず、通常CI・WPE recipe/export・ローカルSDK/volumeは
変更しない。通常PRのcache miss fallbackや正式SDK供給方式をこの単位で置き換えない。

実装開始時のcache一覧は旧WPE branchとPR #222/#223のmerge refにあり、master refにはなかった。
同じkeyでも別PR/兄弟branchのcacheはmasterの初回seedに使えないため、source buildが一度必要になり得る。
workflowのmasterへの導入、実GitHub上の初回cache save、別PRからのrestore、短縮時間はlocal実装時点では未検証だった。
API key・画像・Pi・service設定には触れず、SDKのcache成功を実機・ABI/再現性・正式配布の証拠にしない。

`python3 tests/wpe_sdk_workflow_test.py`の4 unittestが成功した。実consumerからkey/pathを取得し、
master guard・trusted trigger・read-only権限・restore/build/save順・成功後miss時saveを静的に照合する。
17種類の不正producer変異と2種類のconsumer不一致を拒否し、正常fixtureを各case後に復元する。
これは限定された文字列/step契約試験で、一般YAML parserやGitHub上の実行試験ではない。
利用可能なactionlintは確認できず、workflowは手動で構文・GitHub Actionsの条件を照合した。

`bash scripts/build-wpe-runtime.sh`は`Reusing cached WPE SDK`で成功した。recipe入力・consumer workflow・
SDK scriptは差分なしで、volume再作成・cache破棄・WebKit sourceの再buildは行わない。
`bash scripts/build-container.sh`が成功し、既存SDKを再利用して標準arm64 Debian packageを生成した。
network-noneの全CTestは追加workflow試験を含む62/62成功、package lifecycleも成功した。
独立レビューで指摘された「master cacheだけ」の不正確な説明を、自身のscopeまたはmasterのcacheを
利用する説明へ修正した。全source/文書差分を確認し、`git diff --check`も成功した。
上記はlocal開発時の確認で、実cache save/restoreや所要時間短縮の証明ではない。

2026-10-14 UTC、利用者指示により実GitHub上のPoCへ進む。導入PRの最新headの通常CIを完走してから
masterへworkflowを統合し、master scopeのSDK producerを完走させる。その後、別branch・新規PRから
通常のアプリbuild・全CTest・package lifecycle・候補artifact検証を実行し、同一PRの再run cacheではなく
master cacheを新しいrunnerでrestoreすることを確認する。初回seedは短縮後の通常CIと別に記録し、
PR #223の2時間31分29秒をcold基準に各step/全体時間を比較する。結果は実測後に追記する。

## 旧Draft PRの資料保全と統合経路（2026-10-13 UTC）

利用者の指示により、`feature/25-background-enrichment`をmaster向けの単一後継PRとして扱い、
CIと差分確認後に通常mergeする。旧Draft #217/#192/#194は相互リンクで置換先を示してcloseする対象だが、
branch/worktreeは削除しない。以後の開発を更新済みmasterから切り直すための整理で、旧PRをすべて
mergeする操作ではない。原ワークスペースの未commit作業と既存SDK cacheは保持する。

旧PRの固有資料は現在のbranchへ全コピーされていないため、次の固定commit参照で保全する。
PRのcloseはこれらの履歴・試験証拠を最新実装の結果に読み替えたり、消去したりするものではない。

| 旧PR | 保全対象と固定参照 | 現在の実装との境界 |
|---|---|---|
| [#217](https://github.com/udonchan/picdplayer/pull/217) | [元のverification全文](https://github.com/udonchan/picdplayer/blob/1f953606137b5defb616a3ffae02d9b4a57647de/docs/development/verification.md)。単一画像worker、runtime、Pi Chromium CDP、実HTTP/WS component、Draft/local review、provider/OFF gate統合の元のhash・失敗・cleanup条件を保持 | runtime/fixtureは後継branchへ再利用し、WPE build・Settings・追加回帰を合わせた。過去のPi Chromium観測はWPE実写真の証拠ではない |
| [#192](https://github.com/udonchan/picdplayer/pull/192) | [Pi 3実験報告](https://github.com/udonchan/picdplayer/blob/f2b401020c8e7bead81de743d01e6bde9e4d0f85/docs/development/reports/2026-10-05-wpeplatform-drm-poc/README.md)、[元PoC launcher・Custom UI fixture・MainPID-tree CPU/PSS測定tool](https://github.com/udonchan/picdplayer/tree/f2b401020c8e7bead81de743d01e6bde9e4d0f85/poc/wpe-platform) | 現masterに元PoC directory/reportはない。#222の標準source-build/packageへの採用とは別の実験資料。profile条件の異なるPSS値を比較結論にしない |
| [#194](https://github.com/udonchan/picdplayer/pull/194) | [元のbounded試験・rescue timer script](https://github.com/udonchan/picdplayer/blob/5f94fe7a7ff0d79ea1dcac7e756e3cf5101a8b4d/poc/wpe-platform/run-pi.sh)、[操作・復旧手順](https://github.com/udonchan/picdplayer/blob/5f94fe7a7ff0d79ea1dcac7e756e3cf5101a8b4d/poc/wpe-platform/README.md)、[元のInspector/TV/復旧検証](https://github.com/udonchan/picdplayer/blob/5f94fe7a7ff0d79ea1dcac7e756e3cf5101a8b4d/docs/development/verification.md) | #192上のstacked trialで、現在の標準runtimeへsid rootfs切替やrescue scriptを移植しない。#222で製品採用経路を置換したが、全復旧条件を検証済みとは扱わない |

統合対象は単一背景runtimeとSettings接続。API key設定、実provider認証・WPE実写真、背景保存・
slideshow・full decode memory上限は残課題とし、#25などの親/子Issueを自動closeしない。
WPEの正式供給・cold boot・長期性能・sandbox/復旧検証も旧PR整理をもって完了扱いにしない。

## 単一画像runtime試験packageのPi配備（2026-10-13 UTC）

利用者のdeploy指示により、`wpe-primary-runtime`の未commit runtime差分から生成済みの標準
`picdplayer_0.1.0_arm64.deb`を`bash scripts/deploy.sh`で`picdplayer-pi`へ配備した。
既存の限定された`sudo -n dpkg -i`権限を使い、sudoers・起動設定・key設定・背景ONは変更しない。
packageのpostinstにより稼働中daemon/kioskは再起動し、deploy scriptはactive→activeと実効WPEを
確認して成功した。既知の35秒待機問題は今回発生せず、そのcodeを修正・回避していない。

配備後の観測は次のとおり。これは写真表示や試聴の確認ではない。

- package: `picdplayer 0.1.0 arm64 install ok installed`。
- daemon: active/running、MainPID `102754`、観測時のNRestarts `0`。
- kiosk: active/running、MainPID `102778`、実process `picdplayer-wpe`、観測時のNRestarts `0`。
- launcher: `--version`は`2.54.0`。daemon・launcher・WebKit libraryのpackage所有を確認した。
- 標準UIのinstall fileと実API配信内容のSHA256はともに`aa359563f0f22a70cf5bdce57517fa6406ed817cac0d6f5fd24d7dc77355013d`で、最後のlocal Chrome試験と同じ内容。
- Read Policyは配備前後ともSINGLE、75 frames/2 matches/3 attempts/10000 ms、startup由来、persistence_configured=false。
- 背景状態はrequested/effective/availableがすべてfalse、理由`key_missing`。metadata機能は有効だが、形式上有効なproject keyの未指定・不正・読込不可を区別しないこの理由によりONできない。

同日12:28 UTC、利用者からこの配備版で実機CEC操作と再生→停止を確認し、backgroundをONに
できないことも確認したとの報告を受けた。CEC/再生→停止は利用者による実機確認済みとして記録する。
背景ON不可は配備後に観測した`key_missing`と整合し、利用不能時のgateが実機UIでも確認された。
操作した個々のCEC keyやSettingsの全navigation経路、音声品質・長時間継続はこの報告から推定しない。

serviceの起動・runtime/package同一性はPiで観測済み、CEC/再生→停止は上記の利用者確認済みだが、
有効なkeyでの背景ON、写真取得/表示、取得中の再生継続、OFF時の写真消去は実機未確認である。
写真の実機試験には、serviceが読める保護された
project key fileと起動引数を利用者が別途設定し、再起動後にSettingsで明示ONする必要がある。
personal keyのclient-keyは未対応であり、keyの値をargv・EnvironmentFile・chat/logへ置かない。
同日12:54 UTC、利用者はAPI key設定を残課題へ残す方針を示した。設定・実provider認証・写真の
WPE/Pi試験は今回のcommit/pushを待たせる条件にせず、完了済みとは記録しない。

## 単一画像runtime接続の開発差分（2026-10-13 UTC）

利用者の指示により、ここまでのSettings・画像worker部品・保持済みWPE文書を`699f627`
（`Checkpoint background settings and image worker integration`）としてlocal commitに保存した。
commit直前の全CTestは59/59成功し、直後のworktreeはclean。作業branchは
`feature/25-background-enrichment`、directoryとWPE SDK cacheは従来の`wpe-primary-runtime`を保持する。
元ワークスペースとDraft #217のworktreeは変更していない。この節は`699f627`後の開発・検証記録である。
配備と利用者による実機確認は前節を参照する。利用者の追加指示に基づきこの単位をcommit・branch
pushするが、PR作成・Git merge/cherry-pick・master統合は別の操作として行わない。

Draft #217の単一画像runtime・同一origin配信・optional snapshot参照・標準rendererを再利用する。
前回のSettingsとRead Policy保存を保持し、master/WPEのCMake全体を旧branchの内容で置換しない。
既定OFF、keyだけのprovider要求0、明示ON、OFFで協調取消・保持画像/参照の即時失効を維持する。
metadataをcompileしない構成でも`metadata_disabled`を返すよう、PlayerSessionの初期policyを合わせる。
候補と画像は空cache pathで一時取得し、disk cacheの採用・保存schema・slideshow・full decode後の
memory上限はこの単位へ混ぜない。画像の権利判断は利用者に委ねる。写真は配布物へ同梱しない。

以下の過去節の「runtime未接続・ON409」は`699f627`までの境界で、今回の開発差分と区別する。
通常daemonの実provider、WPE/Pi背景表示、実音声・実CECは未検証であり、合成fixtureやlocal Chromeを
実機確認とは扱わない。

最終差分で`bash scripts/build-container.sh`はcached WPE SDKを再利用して成功し、標準arm64 Debian
packageを再生成した。network-noneの全CTestは61/61成功。`699f627`の59件に、実HTTP/WSの
`background_runtime_transport`とCDP helperの`background_cdp_fixture`を加えた構成である。
transportは合成provider/画像応答を使い、14 snapshot revision、OFF/artist/辞退/discによる旧参照失効と
metadata/cover/backgroundの独立更新を確認した。通常daemon・実光学drive・実音声とは区別する。
`bash scripts/test-package-lifecycle.sh`も成功し、Read Policy保存・Settings回帰は維持した。

別build `build-container/background-no-metadata`ではAPI=ON/METADATA=OFF/WPE=OFFの44/44 CTestが
成功した。初回は追加key試験が非compile構成に未対応CLI optionを渡し、daemonが起動しない失敗を
検出した。CMakeをsource of truthとしてmetadata有無を試験へ渡し、非compile構成ではkey試験だけを
除外した。基本daemon/API/OFFとmetadata_disabledの検証は残し、標準構成のkey_missing・valid key・
session ON/OFF試験も通過した。API=OFF/METADATA=ON/WPE=OFFのproduction buildも成功したが、
その構成の全CTestや全option組合せが成功したとは扱わない。

macOS Chrome `155.0.8059.40`の隔離headless instanceで、合成PNG/JPEG/WebP、decode失敗→cover、
default fallback、OFF、新世代、旧callback無視、1920×1080・640×720の9ケースを確認した。
横overflow・JS例外なし。狭い画面の既存縦scrollは許可し、同じSTOPPED snapshotのDOM mutationは0。
このbrowser試験はWS stubとfixture render呼出を使い、backendの実HTTP/WS試験とは別である。
新renderer上でもSettingsの1920×1080・720×720・390×844×利用可否2状態の6ケースが成功した。
証跡はworkspace外の`background-runtime-chrome-evidence`へ保存し、画像・secretをrepositoryへ同梱しない。
独立したruntime差分レビューは具体的指摘なし。全差分と`git diff --check`を確認した。

Settingsとrendererの結合回帰では、state stream切断中にOFFが204でも読込済み写真が残る問題を
Nodeで再現した。受理時に表示側の現在参照も失効させ、旧callback/遅延modelから復活させないよう
修正した。403/409拒否では写真を消さず、REST refresh失敗時も確認済みOFFの失効を維持する。
追加レビューで、別clientのON/new URIが古いOFF応答を追い越すと新URIまで抑止するP2を検出した。
OFF応答後の新しいRESTと最新の採用済みsnapshotが同じ参照を返した場合だけ再承認し、採用済み
snapshotから描画し直すことで修正した。遅延ACKと別clientのONを含む回帰試験が通過し、再レビューで
指摘の解消を確認した。最終sourceでChromeの9 renderer/6 Settingsケースも再実行して成功した。
refresh中に新しいlive snapshotが到着した場合はRESTの古いmodelで置換しない。No Disc/null sessionの
応答を待つ間に新sessionが公開される回帰も追加し、最新のalbum/画像を保持することを確認した。
この最終sourceを再buildし、標準61/61、metadata非compile構成44/44、package lifecycleを再確認した。

runtimeの検証・配備はlocal checkpoint `699f627`後の未commit差分で行った。今回この単位を新branch
`feature/25-background-enrichment`へ保存・公開し、GitHubのDraft #217/masterは更新しない。
slideshow・背景設定保存・full decode memory上限、実provider、通常daemonからWPE/Piの写真表示・
実音声・実CECの最終統合確認は残る。

## 背景画像workerの統合準備（2026-10-13 UTC）

最新master `f0d44c1`とDraft PR #217のhead `1f95360`に変更がないことを再確認した。
同PRはOpen/Draft・masterに対して競合中であり、git merge/cherry-pick・commit・pushは行わない。
この記録は統合準備時点の境界である。その後の利用者指示により、Settings・worker部品・保持済み
WPE文書の変更をlocal commitで保存してから次のruntime単位へ進む。GitHubへのpush・PR作成・
merge・deployは別の操作であり、今回のcommit指示には含まれない。
WPE worktreeに画像workerのheader/source/fixture試験と8 MiB共有上限をそのまま再利用し、
候補workerのthread宣言順修正も再利用した。これら6ファイルは`git hash-object`でPR #217側と
同一内容であることを確認した。CMakeはworkerのcompileと試験登録だけを追加し、現在のWPE
build/package/test設定を保持する。一般provider frameworkや新たなcache方針は導入しない。

この単位はruntime未接続の部品統合である。背景policy・EnrichmentService・画像API・snapshot・
rendererには手を加えず、既定OFF・ON409、key指定だけのprovider呼出0、前回のSettings差分を
維持する。試験の取得経路は合成HTTP応答と空cache pathであり、実写真・実provider・secretに
依存しない。8 MiBは取得/返却bytes上限で、full decode後のmemory上限ではない。
背景設定の保存・slideshow・runtime画像配信・WPE/Pi表示は今回の完了条件に含めない。

今回の最終sourceで`bash scripts/build-container.sh`を実行し、アプリ・worker・標準arm64 Debian
packageのbuildが成功した。WPE SDKは再利用し、WebKitの再buildやcache破棄はない。
network-noneで全CTest 59/59成功。これはmasterの58件と画像worker単体試験1件であり、同じ59件
だったDraft #217の試験構成とは異なる。runtime transport/CDP試験はまだ取り込んでいない。
`artist_background_worker`と`artist_background_image_worker`をそれぞれ100回反復して成功し、
`bash scripts/test-package-lifecycle.sh`も成功した。SettingsとRead Policy保存の既存回帰試験も通過した。
新規3ファイルを含めてsource/差分を確認し、初期化順・serial失効・取消/shutdown・上限・CMake登録を
照合した。独立レビューの成功は今回の検証結果に含めない。

次のruntime単位では既存PR #217のpolicy/service/API/presentation/rendererを一緒に扱う。
静的照合では、同PRのAPI=ON/METADATA=OFF時の利用不能理由についてdaemon試験の期待値
`metadata_disabled`と実装の`runtime_unavailable`に差がある。今回そのcode/試験は取り込んでおらず、
後続の統合時に実行して解決する必要がある。通常daemon・実provider・WPE/Pi表示・音声・CECは未検証。

## 背景機能の再開時棚卸し（2026-10-13 UTC）

GitHubの最新masterは`f0d44c1`。PR #174/#175/#176/#213はmerge済みで、Artist識別、
metadata先行・後続cover、provider/画像取得部品、既定OFF・利用不能APIは作り直さない。
一方、[Draft PR #217](https://github.com/udonchan/picdplayer/pull/217)はOpenで、単一画像の
memory-only取得、世代失効、同一origin配信、標準Playerのfallback、session ONを実装済みだが
master未統合。再確認時のGitHub状態は`CONFLICTING`/`DIRTY`で、今回その競合解消・mergeは行わない。
以下の過去記録にある「runtime未実装」はmasterとの境界または当時の状態であり、
未統合branchの実装が存在しないという意味ではない。#25/#49/#50/#51/#177/#178はOpenを維持する。

古い#25/#49/#51本文の製品側包括的許諾確認gateは現在の方針では採用しない。画像の権利・許諾・
表示条件の判断は利用者に委ね、製品は保証せず、既定OFF・明示ON・外部写真非同梱を維持する。
PR #217の一時的memory経路と、PR #176のfile cache部品の試験済みは別であり、ディスク保存の
採否・保持・削除を決定したことにはならない。project keyの`api-key`とpersonal keyの`client-key`も
区別する。#164、deploy待機時間問題、WPE復旧・正式供給精査は今回の実装に含めない。

今回の最小単位は#178の背景Settings。既存4 field APIへ接続し、利用不能・UnknownではONを
有効にせず、Read Policyと独立して表示・要求する。背景保存schemaは変更しない。
背景runtime・slideshowを再実装せず、通常daemon/WPEでの背景の実機確認も行わない。
作業場所は`wpe-primary-runtime`で、開始時点の4文書の未commit変更を保全する。
元ワークスペースとDraft #217のworktreeは変更しない。標準buildのWPE SDKはcache再利用を確認した。

背景Settingsのfixtureでは利用不能時のON無効とOFF要求、技術的に利用可能な場合の明示ON/OFF、
204後の再取得、403/409/500・通信失敗、不正/欠落field・未知理由、閉じる/再表示時の古いGETと
処理中POSTの重複防止・再取得をNodeで確認した。既存のRead Policy、metadata候補、transportの
試験も維持する。初回にはfake BackがHTMLと異なりdisabled、再接続後のhelperが旧socketへ入力を
送るfixtureの不整合を検出し、実DOM/現在のnavigation入力へ合わせて修正した。

`docker build -t picdplayer-build .`は既存layerを再利用し、`bash scripts/build-container.sh`で
WPE SDKを再利用してアプリ・埋込UIを再build、arm64 Debian packageを生成した。再build後の
`docker run --rm --network none -v "$PWD:/src" -w /src picdplayer-build ctest --test-dir build-container --output-on-failure`
は58/58成功、`ui_controls_javascript`の10回反復と`bash scripts/test-package-lifecycle.sh`も成功した。
実装差分の独立レビューで具体的な問題はなく、全diffと`git diff --check`を確認した。
通常CIに実provider・secretは不要。
未変更の`ApiCommand::session_id`省略初期化warningは残り、今回の範囲へ修正を混ぜない。
package lifecycleの削除時warningは、無関係なfile/directoryを保持する試験の期待された結果である。

macOS Google Chrome `155.0.8059.40`のheadless/CDPでは、1920×1080・720×720・390×844を
利用不能/利用可能の合成API各2状態で計6ケース確認した。横overflowなし、dialog/surfaceはviewport内、
有効な背景選択肢とBackへTabで到達でき、JS例外と明示選択なしの設定POSTは0。
狭いviewportではsurface内の縦scrollを許可し、focusとBackの到達を確認した。外部画像は禁止し、
WebSocketはstubである。これは標準UIのlocal browser試験で、WPE、Pi、provider、実serviceの証拠ではない。
背景保存、slideshow、通常daemonからWPE/TVへの背景写真表示・音声・実CECは未実装/未検証である。

## WPE移行のmaster統合とレビュー（2026-10-13 UTC）

PR [#222](https://github.com/udonchan/picdplayer/pull/222)をmaster `f0d44c1`へmergeした。
PR CI [38327626644](https://github.com/udonchan/picdplayer/actions/runs/38327626644)は
03:06:42 UTCにSUCCESS、mergeは03:07:43 UTC。以下の「CI未確認」は当時の履歴である。
レビューではdeployの約35秒待機とkioskの最大300秒API待機設定の不一致を確認した。
API起動が遅い有効な設定でdeployが早期失敗し得るが、修正・遅延起動の再現試験は未実施。
今回の文書更新ではコード変更も実機操作も行っていない。

## WPE標準packageのPi更新・CEC操作確認（2026-10-13 UTC）

利用者が標準deployを実行して成功し、TVリモコンでアルバム選択→再生→停止を操作して
確認した。先の手動tar配置とは別に、標準package経由での更新後の正常操作を確認したもの。
確認後のsudoなしSSH観測では`picdplayer 0.1.0`が`install ok installed`、WPE libraryとlauncherは
`dpkg-query -S`でpicdplayer package所有だった。kioskはactive、MainPID 101577は
`picdplayer-wpe`、Web/Network processが存在し、Cage/Chromium processは検出されなかった。
利用者の表示・リモコン操作確認とagentのprocess/package観測を区別する。
長押し等のCEC網羅確認、cold boot、異常終了fallback、DRM解放/再起動、長期安定性、
sandbox隔離の有効性は未確認。追加依存精査・完全固定のv1.0までの保留は維持する。
利用者は開発中のWPE主runtime運用を優先し、復旧の追加検討は必要時に再開する方針とした。
既存fallbackと明示Chromium選択は残すが、実機復旧試験を現段階のcommit/取り込み条件にはしない。

## 標準WPE build/package/deployの統合（2026-10-12 UTC）

標準`build-container.sh`はWPE kioskをONにし、CMake install規則でlauncherと
`/opt/picdplayer-wpe/2.54.0`のruntimeを同じpackageへ収める。header/pkg-configは除外する。
`build-wpe-runtime.sh`はsource/patch/設定hashでSDKをcacheし、cache miss時だけ専用Docker
volumeでconfigure/build/stageする。BuildKit attestationの変動で再compileしないよう、volume側は
imageのRootFS layer一覧を比較する。既存試験volumeを使った生成と、続くSDK cache hitを確認した。
通常build imageにlauncher用GBM/DRM/XKB headerとruntime依存を追加し、標準package生成が成功した。
CPack/shlibdepsが外部依存を生成し、bubblewrap/xdg-dbus-proxyだけを明示追加する。

deployは2.54.0の導入を確認し、active kioskのMainPIDが期待runtimeへexecされるまで最大35秒確認する。
fallbackをWPE成功とせず、inactive kioskは起動しない。Chromium明示運用は期待値を指定できるが、
target設定は変更しない。sudoersは既存の固定dpkg installだけで足り、追加権限は要求しない。
packageは既存/etc設定を変更せず、依存不足時のapt導入・設定変更は利用者が行う。

Linux/aarch64の全CTest 58/58と、使い捨てcontainerのpackage install/upgrade/reinstall/purgeが
成功した。packageにはlauncher、Web/Network/GPU process、InjectedBundle、Inspector資産を含み、
SDK header/pkg-configを含まないこととlauncher versionを検査した。deploy mockは実際のremote
検証shellを実行し、fallback/exec未完了/明示Chromiumを区別する。CIの実行は未確認であり、
SDK Actions cacheと初回source build向け180分上限を設定した。
この実装・container検証時点ではPiへの新package deploy・sudoers変更・service変更は行っていない。
先の手動配置による表示/再生確認と、新packageでの更新確認は別である。
完全固定・clean rebuild・security/license精査のv1.0までの保留方針は維持する。

## Source-built WPEのPi切り替え確認（2026-10-12 UTC）

checkpoint `c0fe77e`のlauncher/wrapper/unitと、2patch付きWPE 2.54.0の成果物を転送した。
bundleは`wpe-2.54.0-c0fe77e.tar.gz`、転送前後のSHA256は
`be2f64426c489bd000eac974ab6e786e05561822ad799beacd8d142076732eec`で一致した。
利用者がsudoによる配置・設定・kiosk切り替えを行い、TVの画面表示と再生に問題がないことを
確認した。runtimeは`/opt/picdplayer-wpe/2.54.0`、launcherは
`/usr/local/libexec/picdplayer-wpe`。daemonとホストlibraryはこの切り替えで置き換えていない。
Chromiumのbinaryは復旧用に残し、従来wrapper/unitは
`/var/backups/picdplayer-wpe-c0fe77e`へ保全する手順を提示した。

確認後のsudoなしSSH観測ではkioskはactive、MainPID 101044は`picdplayer-wpe`、
`WPENetworkProcess`/`WPEWebProcess`が存在し、Cage/Chromium processは検出されなかった。
従って今回の表示確認はChromium fallbackではなくWPEでの初回正常経路の実機確認である。
利用者による実表示/再生確認と、agentによるprocess観測を区別する。
CECの網羅的操作、cold boot、異常終了後fallback、DRM解放/再起動、長期性能、sandbox隔離の
有効性は未確認。追加依存精査/完全固定のv1.0までの保留方針は維持し、#219全体を完了とはしない。

以下のChromium稼働・Pi未確認という記録は、切り替え前の各調査時点の状態である。

## Trixie WPE source build成立性調査（#221、2026-10-12）

[#221](https://github.com/udonchan/picdplayer/issues/221)を作成し、公式WPE WebKit 2.54.0 sourceを
SHA256 `efa9bcc3cb891c2d88f50eec710d9ccee71cbdf1040420361eb98c17355eb452`で固定した。
専用Dockerfile/scriptと[再実行手順](../manual/build.md#trixieでのwpe-source-build成立性調査221)を
追加し、Trixie/arm64 containerのnetwork none、6 GiB、2 CPUでconfigureが成功した。
DRM・bubblewrap sandboxはON、動画/Web Audio/WebCodecs/GStreamer等はOFFの試験構成である。
初回のLibXslt不足はTrixieの`libxslt1-dev`追加で解消し、GStreamer要求は省略方針に合わせて
明示OFFとした。初回configure時点ではsid依存の追加、glibc置換、upstream source patchは行っていない。

baseは`debian:trixie`、解決digestは
`sha256:9cc080028c43b27d2074d63a5f9caf7166d731494965616c1a6d2827a004585c`。
configure成功時のprobe image IDは
`sha256:6e050b80b9732c15f5e85ef306112f9816d72e0fb11e7d16c8c60a09d461ecc0`。
GCC 14.2.0/CMake 3.31.6、GLib 2.84.4、ICU 76.1、libinput 1.28.1、libdrm 2.4.124、
GBM 25.0.7をconfigureで検出した。base/apt snapshotの完全固定は未実装で、依存一覧は
`picdplayer-wpe221-build` volumeの`/work/logs/packages.tsv`へ取得時点の版を保存する。

本体buildはcontainer `picdplayer-wpe221-compile`で開始し、初期のC/C++ compileまで進んだ。
初回build開始時点ではbuild完走、staging install、launcher compile/link、Pi実機は未検証だった。
出力とlogは専用volumeに保持し、既存Buildroot volume、標準build image、Pi serviceは変更しない。

初回buildは14:34:35Zから15:06:17Zまで約32分実行し、exit 1、containerのOOMKilled=falseだった。
WebCoreの`UnifiedSource-JSBindings-27.cpp.o`で`JSHTMLMediaElement`未宣言により失敗した。
2.54.0の`JSHTMLMediaElementCustom.cpp`には`ENABLE(VIDEO)`のguardがなく、VIDEO=OFFで
生成されないclassを参照していた。初回logはworktreeのGit除外directory
`build-wpe221-logs/`へコピーして保持した。
`scripts/wpe-source-video-disabled.patch`で該当コードにVIDEO guardを追加し、fuzz 0で適用した
試験image `sha256:12ba7820e1ab16ebcec1d83b2ac7470006f18e12cd8abe49c2d88ec530d3e04f`を生成した。
container `picdplayer-wpe221-compile-p1`で同じvolumeから再開した。この再試行はupstream無改変の
成立証明ではなく、明示した小patch付きの成立性調査である。依存/OS/sandbox設定は変更していない。

再試行はWebCore compileを通過したが、WPEWebKit shared libraryのlinkで`seccomp_*`未解決に
なった。libseccomp検出とsandbox ONをcacheで確認した一方、実link responseにはseccompがなかった。
同じ設定の明示再configureでも再現したため、`scripts/wpe-source-sandbox-link.patch`で
WPE/sandbox有効時に最終WebKit targetへLibseccompを明示linkした。依存解決を隠すために
sandboxを無効化せず、追加patchをfuzz 0で適用した。

2つのpatch付きimage `sha256:be055972c5626d66c351669d74e6b79f24557a202577387b1b7f74565f803554`
で増分buildがexit 0となり、WPEWebKit、Web/Network/GPU process、InjectedBundleのlinkが成功した。
staging installも成功し、既存`src/wpe_kiosk.c`を`-std=c11 -Wall -Wextra -Wpedantic -Werror`で
compile/linkできた。2つのpkg-config moduleとlauncher `--version`はすべて2.54.0を返した。
再実行用`verify` actionを追加した最終image IDは
`sha256:b6c4a721567dc825255e33a713e25acbe218b5a19a84c37d10c8367f42b1bb4d`。
同actionでlauncherとWeb/Network/GPU processの`ldd -r`を機械検査し、Trixie container内で
`not found`/`undefined symbol`がないことを確認した。stage/log/binaryは専用volumeに保持する。

結論は「現行Trixieの依存だけで、明示した2つの小patch付き省略構成をソース供給できる」である。
今回の根拠ではOS移行やsid由来のglibcは不要。無改変2.54.0の成功、最終recipeのclean rebuild、
bit-identical再現性、Pi側の依存版照合、DRM実表示、sandbox実動作、音声/CEC、性能、正式package、
security/license運用は未確認であり、#220/#219の完了ではない。

続いてsudoなしでPiとbuild imageの主要依存36packageを照合し、全件導入済み、30件の版一致を
確認した。差異はPi固有版のlibc6/libdrm2/libgbm1/libinput10と、Pi側のrevisionが一つ古い
libpng16-16t64/xdg-dbus-proxyの6件。symbol/ABI互換性の保証とは扱わないが、現時点で明確な
導入不足は見つからなかった。Piのpackage更新・service変更は行っていない。

利用者の方針として、開発は今回のWPE 2.54.0・2patch・設定と成果物を基準とする。
追加の依存/ABI精査、最終recipeのclean rebuild、供給環境の完全固定・再現性・正式配布の検討は
v1.0確定段階まで保留し、開発や初回実機試験の前提にはしない。具体的不具合が出た場合だけ
必要な範囲を調査する。Buildroot検討再開時に最終固定を評価するが、採用を確定したことにはしない。
成果物の識別と安全な実機試験/復旧は維持し、#219の実表示・操作確認を次の段階とする。

## WPE主runtime移行候補の再開（2026-10-12）

`feature/wpe-primary-runtime`の未コミット実装を引き継ぎ、前回未完了だったInspector診断を検証した。
WebSocket transportに任意の総deadlineを追加し、WPE診断では接続開始から25秒を適用する。
handshakeの断片受信、外側ackを待つ間の連続event、frameの断片受信でもdeadlineを更新せず、
JavaScript評価例外を観測成功として出力しない。従来のChromium測定はdeadline未指定の動作を維持する。
手順と制約は[操作・診断](../manual/operations.md#wpe候補のinspector診断)に記載した。

macOSでInspector 8件とtransport 10件、標準Debian Trixie/Linux/aarch64 containerでbuild・CPack、
全CTest 58/58、使い捨てcontainerのinstall/upgrade/reinstall/purgeを確認した。
標準buildの`ENABLE_WPE_KIOSK`はOFFで、生成packageにWPE launcherは含まれない。
この成功はWPE有効build、実WebKitとのInspector接続、systemdの復旧動作、DRM/表示/音声/CECの
実機検証を意味しない。protocol試験はmockであり、実WebKitのwire互換性は未確認である。

今回のsudoなしSSH確認ではdaemon/kioskともactive/running、kioskのMainPID 86772はCageで、
子processはChromiumだった。WPEへの本番切り替え・deploy・sudo操作は行っていない。
再現可能なWPE供給は[#220](https://github.com/udonchan/picdplayer/issues/220)、本番serviceと復旧は
[#219](https://github.com/udonchan/picdplayer/issues/219)で追跡し、移行の完了とは扱わない。

## Buildroot研究成果の保全（2026-10-12）

採用は時期尚早として保留し、#195/#196/#197/#201の調査文書と#202/#203の検証結果のREADMEを
現在のmaster上の文書専用変更へ集約した。参照は[保全索引](backlog.md#buildrootの検討成果保全と採用保留)と
親[#74](https://github.com/udonchan/picdplayer/issues/74)。元PR/remote branch/headを保持し、
試作Dockerfile・BR2_EXTERNAL・WPE recipeはmasterへ採用しない。元コードへの相対リンクは固定SHAのURLへ変更した。
各文書は実施日とrevisionに対応する過去の事実で、当時の「build継続中」は現在のprocess存在を意味しない。
今回Buildroot/image/WPEのbuild、生成image/volumeの再確認、Pi起動・配備・音声/CEC/DRM試験は行っていない。
過去のimage生成/package build/configure成功を、実機・正式供給・Buildroot採用の成功とは扱わない。
local/remote research headは一致し、別package branch `173e088`は#202のhead `4129757`の祖先である。
元workspace・旧worktree登録・#217・WPE #192/#194を変更せず、必要な文書だけを独立worktreeで整理する。

## artist background providerのmaster同期（Draft PR #176）

2026-10-11、PR #182反映後のmaster `9745739`を独立worktreeへ取り込み、HTTP socket callback、
HTTP試験、backlogの競合を解消した。fanartのkey/host制限・redirect拒否・proxy無効化と、
CAAのpublic peer制限、test専用CA/DNS/private peer指定を両立させた。
Debian Trixie/aarch64の正式build、CTest 55/55、使い捨てpackage lifecycleが成功した。
統合したHTTPS fixtureはnetwork noneで10回連続成功した。既存の非背景機能・新しい試験登録は維持した。
key未指定なら背景workerは作らない。今回は実provider通信・Pi deploy・TV表示を行っておらず、
この同期時点では画像のruntime取得・API配信・Player表示・背景ON/OFFが残課題だった。
後続のOFF gateと、画像の権利判断を利用者に委ねる方針確認は下記に記録する。
Draftを維持し、この同期を#49/#25の完了とは扱わない。

## metadata HTTP試験のmaster同期（PR #182）

2026-10-11、PR #212反映後のmaster `42729cd`を独立worktreeで取り込み、CMake・README・backlogの
競合を解消した。新しいcontrol inventory、release artifact、Read Policy試験を維持し、Python必須化と
loopback HTTPS fixtureを保持した。Debian Trixie/aarch64の正式build、CTest 50/50、使い捨てcontainerの
package lifecycleが成功した。HTTPS fixtureはnetwork noneでも10回連続で通過した。
testing ONでPythonを無効化したconfigureは期待どおり拒否され、testing OFFでは成功した。
生成flagsでもHTTP test hookは`http_client_test`だけにあり、製品daemonにはないことを確認した。
今回Piへのdeploy・再試聴・TV確認は行っていない。以下の既存実機記録は当時の条件のまま保持する。

## Debian control inventory（#67、PR #212）

2026-10-11、Solが未採用の独立関数を補修した。元oracleの期待値を変更せず、整数mode、
directory処理順序、重複・非regular拒否、TarError、実package・script非実行の11試験を確認した。
正式Debian Trixie/aarch64 Docker buildとCTest 50/50が成功し、実CPack artifactのcontrol archiveも
inventory化できた。Qwen初版の失敗とSol補修は[付録](reports/2026-10-08-local-agent-trials.md)で区別する。
既存manifestへの接続・copyright情報・Pi実機検証は今回の対象外で、#67は未完了。

## SettingsのRead Policy出所表示（#177/#178）

2026-10-08、`GET /api/read-policy`のrequested/effective sourceをSettingsで独立表示するよう補修した。
local試行の未完成差分をSolが整理し、既存のPOST拒否・フォーカス検査を復元した。
macOS NodeでUI boot/render/status/integrity/controlsの5 script、正式Debian Trixie/aarch64 Docker build、
CTest 49/49を確認した。controls試験は4種類の出所、欠損・未知・非文字列、保存先指定からの非推測、
POST受理/拒否、読込中、GET失敗、旧GET応答の破棄を確認する。
Pi・CEC実操作、TV/狭いviewportの実表示、再起動後の復元は今回未検証で、#177/#178全体は未完了。
試行の事実・限界と未適用の規約案は[付録](reports/2026-10-08-local-agent-trials.md)に記録した。

## 版付き候補artifact（#45、比較案の統合）

2026-10-08、Debian Trixie/aarch64 Dockerで正式build、CTest 49/49、使い捨てcontainerのpackage
lifecycleを確認した。実際のCPack `.deb`をstageと照合してmanifest付き候補を生成した。
artifact専用試験20件は実Debian archiveで版・tag・package・arch・Depends・payload・mode・symlink差異、
過去候補の無効化と生成先symlinkの拒否を検査する。Draft PRの初回CIではvalidatorまで成功したが、
root所有の一時directoryのmode 0700が残り、runnerがmanifestを読めずupload前に失敗した。
生成先を0755、保存fileを0644としてrunnerが読めるよう修正し、権限の回帰試験を追加した。
Sol比較案[PR #206](https://github.com/udonchan/picdplayer/pull/206)の最終CI run 37710332784は成功し、
取得artifactのJSON・SHA256・manifest配置・merge commitの識別を確認した。
local比較案[PR #207](https://github.com/udonchan/picdplayer/pull/207)もCIは成功したが、単独validator失敗、
版/tag/stage比較・新テスト登録の欠落、temp directoryの入れ子が残ったため採用しない。
統合案はSol案を土台に、local案の依存group配列という表現を原文保持付きで取り込み、
代替依存の保持と成果物直下の配置を回帰試験で確認する。比較記録は両PRに残す。
統合worktreeでも正式Docker build、CTest 49/49、artifact専用試験21件、package lifecycle、
実packageの二回連続finalizeを確認した。GitHub上の統合版は統合PRのCIで別途確認する。
公開release、ライセンス監査、byte-identical再現性、Pi deploy・実機動作は未検証で、#45全体の完了ではない。

## Player設定画面の第一段階（#178、作業ブランチ）

標準PlayerにCEC/keyboard/pointerで開けるSettings領域を追加し、既存Read Policy APIの
SINGLE/REPEATを選択できるようにした。`requested/effective/pending`と詳細4値を表示し、
取得・変更失敗でも再生は継続する。Artist Backgroundは利用不能と明示してON操作を提供しない。
Debian Trixie/aarch64 DockerのビルドとCTest 48件を確認した。PiのChromium上でCDPから
1920×1080のSettings画面、overflowなし、Read PolicyのREPEAT→SINGLE反映を確認した。
TVではSettingsを表示し、リモコンの物理「戻る」キーでPlayerへ戻れることを確認した。
修正版BackのCEC方向・決定操作と、実serviceでの保存後の再起動復元は未確認。
Mac headless Chromeのmock APIでは
[1920×1080と720×720の表示記録](reports/2026-10-02-player-settings/README.md)を保存し、
Settings surfaceがviewportへ収まり、内部scrollを必要としないことを確認した。
PiでのCDP結果も[同じ表示記録](reports/2026-10-02-player-settings/README.md)に追記した。
TVでの確認ではSettingsのBackボタンへCEC方向キーで移動できないことが判明した。
リモコンの物理「戻る」キーではPlayerへ戻れることをユーザーが確認した。
Backを3番目の選択対象に追加し、CECの方向・決定で閉じる回帰試験を追加した。
修正版はPiへ再デプロイ済み。BackのCEC実操作は未確認である。

日付付きの測定は当該条件だけの結果である。

## 利用者設定の保存（#177、部分実装）

`--settings-file`を明示したAPI有効Playerで、Read Policyの5項目をversion付きJSONへ保存し、
起動時に復元する経路をPR #179で実装した。Docker Debian Trixie/aarch64ではビルドと
47件のCTestを通し、`read_policy_store` testで保存/復元、無効値の拒否、破損・旧schema・
symlink・保存先directory欠落を確認した。`read_policy_persistence` testでは実daemonのloopback APIで
変更→再起動後復元→破損時fallbackをドライブなしで確認した。Piでも2026-10-02にaarch64 binaryを
一時directoryへ転送し、`/nonexistent` drive・null ALSA・CEC無効・別loopback portの独立processで
同じAPI試験を通した。稼働中serviceはactiveのままで再起動せず、試験用ファイルは削除した。
Piの**systemd service userでの保存権限**・実service再起動後復元とCEC設定画面は未確認。
設定画面の第一段階はPR #180で実装し、masterへ統合済み。実serviceでの保存・復元は未確認。
同じ試験で`requested_source`/`effective_source`がstartup→saved→restoredとなり、
破損時にstartupへ戻ることを確認した。保存先を指定しないAPI変更は`session`を返す。
2026-10-06には実daemon試験へ、保存値が起動引数より優先する条件と、保存先directory欠落による
POST 409後にrequested/effective/sourceが変わらない条件を追加し、Docker/aarch64の
`read_policy_persistence`、`read_policy_store`、`api_server`を通した。Piではservice userが
`/var/lib/picdplayer`を所有しdaemonはactiveだが、稼働中daemonに`--settings-file`は付いていない。
実serviceでの保存・復元とCEC画面からの設定は引き続き未確認である。
Artist Backgroundの任意ON/OFFは#49/#51のruntime経路を待つ。標準unitは
`--settings-file`を自動指定しないため、現行Piへのdeployだけで永続化は有効にならない。

現在の到達点は[実機確認済み](#実機確認済み)、次に取り組む作業と進捗は
[残課題とIssue一覧](backlog.md)を参照する。末尾の継続課題は検証上の根拠として残す。
その間の日付付きの節は、条件ごとの個別実験記録である。過去のbuffer値や試験件数を
現在の仕様として使わず、設定の正本は[機能設計](../design/functional-design.md)で確認する。

## 現在の確認待ち

傷disc、cache独立性、速度変更の効果、S/PDIF出力は未確認。
Now Playingのcold boot後TV表示、停止中metadata・画像表示は確認済み。CEC操作後の画面追従や異常時表示は
[Now Playing実機確認結果](#now-playing実機確認結果)に残る範囲を記す。
S/PDIFは[将来候補](digital-audio-output.md)であり、現在の必須試験ではない。

## Semantic CEC navigation（#55、Docker自動試験・Pi部分確認）

CEC方向・決定・戻るをtransport keyと分離し、`WS /api/navigation`へ短命な入力として配信する経路を追加した。
Linux/aarch64 DockerでCEC code変換、長押し/解放filter、API WebSocket配信を自動試験した。
標準Playerのfocus UIは後述の#56でこのchannelを消費する。2026-09-30のPiではTVリモコンの右方向キー1押下で
`WS /api/navigation`の`right`とChromiumの`ArrowRight`の両方が届くことをCDPで確認した。
修正後、左右上下・決定・戻るをTVリモコンで操作し、標準Playerの選択移動を確認した。
2026-10-03には同じTVでBack後に右方向キーを約2秒長押しし、離した後は選択枠が止まり、
音声も正常なことをユーザーが確認した。CECの生code/releaseの詳細と入力切替中・別TVは未確認。

## 標準PlayerのCEC transport操作面（#56、Docker自動試験・Pi部分確認）

標準Playerに5つのtransport buttonを追加し、CECのsemantic navigationとkeyboardでfocusを移す。
状態snapshotからbuttonの可否を計算し、POST受理だけでは再生状態を確定しない。
NodeのUIテストではfocus、204/409、通信失敗、NO_DISCでの無効化を確認する。
navigation WebSocket切断後の再接続と、古い接続からの遅延入力を無視することもNodeで確認した。
MacのChrome headlessによる[1920×1080静的fixture](reports/2026-09-30-cec-controls/README.md)では
5操作とIntegrity Monitor全体が画面内に見える。PiでTVリモコンからPlayを選んで決定すると、
CDPに`REQUEST ACCEPTED`、APIに`PLAYING`が現れ、ユーザーがTVの音声を確認した。PauseもTV表示とAPIの
`PAUSED`で確認し、Backによる選択解除をユーザーが確認した。右キーの二重経路によりStop選択が
不安定だったため、同一操作の近接した異経路入力をUIで抑える修正を加えて再デプロイした。
決定キー由来のbutton clickとCEC selectの重複可能性にも同じ入力処理を適用し、自動試験した。
修正後はSTOPPEDのPrevious→Play、PLAYINGの3回の右入力でStop選択、Stop決定後の
API `STOPPED`、左でNext、上でPlay、下でNextを確認した。画面の選択結果はユーザーのTV目視と
CDPで照合した。異なるTV/remote、長期負荷・温度への影響は未確認である。
追加修正をPiへ再デプロイした後、リモコンでPlayを選び、CDPのresource timingで`/api/play`要求が
1回だったことを確認した。このTVの決定キーではChromiumのbutton `click`は観測されず、
二重click対策そのものの実機発火は未確認である。APIからStopを要求し、後続snapshotの`STOPPED`を確認した。
DockerのLinux/aarch64で`ENABLE_API=OFF`もビルドできた。STOPPED画面の20秒・4 sampleの読み取り
計測では全core CPU平均1.70%、最大2.18%、温度56.9〜58.0°C、現在throttling bitは全sampleで0だった。
短時間かつ過去の基線と同条件ではないため、負荷回帰がないという保証には使わない。

## 選択済みreleaseのArtist MBID（#48、Docker自動試験）

MusicBrainzのrelease artist-credit内に、同じ有効なArtist MBIDが一意にあるときだけ
Enrichment内部の`ArtistIdentityStatus::available`へ投影する。異なるIDの複数creditは`ambiguous`、
ID欠損・不正形式・Various Artistsの特殊IDは`unavailable`とする。選択前の複数候補や
metadata sessionの失効後には、旧候補のMBIDを返さない。IDは現段階でPresentation Modelへ公開しない。

Debian Trixie/aarch64 DockerでbuildとCTest 45件が通過した。parser fixtureで単一・複数・
同一IDの重複credit・ID欠損・Various Artists・不正IDを、session fixtureで候補選択変更と
世代失効を確認した。外部Artist Background providerやPi実機表示は#49/#51の範囲である。

## 複数metadata候補の選択経路（#15、Docker自動試験）

同じDisc IDに複数候補がある場合、未選択ではAudio CD fallbackを維持する。現行実装では
`enrichment.selection`へ表示用候補とsession/disc/metadata世代を載せ、loopback限定の選択POSTで
現行discの候補だけを適用する。選択後のcover artは別workerで取得し、世代またはrelease IDが変わった結果を
適用しない。再挿入時の自動再選択は行わない。

Debian Trixie/aarch64 DockerでbuildとCTest 45件が通過した。候補選択・古い世代と画像結果の拒否、
provider固有IDを公開しないPresentation Model、API入力境界を確認した。
2026-10-01にPiへ#15/#166を含むpackageを導入し、『The Slip』の実候補US盤・JP盤が
`AMBIGUOUS`として公開されること、CEC操作後に`SELECTED`へ変わり、disc/track情報とcover URLが
authoritative snapshotに反映されることを確認した。cover endpointはHTTP 200、image/jpeg、77911 bytesを返した。
TV上のcover画像そのものと失敗系は別途確認対象とする。

## 標準Playerのmetadata候補picker（#166、Docker/Chrome/Pi確認）

#15の公開契約と#55のsemantic CEC navigationを使う標準UIを作業branchで実装した。
曖昧候補の初回検出時にpickerを自動表示し、backで閉じた同じ候補群は自動再表示しない。
`Choose album`で開き直せる。方向入力で候補移動、selectで選択POST、backで閉じる。
選択後にも複数候補の表示と`Change album`を残す変更はNode fixtureで再選択POST、選択済み候補の表示、
選択前の自動表示と選択後の非自動表示を確認した。Mac Chromeの
[1920×1080合成snapshot](reports/2026-10-04-metadata-reselect/README.md)で選択後の配置も確認した。
2026-10-04、PiへPR #183のpackageを導入し、ユーザーがTVリモコンで『The Slip』の候補0を選択後、
`Change album`からCDを取り出さず候補1へ変更した。Pi APIでも`selected_index=0→1`を確認し、
CDPで選択番号の表示を確認した。さらにPR #180のSettingsとPR #183を結合した検証用packageをPiへ導入し、
CDPから通常Playerの候補を選択して`None of these`を押した。APIのselectionは`SELECTED`から`DECLINED`へ変わり、
Playerは`Audio CD / Track 01`へ戻り、pickerが閉じた。`Change album`と`Settings`は表示を維持し、
1920×1080でdocument overflowがないことをCDPで確認した。CECリモコンによる辞退操作と音声は未確認。
HTTP 204は選択完了とみなさず、次のauthoritative snapshotでalbum/trackを更新する。
同じcover URLで選択candidateが変わっても画像を再読込する。
Debian Trixie/aarch64 DockerでbuildとCTest 46件を実行し、Node fixtureで候補表示、CEC操作、
204待ち、409拒否、選択後snapshot、disc不在を確認した。
Mac headless Chromeの[1920×1080表示](reports/2026-10-01-metadata-picker/README.md)は合成候補の
レイアウト確認である。2026-10-01にはPiの通常kioskで、ユーザーがTVリモコンから『The Slip』の
候補を選択した。Pi APIでは2候補（US `2008-07-22`、JP `2008-09-10`）からindex 0が選択され、
`enrichment.status=AVAILABLE`、disc title/artist、track 1 title `999,999`、cover URLを確認した。
ユーザーはTV上のメタデータ表示、pickerの終了、表示欠け・意図しないscrollがないことを確認した。
候補選択中のTV画像は保存しておらず、合成fixture画像を実機の証拠とは扱わない。
その後、曖昧候補の初回自動表示へ変更して再デプロイした。ユーザーはTVで`Choose album`を
押さずにUS盤・JP盤の候補一覧が開くことと、Backで閉じた後に同じ候補群が勝手に再表示されないことを確認した。
2026-10-03には更新済みpackageで同じCDを再挿入し、ユーザーが候補一覧の自動表示、
CECでの候補選択後のdisc/track情報とcover画像をTVで確認した。CDPでも同じ画面を確認した。
再生は選択前に開始しており、候補選択が再生開始の条件ではない。異常候補やCEC入力切替時は未確認。

## Audio CD再挿入時の自動再生（#172、Pi確認）

明示的なdisc不在/eject観測から新しいAudio CDのTOC受理へ進んだ場合だけ、自動でPlayを要求する。
起動時にCDが既に入っている場合、metadata到着、同一TOCの再読込、STOP後のmedia pollでは再生を始めない。
DockerのLinux/aarch64 buildとCTest 46件が通過し、media trackerの挿入根拠の一回消費を自動試験した。

2026-10-01に#166を含むpackageをPiへ導入した。『The Slip』が入ったままdaemon PID `47033`へ
再起動した直後は`AUDIO_READY / STOPPED`だった。loopback APIでejectし、ユーザーが同じCDを
物理的に再挿入した。PID `47033`のままjournalに`auto_play=inserted_audio_disc disc_generation=2`、
続けて`state=PLAYING track=1`と`metadata: status=LOADING`が記録された。APIは
`PLAYING / AMBIGUOUS`を返し、ユーザーはTVで候補一覧の自動表示と実際の音声を確認した。
選択前の音声再生が成立したため、metadata選択は再生開始条件ではない。最後にAPIのSTOPへ204が返り、
その後も`STOPPED / AUDIO_READY`を確認した。2026-10-03には競合解消後のpackageを再導入し、
既挿入CDでのdaemon再起動後は`STOPPED`、同一PID `51063`での物理再挿入後は
`auto_play=inserted_audio_disc disc_generation=2`、`PLAYING / AMBIGUOUS`を再確認した。
ユーザーはTV上の候補一覧と選択前の音声再生、候補選択後のカバー画像を確認した。
APIのSTOPは204で、5秒後も`STOPPED`を維持した。Docker統合後のCTestは48件成功。
異常媒体や別driveは未検証である。

## Artist Background provider入力（#49、Docker自動試験）

fanart.tv v3.2の`artistbackground`応答を、要求したArtist MBIDと照合し、最大512 KiB・32画像の範囲で
解析するparserを作業branchで検証した。画像のID、URL、寸法を入力として扱い、別hostやpath traversalの
URLを候補から除く。候補取得adapterではkeyが空ならrequestせず、keyはURLではなくHTTP headerへ渡す。
200/404/401、取消、timeoutのfixtureを確認した。Debian Trixie/aarch64でbuildと関連試験を確認した。
一つの有界workerで旧artist要求を置換・取消した結果がmain側へ出ないこともfixtureで確認した。
保護されたregular fileからkeyを読む部品は作業branchで追加し、symlink、過大入力、弱い権限、
不正文字を拒否するDocker/aarch64単体試験を通した。全CTest 50件も通過した。
daemonの`--artist-background-key-file`設定経路へ接続し、有効・無効なファイルのいずれでも
playerが起動・終了でき、key/pathをlogへ出さないことをhardware非依存のdaemon試験で確認した。
候補JSONは同じ64 MiB file cacheの管理対象に加え、正常応答は7日、404の候補なしは1日を期限とする。
期限切れの有効JSONはprovider失敗時だけ再利用し、破損したentryは破棄する。
これは候補情報のcacheであり、写真本体のdownload/cacheや表示を有効にするものではない。
写真本体の1件取得部品は、URL再検証、8 MiB上限、JPEG/PNG/WebPの外形・Content-Type、
30日/64 MiB file cache、offline fallback、破損entry・明示的な403/404/410の破棄を
合成fixtureで確認した。完全な画像decodeや実provider通信の確認ではない。
選択済みArtist MBIDの候補取得は別workerからruntimeへ接続したが、既定OFF gateの追加により
key指定だけのworker生成・照会は停止した。実provider通信、写真本体のruntime接続・配信、
権利表示の確認、実機表示は未実装・未検証である。APIが画像の権利者を返すと推定しない。

### 既定OFF・利用不能gate（2026-10-11、作業branch）

#176をbaseとする第一単位では、main ownerのpolicyを状態APIとEnrichmentServiceで共有する。
有効なproject keyがあってもworkerを生成せず、候補JSON・写真取得を開始しない。
Linux/aarch64の正式container build・Debian package生成、全CTest 56/56、package lifecycleを確認した。
productionのEnrichmentServiceをlink-time fake lookupで試験し、key有無、候補選択・辞退・再選択、
disc世代更新、OFF反復・ON拒否後の背景provider呼出0を確認した。通常metadataとcover処理は進む。
初回の追加試験ではcover完了前の取消で完了回数のassertが競合したため、完了観測後に取消を試すよう修正した。
実daemonのNo Disc APIでは4 fieldの利用不可状態、204/400/405/409/413、Read Policy不変、
背景asset参照なし、keyのlog非公開をkey有無で確認した。socket API試験の外部peer POSTは403。
gate・daemon試験は`--network none`のcontainerで各10回反復して成功した。
背景のON、保存・復元、Settings UI、runtime画像配信は未実装。実provider通信・Pi・TV表示は未検証。

### fanart.tv画像の権利条件（2026-10-02再確認）

[公式のMusic Fanart説明](https://fanart.tv/music-fanart/)はArtist Backgroundをメディアセンターでの背景・
スライドショー用途として紹介し、投稿時に出典や権利者をコメントへ記すよう求めている。
[利用条件](https://fanart.tv/terms-and-conditions/)は投稿者へのクレジットや対価なしの利用に言及する一方、
画像の著作権は各権利者に残り、権利侵害となる利用には権利者の許可が必要と明記する。
サイト自身のfair useに関する見解を、PiCDPlayerへの包括的な利用許諾と扱わない。

[公式API仕様](https://api.fanart.tv/)の`artistbackground`応答には画像URL等はあるが、
画像ごとの権利者・ライセンス・許諾範囲を確定できる項目は確認できない。
API仕様に表示されるCreative Commons Attribution 3.0を、写真本体のライセンスと読み替えない。
端末内へのdownload/cache、同一端末のUIへの配信・表示、公開配布版での既定有効化について、
全画像へ一律に適用できる権利条件は確認できていない。写真をrepo・`.deb`・bootable imageへ同梱しない
方針でも、この未確認事項は解消しない。候補取得・画像取得部品のDocker試験は、写真利用の許諾を
意味しない。この調査結果を、全画像の包括的許諾を製品側で確認する実装gateとはしない。

方針確認（2026-10-11）: ダウンロード画像に必要な権利・許諾・表示条件の確認と利用判断は利用者に委ねる。
製品は権利を保証せず、明示ON自体が権利者の許諾を生まないことを説明する。外部写真は同梱せず、
既定OFFを維持する。以前の「製品側で画像ごとの条件を確認できるまで表示実装を保留」という判断は
この方針で置き換える。API認証・アクセス制約は守り、合成/利用許諾済みfixtureによる開発試験は継続する。
ディスクキャッシュの採否・容量・保持期間・削除は独立した議論とする。既存cache部品の試験済みという
記録は、runtimeでのディスク保存採用を意味しない。現段階のON拒否はruntime未接続による。

[API認証仕様](https://api.fanart.tv/)はproject keyの`api-key`とpersonal keyの`client-key`を区別する。
現行`--artist-background-key-file`は`api-key`ヘッダーだけを送る開発中の**project key用**経路であり、
personal keyの入力経路ではない。現段階はkey fileを指定しても候補metadata照会を開始しない。
写真本体の取得・配信・表示や利用者の写真表示ONも意味しない。将来の設定画面ではkey種別と写真表示ONを別に扱い、
secretをAPIへ返さない。API文書にあるCC BY 3.0表記は写真本体のライセンスとして扱わない。

## Metadataとartworkの段階配信（#50、Docker自動試験）

runtimeのMusicBrainz lookupはCAAを待たずにmetadataを先に返し、単一候補または明示選択後の
artworkを別workerで取得する。metadata AVAILABLE時点のartworkはNOT_REQUESTEDで、後続の
AVAILABLE/UNAVAILABLE/ERRORはmetadataのstatusを変えない。古い世代・別releaseの画像結果は適用しない。
Docker Debian Trixie/aarch64でbuildとCTest 45件が通過した。metadata-only lookupでCAAを呼ばないこと、
後続artwork失敗と古い世代の拒否をfixtureで確認した。Pi上のnetwork遅延下での更新順序と
WebSocket観測は未確認であり、#25の統合時に確認する。

## View向けloopback操作契約（#54、Docker自動試験）

既存の`play/pause/stop/previous/next`のPOSTについて、method、空body、204受理、400/405/409、
loopback制限とauthoritative snapshotの意味を仕様化した。Linux/aarch64 DockerのAPI testでは
5操作のroute、本文不正、method不正、handler拒否、既存の外部peer試験を確認する。
標準Playerからの操作、CEC方向・決定との結合、Pi実機のUI操作は#56で確認する。

## Bounded stopped-idle drive stop（#144、Pi確認）

通常Audio CDを認識したPiで、従来のSTOPPED/PAUSED中15秒ごとの`CDROMSTART`要求を廃止した。
TOC取得後のSTOPPEDが連続5分に達すると、DriveAccessCoordinatorを通した`CDROMSTOP`要求が一度だけ
発行される。Pi journalでは`stop_command=accepted elapsed_ms=10 rotation=UNVERIFIED`を確認した。
TVでdrive spinが停止したことはユーザーが目視・聴取したが、daemonは回転状態を観測しないため、
ioctl受理と物理停止を同じ保証として扱わない。

停止要求後、loopback `POST /api/play`は204を返し、5秒後にtrack 1の`PLAYING`とposition進行を確認した。
同じrunで`POST /api/eject`は202を返し、約1秒後に`NO_DISC`へ遷移した。CD再挿入後は
`AUDIO_READY → STOPPED`、14 tracks、track 1へ復帰した。daemon/kioskは全工程でactiveだった。
これは#172導入前の測定であり、現行の挿入後自動再生とは異なる。
この確認は通常CD、direct reader、短時間の再生復帰に限る。pause、unsupported/error、長時間STOPPED、
drive/USB bridgeごとの差異は、物理媒体・drive横断の後続検証 #146 で扱う。

## Bounded ALSA underrun recovery（#34、Docker自動試験）

同一streamでのALSA XRUN復帰を最大3回に制限した。各復帰では最後にALSAへ提出したCD frame境界から
readerを再生成し、prebuffer targetを容量上限まで増やす。4回目のXRUNでは
`underrun recovery_limit_exhausted limit=3`を記録してerror停止し、reader再生成を続けない。
play、seek、pauseなど意図したstream切替は回数をリセットする。これはread retry、read error、
Integrityの`RECOVERED`とは別の出力経路である。

Docker Debian Trixie/aarch64で`playback_engine_test`を含むCTest 44件を実行し、3回の復帰、
4回目の停止、既存の終端underrun停止を確認した。Piで意図的にXRUNを起こす確認は未実施。
傷discや長いread stallの実機評価は#146、未解決PCMの追加fallback方針は#156で扱う。
buffering UIはこの試験で検証しておらず、#34の未完了条件とは扱わない。

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
retry policy unit testで値なし、0、4秒、上限超過、負値を確認した。
`http_security_policy` testではCAA初期URLとredirect host、相対redirect、IPv4/IPv6のpublic/private/link-local
判定を確認した。実際のHTTPS response headerとredirectは下記#135のfixture試験で確認した。
Piでのnetwork切断時の挙動は未確認である。

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

2026-10-04の追加試験では、test専用の自己署名証明書とloopback TLS serverを用い、libcurlの実HTTPS応答で
200 JSON、429の`Retry-After: 4`、CAAの許可/拒否redirect、本文上限、timeout、private peer拒否を確認した。
test専用buildだけがfixture用CA・DNS override・loopback許可を指定でき、製品binaryは従来どおり
CAAのprivate/link-local peerを拒否する。壊れたJSONがworkerのERRORになること、HTTP lookupを待つ間も
hardware非依存の再生commandが進むこと、古いdisc世代のERRORを現在のsessionが採用しないことも確認した。
Debian Trixie/aarch64の全ビルドとCTest 48件が通過した。これらはPiでのnetwork異常や音声継続の証明ではない。
Piの通常系では2026-10-01〜03に『The Slip』のMusicBrainz候補、選択後のcover endpointとTV画像を確認済み。
その画像が当該時点の新規CAA通信かcache由来かは区別していない。2026-10-03時点ではPiでの
network切断、fresh CAA lookup、実機再生中のtimeout影響は未確認だった。

2026-10-04のPi実機では、稼働中daemonと既存cacheを変更せず、APIのTOCから既存libdiscidで
『The Slip』のDisc IDを計算した。一時cacheを使う別の`--lookup-disc` processでMusicBrainzの
新規HTTPS lookupを行い、`cache=miss`でUS/JPの2候補を取得した。標準PlayerからJP候補を選択すると
APIは`SELECTED`/`AVAILABLE`となり、再生位置は進行した。TVで曲名と音声を確認したがcoverは表示されなかった。
PiからCAAへ直接照会するとJP releaseはHTTP 404、US releaseはarchive.orgへの307 redirectだった。
JPで画像がないことはCAA応答と整合するが、この段階ではdaemonのfresh CAA取得経路を確認していない。
別の診断processだけに到達不能なHTTPS proxyを指定すると、MusicBrainz lookupは
`HTTP request failed: Could not connect to server`となり、稼働中daemonはAPI上`PLAYING`を維持した。
ユーザーはこの間のTV音声に途切れがないことを確認した。この隔離試験はdaemon自身のnetwork断や
timeout中の音声継続を証明しない。

同日、一時的にsystemd daemonだけを停止し、既存cacheを共有しない隔離daemonを同じPi上で起動した。
空のcacheからMusicBrainzの2候補を取得後、試験用にUS候補を選択すると、CAA経由のcoverが
same-originの`/api/presentation/artwork/cover`として公開された。元のJP候補の選択は最後に復元した。
別の空cacheを使う隔離daemonへ到達不能なHTTPS proxyを設定すると、enrichmentは`ERROR`になったが、
`POST /api/play`は204を返し、`PLAYING`の再生位置は101→482 frameへ進んだ。つまり、Pi上の
daemon自身がmetadata HTTP失敗を受けても再生制御・PCM進行は止まらなかった。これは意図的なproxy
接続失敗の試験であり、実network切断や15秒timeout、音声の無欠落を証明しない。
試験後は隔離daemonと一時cacheを削除し、元のsystemd daemon/kioskをactiveへ戻した。
JP候補を再選択して再生を再開し、試験前の4曲目・約125秒へAPIで位置を戻した。
長期安定運転は[#4](https://github.com/udonchan/picdplayer/issues/4)、傷disc・別driveの
物理異常試験は[#146](https://github.com/udonchan/picdplayer/issues/146)で保留する。
これらは#135のHTTP失敗経路の自動試験・限定的なPi確認を完了する条件には含めない。

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
undervoltage、長時間再生、throughput、失敗時のdrive状態は未確認である。#8の実装済み範囲はCloseし、
通常CDの継続運転・聴感を#184、既定要求なしとの同条件定量比較を#83、非対応drive/ioctl失敗を#146へ移管した。

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
確認していない。この短時間確認を通常系の継続運転を扱う#184の完了とはしない。

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
過去のtrace件数を定量的な削減率の根拠から外した。現行masterの負荷・残測定は#83、通常系の継続運転・実表示・音声は#184で別途確認する。

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
- direct/paranoiaの同条件比較は未実施。ライセンス・再配布条件の確認が必要なためparanoiaの公式採用を見送り、比較Issue #6は終了した。標準運用はdirectを維持し、現行構成の性能・速度要求の定量比較は#83、継続運転は#184で評価する。
- pause再開の待ち時間とbuffering表示、傷disc/長いread stall時の復旧方針を検討する。同一streamの
  ALSA underrun復帰上限は3回としてDocker自動試験済みだが、実機異常系は#146で未確認である。
- mediaとPCMのdevice access直列化は実装済み。挿抜を含む実機回帰確認を継続する。
- 同じTOCの別disc識別を検討する。LOADING後に同じTOCへ戻った場合のmetadata再要求は実装・確認済み。
- 現在の独自AsyncLoggerは要件を満たしている。spdlog等との比較、Buildroot package化、binary size、
  非同期queueの満杯時挙動、runtime level変更、追加sink、rotation、ライセンスを調査し、必要性が確認できた
  段階で置換を検討する。現時点では再生経路へ影響する変更を行わない。
- metadata lookup中交換、network切断、CAA失敗時の扱いを実機確認する。複数候補の通常選択は
  Piの『The Slip』で確認済みだが、lookup中交換やnetwork異常と重なるケースは未確認。
- stale cacheのoffline fallbackと破損JSON再取得の統合試験、書込み不能、非1始まりtrack対応、
  network切断とPi上のfresh CAA lookupは未完了または継続確認とする。実HTTPS response headerを使う
  Retry-After/redirect統合試験は#135のloopback TLS fixtureで確認済み。
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

今回の成功は実機音声の保証ではない。実機音声・通常系の継続運転は#184、性能測定は#83の検証と区別する。
#24は当時Draftとして維持し、実際のPlayer側の履歴consumerを追加する場合は同じ条件の統合試験を必要とした。
Piへのdeploy・再測定は行っていない。#90の本PRは現行契約に対する再現可能な回帰検証であり、
任意接続数への防御、無期限sink停止時のshutdown完了、real-time性能を新たに保証するものではない。

### 実機異常系の担当と記録の共有

[物理媒体・USB drive実機評価 #146](https://github.com/udonchan/picdplayer/issues/146)で、観測できたread異常と
音声/操作影響、API/警告/再接続の整合を同じrun記録で確認する。paranoiaのbackend比較は#6で採用見送りとして終了し、特殊TOCのmetadata対応・
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
通常系の継続運転、CDP未接続のCPU/温度比較は未確認であり、#146/#184/#83の記録と重複しない。

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
