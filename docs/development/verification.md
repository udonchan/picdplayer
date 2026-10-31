# 検証状況と残課題

最新追記: 2026-10-31。実装済み、hardware非依存試験済み、実機確認済みを区別する。
今回のlive照合と休止点は[2026-10-31追記](#pr256-readonly-20261031)を参照する。
以下の過去run記録は保持し、今回再検証した結果とは区別する。

## 小さな文書PRの同期（2026-10-31）

利用者の続行指示により、文書だけの変更も実装完了まで溜めず、独立PR/CIを経てmasterへ統合した。
root checkoutや保存済みfeature branchは変更せず、この専用作業branchだけを最新masterへ載せ直した。
競合したverificationは過去runと今回のread-only記録を両方保持した。

| 文書PR | 検証したhead / CI run | 観測した結果 / master統合 |
|---|---|---|
| [#263](https://github.com/udonchan/picdplayer/pull/263): v1能力要件・31件監査・休止点 | `a9b9c0c` / [41207677826](https://github.com/udonchan/picdplayer/actions/runs/41207677826) | 標準WPE Release/Debug各72/72、package lifecycle・候補検査成功。独立文書reviewで具体的findingなし。`e2174a9`で統合 |
| [#265](https://github.com/udonchan/picdplayer/pull/265): #49限定契約・#264とIssue訂正の同期 | `30f2047` / [41208860159](https://github.com/udonchan/picdplayer/actions/runs/41208860159) | 標準WPE Release/Debug各72/72、package lifecycle・候補検査成功。契約reviewでblockerなし。`42a05a2`で統合 |

全件成功というCI表示だけでなくlogの実CTest数とpackage結果、PRのhead/対象file/merge状態を確認した。
両PRとも製品code変更なしであり、後続#49の実装や実機動作を検証した結果ではない。
MacのDocker socketは不在だったため、daemon起動や代替環境導入をせず既存の標準CIを使用した。
契約reviewの指摘に従い、構造から検出できる切断/不正長とfull decodeの保証を区別する文言を補足する。
privateな設定/key/画像/package/SDKは含めず、この同期単位で実機操作・Issue closeは行っていない。

### #49の未完成試行と品質停止

同日の実装委譲で、既存loader/URL検査/cache経路の欠落、公開headerの必要型宣言欠落、
JPEG marker解釈の誤り、WebP部の未定義変数・C++ではない記述を含む途中差分を確認した。
親のmacOS `clang++ -std=c++20 -fsyntax-only -Iinclude src/artist_background_image.cpp`はexit1・
20 compiler errorsで停止した。これはnative syntax検査でありLinux/aarch64 buildや実行試験ではない。
この試行を修正済みとせず、追加実装/修復を停止するよう指示した。
公開header/sourceの途中差分はrepository外へpatchとして保全し、commit/push/PRへ含めていない。
一時領域のpatchの存在を永続保全の保証にはせず、親の観測・失敗と未確認をここに残す。

14:50 UTCの安全な再開時に、旧workerから編集停止の応答を得た。変更は公開header/sourceの2fileのみで、
現在差分と保全patchのSHA-256はともに`af425ed2c417d0bccf9c3684cb792e09c4f0f9b472a9cca8a4d211197a7fd308`だった。
親が今回の2fileだけをcommit済みの内容へ戻し、`git diff --exit-code HEAD --`で対象source/testsの差分0を確認した。
同じnative syntax commandはexit0へ戻った。これは元実装の復元確認であって、新しいadmissionの成功ではない。
文書3fileの未commit追記は保持し、root checkout・保存branch・既存設定は変更していない。
worker自身はbuild/test未実施と報告した。Kilo設定変更とターン中断の因果関係は独立に確認できていない。

文書PR #263/#265の成功はこのsource試行の成功ではない。fixture更新・新検査の実行成功・
Linux/aarch64 CI・実機写真/音声の受入はなく、#49はOpen・未完了のままである。
次の実装は既存取得/安全境界を保持できる小単位に再分割し、形式ごとの有効fixtureと構造解析を
先に検証する。今回の不成立な独自parserを微修正だけで製品経路へ投入しない。
最初の単位はJPEG/PNG/WebPの有効な小さいfixtureと既存試験の意味の保持だけとし、
画像decoderでのfixture検証と再build後の試験を行う。形式別の境界/構造試験を揃え、
全形式とnetwork/fresh/stale経路が適合するまでproductionのadmission有効化を完了としない。
設定変更後という理由だけで旧workerを自動再開せず、後続の委譲には新しい小単位・上限・試験契約を渡す。

## PR #256 全域local PCM指紋の実機完了（2026-10-27）

下記の90分上限試験を、限定helperと一時offset0を用いて一度だけ実行した。
06:46:21 UTCの開始前確認後、daemon判定logは06:49:14.714 UTCにstart accepted=1・
readiness=ready・media_activity=IDLEを記録した。結果はCOMPLETE、failure/core_failure NONE・native_error0、
全14 track・LBA `[0,242334)`・242334 CD frame・canonical569969568 bytesの全域受理だった。
job予算5400000ms、REPEAT region75/matches2/attempts3/per-call10000ms、offset0を維持し、
自動再試行・予約・gate緩和・再生は行わなかった。

保存manifestはschema1/kind complete_pcm_manifest、profile local-pcm-sha256-v1、
canonical s16le-44100-stereo-v1/SHA-256。controllerは開始時TOC/identityとの一致を検査し、
COMPLETE検出直後、復元restartより前に保存した。privateなhostへ回収後も全coverage、
連続TOC、14 trackのbyte数合計と各64桁lowercase digest、job/session identityを再検査した。
disc fingerprintは`351b1612d704ce18edeb69246e27ad2b76efb76cbaf661b56aebc76bc67f8535`。
manifestの観測値はread_calls3232/verified_calls3232、verification_attempts6464、
verification_mismatches0、direct_retries0/recovered_reads0/uncertain_reads0だった。
これはlocalな読取・比較結果であり、external_verification NOT_CHECKED、cache_independence NOT_PROVENのままである。

Piのresult.json mtimeは07:07:14.279993 UTC（start受理から約17分59.566秒）、
controller最終log mtimeは07:07:17.871978 UTC。mtimeは結果保存時刻であり、内部完了時刻そのものではない。
要求2xの理想時間より短いが、実測current_speedは取得しておらず、要求速度どおりの物理読出しや
cache独立性をこの所要時間から推定しない。90分は上限で、実行時間ではない。
controller.jsonlはready1/final1/capture210の212行・15073 bytes、result.jsonは3312 bytes、双方0600。
result.pendingは不在で、PCM本体は保存していない。manifest file SHA-256は
`99bb4351184d96b41a7137d1d8f8accb9b7165ac953fa156506a088a84508efe`。
privateなPi/hostの専用directoryへ結果とlogを保全し、Gitや公開artifactへ含めない。

controllerは同job/session/disc generation/PIDについてowner非busy/released/非inflight/非waitingと
FD0/race0を10秒以内に確認してから元設定を復元し、通常daemon/kiosk restartと15秒以内のreadback、
専用helper/rule撤去を完了した。最終記録はbackendRelease=true/restored=true/unresolved=false。
個々のFD/sensor標本やrelease所要時間はlogへ保存しない設計であり、独立した時系列測定とはしない。
復元後の新session/job0の状態を、旧jobの解放証拠の代用にしない。

07:13:29 UTC以降の独立read-only再確認で、元config.beforeとの設定file byte一致、offset null/NONE、
STOPPED・capture IDLE/job0/非busy/released/非inflight/非waitingを確認した。
REPEAT要求/実効・pending false・背景ON/saved、要求2x・current_speed nullを保持し、
daemon PID51340/kiosk PID51369は07:07:15/16 UTC起動、active/running・NRestarts0だった。
専用helper/ruleは両path不在、package c7e1e38.../daemon e176151...、HTTP JS48749538.../CSS3d418784...は一致した。
終了確認時の温度58.534度、throttled=0x0。root/userのbackupは削除していない。

このrunで全域local指紋生成を初めて実機確認したが、外部MATCH、原盤正当性、bit-perfect、
cache独立性、音声/TV/物理リモコン、取消後の再生復帰は未確認である。
短時間取消runのINCOMPLETEという歴史的結果は変更せず、#238/#239もOpenを維持する。
製品codeの変更・再build・再deploy、commit/push/merge/Issue closeは行っていない。

## PR #256 90分上限の全域試験準備（2026-10-27）

利用者は最初のPCM受理確認後、90分上限の全域完了試験を別承認した。
01:49:29 UTCの公開API read-only観測ではSTOPPED/AUDIO_READY、14 track、start_lba0/leadout242334、
242334 CD frame・音声3231.12秒・canonical569969568 bytesだった。
速度は要求2xが適用済みだが実測current_speedはnull。REPEAT region75/matches2/max_attempts3/budget10000ms・
pending false・背景ON/saved・offset null/NONE・capture IDLE/job0/受理frame0/解放済みを確認した。
理想的に2xで2回読むだけでも約53分51秒＋seek等を要し、90分完了を保証しない。

設定・REPEAT・速度要求・背景ON・Custom UIは保持し、明示offset0だけを一時追加する計画である。
start1回、job deadline5400000ms、完了/失敗/90分前の取消時点5390秒を上限とし、自動再試行・自動再生は行わない。
60秒の進捗停滞、温度80度以上、電源/throttleの現flagや新しいsticky flagは早期中断条件とする。
解放は同job/session/PIDのowner flagsとFD0/race0で最大10秒確認し、不明なら復元のrestartで回避しない。
90分は取得jobの上限であり、解放確認と必要な元設定復元時間は別に記録する。
完了時だけ全coverage/byte数/TOC/profile/14track digestを検査し、64KiB以内の結果JSONをprivate保存する。
PCM自体約570MBを保存する計画ではなく、controller logは10MiB上限、外部照合はNOT_CHECKEDのまま。

host private `pr256-pcmfull-20261027.jJvnSj`とPi `picdplayer-pcmfull-20261027-jJvnSj`へ準備し、元設定を0600で保全した。
同じ4操作helperのSHA-256は`6252fb803bc63e97062c7a75980567957556eaeb7ae214714d0c75c351062520`、
ruleは`b3ae116b03029281b2507c88a11bba5524c8fcf264c364ce003aee06ede9c957`。
構文・転送hash・元引数保持の純粋offset0変換を確認したが、root設置/offset変更/startはまだ行っていない。
private controllerはfake-clockの完了/invalid manifest/拒否/読取失敗/進捗停止/異常/応答喪失/未解放/割込み等で検証中。
既存sudo認証では新しい限定許可を設置できないため、利用者の一度の対話sudoが必要である。

独立reviewでcontrollerの完了結果保存順と期限後証拠の受入を実行blockerとして指摘されたため、開始せず修正した。
COMPLETE検出直後およびstart応答喪失の回復経路でmanifestを先に検査/保存し、未保存の完了結果があればrestartしない。
HTTP/helper/sensorへ残り時間を伝播し、release/readbackの期限後に得た証拠を成功扱いにしない。
元helperのraw FD出力とpublic API/config hash/sensorのadapterを明示し、復元restart後には有限のreadiness確認を行う。
誤ったinvalid-manifest時の復元期待やsensor-due fixtureも修正し、失敗logを消さずに8 fake試験を成功させた。
90分上限でcancel一度、応答喪失時の結果保存、遅いrelease/readbackの拒否も検証し、再reviewで実行blockerの追加指摘はなかった。
reviewで残った早期startup失敗は親が未開始状態を確認して回復する範囲とし、crash-proofとは主張しない。
02:41:36 UTCのread-only照合では今回のhelper/rule/root stateは全て未設置、STOPPED/offset未指定/job0だった。
設置確認dialogはdismissされ、認証成功とは扱わない。最終controllerはprivate配置したが、実機jobはまだ開始していない。

利用者が今回のFULL READY実出力を提示した後、03:42:53 UTCにroot所有/mode/hash・限定FD観測・
現在binary e176151...・元設定一致・未開始log/result不在を確認した。公開layout/policy契約も実データの純粋検査で確認した。
温度57.996度・flags0x0。独立reviewのblockerを修正したcontrollerはSHA-256
`a8b8784d06227aba77e065caed486896d11c67c515663ff3bda2dfbc9f877391`。
限定helperでoffset0を一時適用し通常再起動、03:45:25 UTCにSTOPPED/AUDIO_READY・offset0/USER_CONFIGURED・job0/解放済みを確認した。
この時点はmedia_busyであり、開始予約はせず、次のcontrollerから一度だけownerの全gate評価へ要求を渡す。
長時間処理はprivate logを主記録とし、終了時の結果保存・解放確認・復元/専用許可撤去まで追跡する。

## PR #256 初回PCM受理の30秒予算試験（2026-10-26）

master統合後、利用者は最初の検証済みPCM受理を目標とする短い実機試験の継続を承認した。
job上限30秒・受理PCMが増えれば早期cancel・未受理でも約29秒でcancel一度・解放確認最大10秒とする。
REPEATのregion75/required_matches2/maximum_attempts3/per-call予算10000msは変えず、全域完了・音声を目標にしない。
startは一度だけ、拒否時の再試行はなく、解放不明時はplay/restart/killで回避しない。

18:31:43 UTCのread-only再確認はSTOPPED/AUDIO_READY・offset null/NONE、capture IDLE/job0・
busy=false/reader_released=true/call_inflight=false/waiting_for_access=false/accepted frames/bytes0。
REPEAT要求/実効・pending=false、背景ON/saved、配備package c7e1e38.../daemon e176151...、
daemon PID47624/kiosk PID47650 active/running・NRestarts0を確認した。前回helper/ruleは撤去済みだった。

旧成果を残し、private host `pr256-pcm30-20261026.bw2y3p`とPi `picdplayer-pcm30-20261026-bw2y3p`を新規作成した。
設定をprivate0600で保全し、同じ4操作helperを現在daemonのhashへ固定して準備した。
helper SHA-256は`248550125b25fa4740fe09a7b13202c52b51026893dff9d0da934d432312d63f`、
ruleは`d495b28701de9303a1eea27e1f03c14d54c9231cf540443c7c7f63e05e059781`。
script構文、転送hash、実設定の引数保持/純粋offset0変換を確認したが、この時点では適用していない。
単発harnessはfake APIで409拒否、約8秒後受理、無受理の上限cancel、観測例外時cancel、
start応答喪失後のjob同定/cleanup、解放timeoutを検証し、start/cancel回数制限と復元を阻む未解放結果を確認した。
候補再build/配備、helperのroot設置、offset変更、実機start/play POSTはまだ行っていない。
root設置は利用者の一度の対話sudo待ちであり、private領域の存在を将来の成果保全保証とはしない。

READYの回答後、23:07:13 UTCに実helperを再確認したところ今回のpathが存在せず、操作前に停止した。
23:08:00 UTCの追加read-only照合でも今回のhelper/rule/root state directoryは全て不在だった。
private配置sourceのhashは一致し、実行結果log capture30.jsonlは未作成。設定はbeforeとbyte一致、
STOPPED/AUDIO_READY・offset null/NONE・capture IDLE/job0/受理frame/bytes0/解放済みを確認した。
申告だけを設置成功へ昇格させず、認証/設置の確認待ちとしてoffset変更・start/cancelを実行していない。
利用者は再確認で現在terminal操作できないと回答したため、この準備単位は元設定のまま中断した。
30秒の実取得・初回PCM受理・取消/解放の試験結果はまだなく、準備/fake検証を実機成功へ変更しない。

### 実行と最初の検証済みPCM受理

23:17 UTC台の利用者再開指示後も当初helper未設置を確認したため、認証を迂回せず設置手順を再提示した。
利用者は今回のcommandの実出力（PCM30 READY）を提示し、23:20:10 UTCにroot所有/mode/hash、
専用rule0440・限定observe成功、元設定一致、STOPPED/offset未指定/job0/解放済みを実測した。
helperで明示offset0だけを追加し、通常daemon/kiosk再起動後に一回だけ取得を要求した。

23:21:07.108 UTCの前状態はSTOPPED/AUDIO_READY・offset0/USER_CONFIGURED、全gate成立、
PID49473のFD0/race0。温度59.072度・throttled flags0x0を確認した。
23:21:07.130 UTCにstart HTTP202を受信し、判定時点logもaccepted=1/failure=0/readiness=ready/media_activity=IDLEだった。
job1/COLLECTING・busy true/reader_released false/call_inflight trueと同PIDのFD10保持を観測した。
今回も約2秒時点の受理量は0だったが、取消せず承認された30秒job予算内で観測を続けた。
23:21:11.079 UTC（要求から約3.970秒）にaccepted_frames75/accepted_bytes176400、failure NONE/core_failure NONE/native_error0を初めて観測した。
75 CD frameは44100 stereo sample frame、S16LE stereoで176400 bytes（音声1秒相当）であり、音声試聴ではない。
REPEAT条件を維持したまま、coverage/offset/read verificationを通ったPCMがcanonical consumerへ渡り受理されたという正例である。
個々のprobe/open/seek/readの内訳時刻、物理再読込やcache独立性までは測定していない。

受理増加の観測直後、要求から3.9706秒でcancelを一度送信し、23:21:11.100 UTCにHTTP202を受信した。
結果はINCOMPLETE/CANCELLEDで、途中75 frame/176400 bytesを保持したまま解放待ちを観測した。
最初の解放標本はownerがまだbusy/unreleased、FD0で、次の標本はowner解放済みだが通常media処理RUNNING/FD1だった。
23:21:13.379 UTC標本で同job/PIDのbusy=false/reader_released=true/call_inflight=false/waiting_for_access=falseとFD0/race0が揃った。
cancel応答から約2.279秒で確認が揃い、10秒待ち枠内だった。GETとFD観測は原子的でなく、この差は実close処理時間そのものではない。
追加start・自動再試行・play・強制kill/電源断は行っていない。

今回の目標である最初の検証済みPCM受理は確認できたが、意図した取消により全域未完了で、finalizeされた指紋/manifestは生成していない。
結果の意味はlocal_pcm_fingerprint/外部照合NOT_CHECKEDのまま。全域hashの正しさ、外部MATCH、原盤正当性、bit-perfectを主張しない。
前回の約1.3秒取消で受理0だったことと今回約4秒で初回受理したことは、前回の枠が初回受理には短かった可能性を支持するが、
別jobの準備/drive状態の差もあるため、過去のcall内訳を遡って確定しない。

### 復元と終了確認

解放確認後に元のoffset未指定へbyte復元し、通常daemon/kiosk再起動を実行した。
23:23:07 UTCの終了APIはSTOPPED/AUDIO_READY・offset null/NONE、capture IDLE/job0/解放済み・accepted frames/bytes0。
これは再起動による新sessionの状態であり、直前job1の75 frame受理を否定する値ではない。
REPEAT要求/実効・pending false・背景ON/saved、HTTP JS48749538.../CSS3d418784...、元設定とのbyte一致を確認した。
同じpackage c7e1e38.../daemon e176151...を維持し、daemon PID49718/kiosk PID49745（23:21:47 UTC起動）はactive/running・NRestarts0。
今回のroot helper/ruleはcleanupで撤去し、両path不在を確認した。privateなroot/user backup・script・logは保持した。
温度59.072度・flags0x0を終了時点で確認した。音声/画面/物理入力と取消後再生復帰は今回も未確認。
保存済みlogを再読し、start/cancel各1回・受理75/176400・同job解放の最終result一致をassert確認した。
private capture log SHA-256は`4d279f89f26f98ee9b75ccd952c8b633aa10300fabd2e34462bc5c78c7fe5602`。
コード再build/再配備や追加取得はせず、検証記録/backlogだけを新しい未commit差分として保持する。

## PR #256 master反映判断（2026-10-26）

利用者がここまでの修正のmaster反映を明示指示したため、元のC/D実装と後続UI・診断・調停・記録を
PR #256の同じfeature branchへ保存し、CI/reviewを経由して統合する。現在の統合状態は同PRを正本とする。
後続runtime/診断/調停/回帰はba8b5df、対象を限定した実験機許可は27e6a92へ分けて保存した。
保存基準head470ca4fのCI成功を、今回保存する新headの結果へ代用しない。
統合準備時に現sourceのowner/対象testを再compileし、既存SDK・networkなしLinux/aarch64の
標準WPE Release 72/72・Debug 72/72、hostのcontrols/render Node回帰と3 JS構文検査が成功した。
PR全体のcore/runtime/UI差分を独立reviewへ分担し、元実装から統合対象に含めて確認する。
確認時のrefs/remotes/origin/masterは597cbcaで、05a1a3e以後の追加はPR #261の文書6ファイルだけだった。
それらを保持し、root checkoutの未commit成果や旧候補worktreeへは触れない。

最新の実機正例は下記のstart202/job1/COLLECTING・同PIDのdrive FD保持・1.326秒でcancel202・
取消応答後約4.24秒でowner解放/FD0が揃った標本である。
ただし進行中だったdrive callがTOC probe/reader open/raw readのどれかは未特定で、受理PCMは0。
これは指紋生成/全域完了/音声/取消後再生復帰の成功ではなく、#238/#239はOpenを維持する。
通常再生試験のscript不備・約53秒への時間逸脱も消さずに保持する。
実機の最後の確認は06:17:04 UTCで、offset未指定へbyte復元・一時helper/rule撤去済み。
以後のcommit/push/CI/統合確認で、追加のPi接続・配備・再生/取得を実行しない。
以下の各節のDraft/未merge/未commit/未配備等は各時点の履歴であり、この判断以後の状態へ上書きしない。

## PR #256 初回start受理・取消・解放（2026-10-26）

利用者が判定log版に固定した限定helperの対話sudo設置成功を報告し、試験を再開した。
06:15:41 UTCにhelper root所有0755/既知hash・専用rule0440、配備daemon e176151...、
STOPPED/offset未指定/job0/解放済み、FD0/race0と元設定一致を確認した。
privateな単発scriptを整理し、fake APIで拒否時startのみ、受理時start/cancel各一度、
FD観測例外でもcancel一度となることと2秒以内の取消を確認してから配置した。
本番code/gateに追加変更はなく、helperでoffset0だけを一時追加して通常再起動した。

06:16:08.515 UTCの前状態はSTOPPED/AUDIO_READY・明示offset0/USER_CONFIGURED、
capture IDLE/job0/解放済み・media_activity IDLE/start_rejectionなし、PID47410のFD0/race0。
温度58.534度・throttled flags0x0を確認した。
06:16:08.537 UTCにstart HTTP202を受信した。判定時点logも
`accepted=1 failure=0 readiness=ready media_activity=IDLE`であり、隣接GETだけの推測ではない。
job1/COLLECTING・busy=true/reader_released=false/call_inflight=true・media IDLE、
同PIDのdrive FD10（1件/race0）を2標本で観測した。accepted_frames/bytesは0だった。

開始要求から1.3256秒でcancelを一度送り、06:16:09.865 UTCにHTTP202を受信した。
直後は同job1 INCOMPLETE/CANCELLED・busy=true/reader_released=false/call_inflight=trueであり、
受理202を解放完了としなかった。FD10も保持されていた。
06:16:12.661 UTC標本でFD0を先に観測し、06:16:13.382 UTC標本でowner解放済みだがFD1、
06:16:14.108 UTC標本で同PID/job1のbusy=false/reader_released=true/call_inflight=false/
waiting_for_access=falseとFD0/race0を確認した。cancel応答から約4.24秒で有限10秒枠内だった。
GETとFD観測は原子的ではなく、解放後のFD1は再開した通常media処理の可能性もあるため、
同じFD番号が永続的にcapture所有を意味するとは扱わない。owner解放と同PIDのFD0が揃った標本を終了根拠とした。

結果はstart1回/受理1・cancel1回/受理1、実drive FD保持と解放待ちの観測あり。
受理PCMは0 frames/bytesのまま、結果はINCOMPLETE/CANCELLEDであり、PCM fingerprint生成の正例ではない。
取消前に進行中だったdrive callの内訳やraw read完了はこの観測だけでは特定できない。
実行中I/Oを強制中断せずに解放を待ち、kill/電源断/自動再試行/追加取得/再生POSTは行っていない。

解放確認後に元のoffset未指定へ復元し、通常daemon/kiosk再起動を行った。
06:17:04 UTCの終了状態はSTOPPED/AUDIO_READY・offset null/NONE、capture IDLE/job0/解放済み、
REPEAT要求/実効・pending=false、背景ON/saved、元設定fileのbackupとのbyte一致を確認した。
再起動後のjob0はsession resetであり、取得がなかったという記録にはしない。job1のprivate logを保持している。
専用helper/ruleをcleanupで撤去し、両path不在を確認した。root/userのprivate保全物は残す。
daemon PID47624/kiosk PID47650はactive/running・NRestarts0、温度60.686度・flags0x0。
判定log版package c7e1e38.../daemon e176151...を維持した。音声/画面/物理入力と取消後の再生復帰は未確認。

因果上の結論は、実機のstartが恒常的に拒否されるのではなく、判定瞬間にmedia処理が残る要求は拒否され、
今回のmedia IDLE・全gate成立時は同じownerから受理されたことである。
過去の全失敗が同じ原因だったこと、調停変更前後の定量的な改善率、全域取得/外部照合/bit-perfectは主張しない。
このrunで短いstart/cancel/releaseの確認を一区切りとし、追加の音声再生や長い取得は別枠とする。
今回のcode/AGENTS/記録は未commit・未push。private script/logや設定はrepositoryへ含めていない。

## PR #256 判定時点logと拒否専用HTTP診断（2026-10-26）

利用者は原因追究/実機試験を指示し、対象Piを再install可能な実験機として必要な破壊的変更を許可した。
対象限定・host/成果保全・強制終了を解放成功としない境界をAGENTS.mdへ追記した。
このrunでは破壊的変更を必要とせず、通常配備とoffset未指定の拒否専用診断だけを行った。
中断後05:25 UTC台に保存worktree/差分を再照合した。head470ca4f、既存未commit差分は保持している。

### 判定logと候補

ownerのstart処理へ同じcontextから取得したreadiness/media_activityと、startのaccepted/failureだけをlog_noticeで追加した。
要求body/session ID/設定/PCMを出力せず、gate・POST応答・実I/O・予約/再試行動作は変更していない。
readinessは一般にはidentity不一致等より後の条件であるため、log名だけを常に実拒否理由と解釈しない。
今回の同一identity/job0/capture_not_ready/non-null readinessの条件ではstartの実拒否gateと対応する。

source snapshotはhead470ca4fとdirty patch
`0877b0653f146369639c649e02feaaf17db73dacb7bbaadd4793f0a68abbae85`、新header6f1cb466...を含む
provenance JSON `7ef8fa1229341e986da747a85a6a3cd1eee4a62c91c595941d3b8fa6981c2bfd`で固定した。
既存arm64 image/SDK、networkなし、CPU2/memory2GiB、private出力でReleaseをcompileし、
media_worker/api_server/complete_pcm_session/player_daemonの対象4/4が成功した。
CMake install/CPackと正本artifact検査で全payload/Release profileを照合し、前後のsource一致を確認した。
private総量約2.49GBで予算4GiB以内、download/SDK build/Pi compileはない。今回は全72試験の再実行はしていない。
package SHA-256は`c7e1e38b74b8ff9154c746557e6b71c9b3df494d04d3c2281e93e3e89cbb3a3a`（60,301,336 bytes）、
daemonは`e176151f2a3c9dd97dec6b994ff88331548fcb5439eb5460646795254c49634a`。

05:27:12 UTCにSTOPPED/AUDIO_READY・offset null/NONE・job0/解放済みと既存journal読取権限を確認した。
Piのprivate `picdplayer-decision-20261026`へ直前package007bb864...と設定を保全してから正本toolで通常配備した。
active service維持・実WPE起動を確認し、候補identityも後続で照合した。新sudo権限は不要だった。

### 有限の拒否専用試験

offset未指定を負の安全条件として固定し、現在identity/profile/duration10000を使ったvalid startを6回だけ診断入力した。
これは実取得の自動再試行ではなく、どのgateで拒否されるかを測る固定実験で、offset0や取得gate緩和は一切ない。
各入力前にSTOPPED・offset null/NONE・job0・busy false/reader released/inflightなし/受理frame/bytes0と同一identityを検証した。
guardはfake入力で0/非0 offset・busy/jobありを拒否することを先に確認した。
順序はA/B/C/C/B/A。Aはfresh socketへheader/bodyを一つのsendall、Bはfresh http.client、
CはGET後の同じsocketを再利用した。TCP segmentの境界を一つに保証した実験ではない。
1標本3秒/全体30秒未満・最大6POSTと14GETの固定予算で、idle待ち/phase targeting/適応的retryは行わなかった。

05:29:19.272～.648 UTCの6POSTは全て409/capture_not_readyで、約0.6秒で終了した。
同PIDの判定log6件は順に、最初の4件（A/B/C/C）がoffset_unknown/IDLE、最後の2件（B/A）がmedia_busy/RUNNINGだった。
5件目では直前GETはIDLEであったが、POST判定contextはRUNNINGであり、GETとPOSTの間の実状態差を確認できた。
追加のsaved-log照合で6要求/6判定・固定順・全409・abort/skipなし・終了job0をassert確認した。
request IDによる相関ではなく同PID/同window内の順序対応で、同時の別startがないことを前提とする。
独立reviewはこの条件下のgate解釈に問題なしとしたが、ordinal相関と少数標本の限界を指摘した。

結論: このrunの2POSTは実際のmedia busyで拒否され、4POSTはmedia idleを通過してoffset未指定で拒否された。
POST全般が必ずbusyになること、特定HTTP送信方式だけが原因であることは支持されない。
固定順と時刻は交絡しており、transport効果/占有時間率/過去runの判定瞬間を断定しない。
別途source監査ではLWS service前のcancel wakeupは既にあり、単純なtimeout0ブロックは今回の根拠にならない。
headersだけを処理してbody completion前に新pollが入る窓は存在するが、今回それが原因だったとまでは証明していない。

### 安全な終了と次の境界

05:30:37 UTCはSTOPPED/AUDIO_READY・offset null/NONE、capture IDLE/job0/受理frame/bytes0・解放済み、
REPEAT要求/実効・pending=false・背景ON/saved。設定fileはbackupとbyte一致した。
daemon PID46730/kiosk PID46755はactive/running・NRestarts0、HTTP JS48749538.../CSS3d418784...を保持した。
新package/daemon hashは上記と一致し、直前の限定helper/ruleは撤去されたままだった。
start診断6回/受理0/cancel0/play0、音声/画面/実取得/取消/実reader解放/再生復帰は未確認。

offset0での実取得へ進むには、現binaryに固定した限定helperの対話sudo設置が必要である。
privateに同じobserve/offset0/restore/cleanupの4操作sourceとbootstrapを準備/転送したがroot設置はしていない。
helper SHA-256 `88463a08db4ab81017b92b249f800318838360dcf07d40f26b0c57936d78168b`、
rule `4d5f02380120b3373e32b02d3737f8952cf01d7e030d7fbbead0b49a5a0dfe98`。
対話認証の代替やsudoers迂回は行わず、この手順待ちで取得実行は保留する。
source/記録/AGENTS変更は未commit・未push、private候補/log/helperをrepositoryへ含めていない。

## PR #256 調停修正版の通常配備（2026-10-25）

利用者の継続指示後、package化前に利用制限で中断し、23:15 UTC台の再開指示を受けた。
保存worktreeはhead470ca4f・既存変更14 tracked fileと新header1件で、diff/logは保全されていた。
動作中のbackground processはなく、Release/Debug各72/72・反復・負例のlogを現物で再照合した。
live PRもhead470ca4f・OPEN/Draftのまま。利用者は実機利用可能・接続変更なし・操作中ではないと再確認した。
23:19:37 UTCのread-only観測はSTOPPED/AUDIO_READY・offset null/NONE、capture IDLE/job0/解放済み・
REPEAT/pending=false・背景ON/savedだった。旧配備db169795.../9da29d35...とactive service/NRestarts0を確認した。

旧候補/SDK/生成物を残し、private Arbitration領域へ正本toolをbyte一致でコピーした。
sourceはHEAD `470ca4f63bb60f908d786a82feb035b7ff5a0478`、dirty patch
`62b365d27729c53dc67090ff98c7e0bf51dbee7b545b1f55c6808d0cb9c50efb`で固定した。
未追跡`include/player_media_dispatch.hpp`も別のsource-extraへ保存し、SHA-256
`6f1cb466b219c152e40249d4943c126702b1abbaaf7f6843a21f5d415f2b8179`を確認した。
355 tracked fileと新headerの内容/modeを含むprovenance JSONは
`4063208e8bf332cb94694267bd82a9dcc434719ec5347a58ebefb339016116da`。
package作成前後に同一性を確認したsnapshotであり、以後の記録追記やCI成果物とは区別する。

既存image/SDK・source/SDK read-only・networkなし・CPU2/memory2GiBで、CMake build/installとCPack、
正本release-artifact.py/package_profile.pyを実行した。全22 payloadとstage、Release実CLI、依存同一性を検査した。
SDK/imageのdownload/rebuildやPi compileは行わず、private保存総量は1,910,696,910 bytesで4GiB予算内だった。
候補は60,300,644 bytes、source_dirty=true/publication=not-approved/hardware_verified=falseを保持した。
package SHA-256は`007bb8642b32eb8027a524f39af39722ed2a873f73abdebb685bd5a5efba8ca8`、
daemonは`2ecbcf92e38192c4847fe472110f39155563e5ab933d5bf0751e18e673c76869`。

23:24:02 UTCに再びSTOPPED・capture IDLE/job0/解放済みを確認し、Pi上のprivate
`picdplayer-arbitration-20261025-LYklke`へ旧package db169795...と設定を非上書き保全した。
正本deploy toolの無変更コピーで通常配備し、active service維持と実WPE起動を確認した。
配備後にpackage/daemon hash、設定fileのbackupとのbyte一致、HTTP JS48749538.../CSS3d418784...を照合した。

23:26:11 UTC台からoffset未指定のまま10秒・83標本をread-only取得した。
IDLE61/RUNNING22、拒否理由はoffset_unknown61/media_busy22で、PENDING/RESULT_PENDING/UNKNOWNは標本に現れなかった。
これは有限標本であり未観測状態が存在しない保証でも、旧runとの因果的な改善率比較でもない。
全標本job0/受理frame0、取得/取消/再生のPOSTは各0回。取得を予約/再試行していない。
観測後もSTOPPED/AUDIO_READY・offset null/NONE、capture IDLE/job0・busy=false/reader_released=true・
call_inflight=false/waiting_for_access=false/accepted frames/bytes0・外部照合NOT_CHECKEDだった。
REPEAT要求/実効・pending=false、背景ON/saved、daemon PID44932（23:25:09 UTC起動）・
kiosk PID44975（23:25:10 UTC起動）はactive/running・NRestarts0。温度58.534度を終了時点で観測した。

このrunでoffset/既存引数/権限を変更せず、新たなhelper/ruleも設置していない。
音声/画面/物理入力、修正版での実開始・競合率改善、取得/取消/取得reader解放/再生復帰は未確認のまま。
旧package/設定とhostの候補/provenance/logはprivateに保持し、source/記録は未commit・未pushである。

利用者はこの配備版での単発取得試験を別途承認した。新daemon2ecbcf92...へ固定した同じ4操作の
限定helperをprivate準備/転送し、構文/hash/元引数保持を確認した。
helper SHA-256は`3d9af36b3344acbb925a2d5dac06c098afc4103ffa9f93ea120313f86340f901`、
ruleは`e81860ebb925fe66e6755c4d816163b3067f2cfb01c219f2fac88711e3995f77`。
この時点は利用者の一度の対話sudoによる設置待ちで、offsetは未指定、取得POSTは0回のまま。
受理時だけ2秒以内の取消/最大10秒の解放待ち、拒否時は再試行せず復元し、音声/画面は未確認として扱う。

利用者の対話sudoで専用ruleの構文とhelper初期化が成功した。23:37:55 UTCにroot所有/mode/hash、
限定observe成功（旧PID44932・drive FD0/race0）、温度59.072度・throttled flags0x0を確認した。
元設定をprivate before-trialへ保全し、helperで明示offset0だけを追加、通常daemon/kiosk再起動を行った。
0は校正値ではない。API再生は受理/解放後の別合図まで送らない条件を保持した。

23:38:34.922 UTCの前状態はSTOPPED/offset0・capture IDLE/job0/解放済み、media_activity RUNNING/media_busy、
PID45530のdrive FD0/race0だった。clientの別GETを予約とはせず、ownerが全gateを再評価する明示startを1回だけ送った。
23:38:34.953 UTCにHTTP409/capture_not_readyで拒否され、同34.975 UTCのGETもRUNNING/media_busyだった。
job0/受理frame0のままで、cancelや再試行は送らなかった。今回の前後の観測は既存media処理との競合と整合し、
修正対象である「新しい定期pollより先にready要求を処理すること」が失敗したとは判定できない。
POST判定瞬間の内訳は応答に含まれないため、原因や受理率改善をこの1要求で断定しない。
実取得・取消・取得reader解放、2秒取消/10秒解放waitの実機正例は引き続き未検証。

拒否時終了の合意どおり、元のoffset未指定へ復元し通常daemon/kiosk再起動を行った。
23:40:32 UTCの終了確認はSTOPPED/track_number1/position_frames0/AUDIO_READY・offset null/NONE、
capture IDLE/job0・busy=false/reader_released=true/call_inflight=false/waiting_for_access=false・accepted frames/bytes0・
media_activity IDLE/start_rejection offset_unknown/外部照合NOT_CHECKEDだった。
REPEAT要求/実効・pending=false、背景ON/saved、設定fileのbefore-trialとのbyte一致を確認した。
終了FD0/race0は未取得の基線であり、取得readerの解放正例にはしない。
専用helperのcleanupで今回のhelperとruleだけを撤去し、両path不在を確認した。private backup/code/logは保持した。
package007bb864.../daemon2ecbcf92...・HTTP JS48749538.../CSS3d418784...を維持した。
daemon PID45758/kiosk PID45789（23:39:59 UTC起動）はactive/running・NRestarts0、温度61.224度・flags0x0を観測した。
start POST1回/受理0・cancel0・play0で終了し、音声/画面/物理入力と取消後再生復帰も未確認である。
今回のsource/記録は未commit・未push、master/PR状態/Issueも変更していない。

## PR #256 明示要求優先の調停（2026-10-25）

利用者はキュー方式の検討に対し、既に届いた明示要求を新しい定期pollより優先し、
busy中のstartは予約せず拒否する推奨方式の検討/実装を指示した。
head470ca4fと既存の診断/実機記録差分を保持し、保存worktreeだけで実装・検証した。
この作業ではPi接続・配備・取得/再生再試行・権限/設定変更・commit/pushを行っていない。

### 実装範囲

内部`player_media_dispatch.hpp`の本番調停を実ownerと回帰で共有し、テスト側だけの順序再現を避けた。
既存のmedia結果適用・TOC/世代更新・engine失敗反映は移動せず、API serviceとcapture poll/confirmの後に
期限到来の新しい定期observeだけを投入する。取消がconfirmより先という順序も維持する。
投入可否はAPI後のdevice退役/現在性・capture busy/実解放・probe完了・PLAYING・eject待ち/実行中から再取得し、
loop冒頭のcached may_access_deviceは流用しない。次回期限はservice後の時刻+500msで、抑止/受付不可でも進める。
probe/TOC/既存pending eject/speed/idle stopは従来の順序であり、実行中/受付済み/未回収結果を破棄しない。
開始gate、POST/manifest、per-call coordinator、slot数、予約/自動再試行の禁止は変更していない。
既存処理が残る時やservice後に届いた要求はなお409になり得る。GETはidle窓を予約しない。

### 回帰と検証

- 実MediaWorker/CompletePcmSessionと共有coordinatorで、定期poll期限とready startが同じowner phaseに揃う条件を作った。
  start受理後は定期observeが未実行なだけでなく、pendingとしても投入されないことを確認した。
- readをblockしたcaptureの取消後もbusy/unreleased中はpollなし、reader破棄/実解放後だけ定期pollが再開する。
  cleanup単体を別gateでblockした試験ではなく、worker側の既存close/consumer cleanup回帰と組み合わせた検証である。
- 未回収結果での拒否、結果回収後の自動startなし、適用したno-discによる新要求拒否を確認した。
  fake clockでAPI後の時刻/可否、非期限時もserviceすること、GETがpollを抑止/予約しないことを確認した。
- 最初のcompileはfixtureのApiCommandResult初期化/cancel interface誤用で失敗した。
  logを残し、既存の型/command enum/session.cancelへ訂正した。本番APIを試験に合わせて変更していない。
- 既存image/SDK・networkなし・source/SDK read-only・CPU2/memory2GiB・private出力で、
  今回の調停差分を含む標準WPE Release 72/72、Debug 72/72が成功した。
  media_worker/complete_pcm_sessionの同じ2試験は両profileで各30回反復して成功した。
- API/metadata/WPE無効のDebugもdaemonと共通workerをcompileし、media_worker/NO_DISC daemonの2/2が成功した。
- private cloneでpollをserviceより先に戻す負例、API前の古い可否を保存して使う負例は、
  それぞれcompile成功後に新しい2試験が失敗した。負例後の正の3対象再実行は3/3成功した。
- 独立read-only reviewに追加指摘なし。回帰は実際の調停関数を呼ぶが、service/可否callbackと
  no-disc適用はfixtureが供給するため、実player loop全体をhardware置換して実行した証拠ではない。

詳細設計/API契約/integrity仕様を実装の順序に同期した。
既存候補package/stage/logを保持し、private作業buildだけを増分更新した。SDK/imageのdownload/rebuildはない。
この優先順変更は未package・未配備・未commit/未pushであり、最後に配備したdb169795.../9da29d35...には含まれない。
実機での開始成功・競合率改善、取得/取消/取得reader解放/再生復帰、音声/画面は未確認のままである。

## PR #256 診断版配備とmedia activity観測（2026-10-25）

利用者は再生操作は可能だが音声確認はできないとして継続を指示した。
09:01:39 UTCのread-only再確認はSTOPPED/AUDIO_READY・offset null/NONE、capture IDLE/job0・
busy=false/reader_released=true/call_inflight=false/waiting_for_access=false・accepted_frames0だった。
REPEAT要求/実効・pending=false・背景ON/saved、service active/running・NRestarts0を確認した。
この時点の旧配備にはmedia_activity fieldはまだ存在していなかった。

検証済みRelease buildからCMake install/CPackと正本release-artifact.py/package_profile.pyを使い、
新しいprivate Diagnostic領域で候補を作成した。既存build/stage/package生成物は上書きしていない。
標準WPE/Release・sourceとSDK read-only・networkなし・CPU2/memory2GiB、既存image/SDKのみで実施した。
候補作成後のprivate保存総量は1,219,971,757 bytesで4GiB予算内。download/SDK再build/Pi compileは行っていない。

未commit sourceはHEAD `470ca4f63bb60f908d786a82feb035b7ff5a0478`とdirty patchで固定した。
patch SHA-256は`acea405827829fa77514c473dbc4cf252607ceb538f19d7ecc04935c6e121743`、
355 tracked fileのhashを含むprovenance JSONは`a7e057dbc4f21134093536aa0afb90e4396a1efbc963cd2986019011ed1c3c73`。
作成前後に一致を確認し、canonical manifestはsource_dirty=trueとした。新headのCI成果物とは扱わない。
これらはpackage化時点のsnapshotであり、以後の検証記録追記まで含む現在のdirty tree全体とは区別する。
候補は60,301,092 bytes、package SHA-256は
`db1697956a5c75aa629fff91d4a9098c29edbad603c6f62b6cead149e242c60c`、
daemonは`9da29d35a5e66e405a710e6a7057695d7a0b13806bbbd872ae07d9cfe8eed15e`。
全22 payloadがCMake stageと一致し、Release実CLI profileと旧配備版とのDepends同一性も確認した。

09:13:42 UTCに再度STOPPED・capture IDLE/job0/解放済みを確認し、Pi上のprivate
`picdplayer-media-diag-20261025-LYklke`へ直前package51723eb1...と設定を非上書き保全した。
正本deploy toolのbyte一致コピーから候補を通常配備し、active service維持と実WPE起動を検証した。
配備後のpackage/daemon hash、HTTP JS48749538.../CSS3d418784...、設定fileのbackupとのbyte一致を確認した。
offset変更・取得/取消・再生のPOST、新たなroot helper/ruleの設置はこのrunでは行っていない。

09:16:31 UTCから10秒のread-only観測を行い、83標本でIDLE59/RUNNING23/RESULT_PENDING1を得た。
media_busyは24標本、offset_unknownは59標本、PENDING/UNKNOWNは0標本だった。
全標本job0/accepted_frames0で、取得を予約していない。標本間隔はsleep約100ms＋HTTP約15-26msであり、
件数比を連続した占有時間比と解釈しない。RUNNINGは約500msの媒体確認周期と整合する。
恒常的なbusyではなくidle窓があることと、結果回収待ちも実機で一度観測できた。
operation種別や実driver I/O時間はfield単体では断定せず、過去の失敗時点のphaseを遡って確定もしない。

09:25:35 UTCの終了GETはSTOPPED/AUDIO_READY・offset null/NONE・REPEAT要求/実効/pending=false・背景ON/saved、
capture IDLE/job0・busy=false/reader_released=true/call_inflight=false/waiting_for_access=false・accepted frames/bytes0、
media_activity=IDLE/start_rejection=offset_unknown/外部照合NOT_CHECKEDだった。
daemon PID42414/kiosk PID42440は観測中active/running・NRestarts0。直前packageと設定のprivate backupは保持した。
音声・画面/物理入力・実PCM受理/取消/取得reader解放/その後の再生復帰は未確認のままである。
候補/provenance/code/logはrepository外、sourceと記録は未commit・未pushで保全している。

利用者は次に単発start API試験へ進むことを明示承認した。別の事前GETだけで無送信終了する方式ではなく、
既存ownerが全安全gateを再評価する明示startへ1回だけ渡し、拒否時は終了、受理時だけ早期取消/解放待ちを行う。
backend gateの緩和、開始予約、自動再試行は追加しない。音声は未確認、再生は解放後の合図まで実行しない。
新binary固定の4操作helperをprivateに準備/転送し、source hash/構文/元引数保持を確認した。
helper SHA-256は`4478527af71e4cff43a77cf478e6ecbd07d40c292b397f6d4f51d59b715eb675`、
ruleは`8266e4c25819eca2acd0427d32abba587674c465279fcfeb08aa353400ea6871`。
設置は利用者の一度の対話sudo待ち。この時点ではoffset未指定のまま、start/cancel POSTは0回である。

### 単発owner API試行と復元

利用者の対話sudoで専用helper/ruleの設置が完了した。今回は専用ruleのvisudo構文だけを検証し、
既知の既存deploy ruleのmode不整合を修正したり全体検証成功と扱ったりしていない。
09:50:26 UTCにhelperのroot所有/0755/hashと専用rule0440・限定observeの成功を確認した。
設定をprivate `config.before-trial`へ保全し、温度60.148度/throttled flags0x0を観測した。
元引数を保持してhelperでoffset0だけを一時追加し、通常daemon/kiosk再起動を行った。

承認された新手順では、read-only GETのcan_startやFD基線で開始を予約せず、通常再生STOPPED・AUDIO_READY・
明示offset0・capture未開始/解放済み・profile一致を確認した上で、全安全gateを再評価するownerへstartを一度だけ渡した。
既存worker admissionやmedia_idle条件を緩めたのではない。
09:51:52.858 UTCの前状態はIDLE/job0・media_activity RUNNING/media_busy・FD0/race0だった。
09:51:52.889 UTCにstartのHTTP409/capture_not_readyを受け、再試行もcancelも送らず終了した。
約24ms後のGETはIDLE/job0・media_activity IDLE/can_start=trueだった。
短いmedia処理との競合と整合するが、POST応答には判定瞬間のactivity内訳はなく、そこを遡って断定しない。
このrunのstart POSTは1回、受理0回、cancel POST0回、job0/受理frame0のままである。
実取得・取消・取得reader解放は未検証で、期限/取消/解放wait部分も実行していない。

拒否時終了の合意に従いoffset未指定へ復元して通常再起動し、09:55:49 UTCにnull/NONE・STOPPED・
capture IDLE/job0/解放済みと設定fileのprivate before-trialとのbyte一致を確認した。
専用helperのcleanupで今回のhelper/ruleだけを撤去し、両path不在を確認した。保全物は保持している。

### 通常API再生確認の手順不備

利用者は、取消後復帰とは別の診断版通常再生について5秒のAPI進行確認を承認した。音声は確認不可。
10:00:38.613 UTCのPOST /api/playは204で受理されたが、観測scriptが誤って202を期待して例外となり、
その分岐で所有flagを解除したため予定のfinally STOPへ進まなかった。これは製品側APIの不具合ではなく試験手順の誤り。
再生を再要求せず、10:01:31.506 UTCにPLAYINGを観測して承認済みSTOPを一度送信した。
10:01:31.525 UTCのSTOP204、10:01:31.820 UTCのSTOPPEDを確認した。
したがって予定5秒に対し約53秒のPLAYINGとなり、試験時間を逸脱した。成功扱いにせず、この失敗とlogを保持する。

さらにscriptが内部serializerのplayer.track/position_lbaを参照していたため、null値を進行不能の根拠にしてはならない。
公開契約はplayer.track_number/position_frames/track_duration_framesであり、進行測定自体が未成立だった。
追加再生は行わず、privateな修正版で204の受理・公開field参照・観測例外時にもSTOP一度となることをfake APIだけで確認した。
修正版をPiで再実行したという主張はしない。音声・実進行・取消後再生復帰は未検証である。

10:06:20 UTCの最終確認はSTOPPED/track_number1/position_frames0/AUDIO_READY、offset null/NONE、
capture IDLE/job0・busy=false/reader_released=true/call_inflight=false/waiting_for_access=false・accepted frames/bytes0・
media_activity IDLE/start_rejection offset_unknown/外部照合NOT_CHECKEDだった。
REPEAT要求/実効・pending=false・背景ON/saved、設定byte一致、helper/rule両path不在を再確認した。
package db169795.../daemon9da29d35...・HTTP JS48749538.../CSS3d418784...を維持した。
daemon PID43340（09:54:46 UTC起動）/kiosk PID43370（09:54:47 UTC起動）はactive/running・NRestarts0。
温度61.762度/throttled flags0x0を終了時点で観測した。取得start1回/受理0・cancel0・通常play1/stop1で終了し、追加試行していない。
source/記録は未commit・未pushであり、master/PR状態/Issueは変更していない。

## PR #256 media_busy監査と診断差分（2026-10-25）

利用者は手動再生開始は正常と報告し、離席中のsource監査と必要な修正を依頼した。
この申告は通常再生経路の情報であり、取消後の復帰や音声/画面の正例とは区別する。
監査基準は保存worktreeのhead470ca4fと既存記録差分。離席後はPi接続・配備・取得再試行・
権限/設定変更・commit/pushを行っていない。08:42 UTCの中断後再開でも同じhead/差分と検証logを再照合した。

### 監査結果と最小診断修正

MediaWorkerのidle条件は実行中/受付待ち/未回収結果が全てないことであり、FDが閉じた後でも結果が残ればfalse。
main loopは500ms間隔の媒体確認をAPI処理より前に要求し、結果回収後にAPIを処理する。
worker完了とowner結果回収の時点差はあるが、mutex下で未回収結果をidleへ誤変換する破綻は見つからなかった。
通常再生は同じmedia idle admissionを要求せずcoordinatorで直列化するため、再生成功と取得用media_busyは両立する。
GET /api/complete-pcmは要求時のcontextを評価し、POSTも再評価する。GETの成功は予約ではない。
永久的な開始不能、workerの排他/取消/解放破綻、drive故障はsourceと既存証拠からは立証できなかった。
API前の新しい定期pollがidleを消費する経路はあるが、今回の実機ではPOST自体が未送信であり、
それを根本原因と断定してschedulerを変更するだけの証拠はまだない。

診断不足に対する最小差分としてread-only statusへ`media_activity`を追加した。
IDLE/PENDING/RUNNING/RESULT_PENDINGを一回のmutex観測で取り、ownerは同じ値からmedia idle gateと説明値を作る。
内部callerが内訳を供給しない場合はUNKNOWN。RUNNINGはcoordinator待ちやcallback前後も含む。
元のidle判定（!busy && !pending && results.empty）、schema version、POST契約、result manifest、
開始/取消、定期pollの順序/頻度、実I/Oは維持し、開始予約や自動再試行を追加していない。
IDLEは他gateの充足や実FD/driveの所有の保証ではない。この診断差分は未配備であり、
最後に配備確認したRelease package51723eb1.../daemon35edfaf7...には含まれない。

### 回帰と負例

- gated fake callbackと実MediaWorkerでRUNNING、callback終了後のRESULT_PENDING、pop後のIDLEを区別した。
  fake deviceが閉じた後も結果回収前はidle=false/次request拒否となる。固定sleepを状態成立の証明にはしていない。
- 実MediaWorker/CompletePcmSessionと共有coordinatorでowner drain後の未回収結果からmedia_busyを再現し、
  capture factory/probe/job0のまま拒否、結果回収後もpoll/GETだけではjobが始まらず新しい明示startだけ成功することを確認した。
- admitted captureのfake readをblockしたままmedia idleを失わせ、cleanup前はbusy/unreleasedを保持し、
  cleanup後も結果非公開・idle復帰による自動再開なしを確認した。
- 既存NO_DISC実daemon試験で、実ownerがUNKNOWN以外の新fieldを供給し、取得拒否を維持することも確認した。
- private build cloneで未回収結果をIDLEへ変えた負例はcompile成功後に2試験が失敗し、
  取得中のmedia競合失効だけを外した別負例もcompile成功後にsession試験が失敗した。
  正のworktree/buildは変更せず、負例後の正の3対象再試験は3/3成功した。

### Linux/aarch64検証と未確認

既存image035cc877...とWPE SDK2.54.0を使用し、source/SDK read-only・networkなし・CPU2/memory2GiBで、
private `pr256-media-audit-20261025.LYklke`へ標準WPE Release/Debugを別々に構成/buildした。
既存build/stage/package/SDK生成物は変更せず、SDK/imageのdownloadや再buildもしていない。
最初のRelease全回帰は71/72で、build_profileだけがcached imageのNinja欠如により前提不足で失敗した。
既存profile検証imageから実Linux arm64 Ninja1.12.1をprivateへコピーし、architecture/hash/実行を確認して
元imageへread-only供給した。試験をskip/緩和せず、元の失敗logも保存した。
最終的に今回差分を含むRelease 72/72、Debug 72/72が成功した。
media_worker/complete_pcm_sessionの同じ2試験は両profileで各30回反復して成功した。
独立したread-only reviewでも実装/負例の追加指摘はなかった。未変更箇所のmissing-field-initializers警告は残る。
出力/正負build/logの保存量は中断前の標本638,113,649 bytesで計画した4GiB未満。補助toolとlogはrepository外に保持した。
再開後、共有component回帰の結果がcontextと同じobservation世代であることを明示した。
最初はrequestの引数数を誤ったfixtureでcompile失敗したが、そのlogを残し、既存のinvalidate(generation)と
request(work)へ訂正した。本番のrequest interfaceやgateは変更していない。
この最終fixtureでもRelease/Debugを再compileし各72/72・境界2試験各30回反復・両負例の期待した失敗を再確認した。
再開後のdu標本は637,516 KiBで同じ予算内。不要な再download、既存生成物の削除、実機操作は行っていない。
今回のsource・回帰・契約・設計・検証記録は未commit/未pushである。
全てhardware非依存の検証であり、実機の新field観測・根本原因特定・取得/取消/実reader解放/再生復帰は未検証のままとする。

## PR #256 Gate C実機試行と復元（2026-10-25）

今回の結論は、Release候補配備と明示offset0の適用/未指定への復元は確認できたが、
2回の利用者合図後の直前確認がmedia_busyで止まり、取得POST0回/取消POST0回だった、である。
実PCM受理・取得readerの取得/取消/実解放・その後の通常再生復帰は未検証のまま残す。
画面/物理リモコンは後回し、音声も確認不可の枠であり、これらを成功扱いにしない。

### 限定権限の設置確認とRelease配備

利用者の対話sudoで専用helperの初期化と専用ruleの構文検証は成功したが、bootstrapの全体visudo検証は
既存`picdplayer-deploy`のmode警告で停止した。bootstrapを再実行せず、read-onlyで状態を確認した。
専用helperはroot/root・0755・既知hash、専用ruleはroot/root・0440で、限定observeは実行成功した。
既存deploy ruleはroot/root・0640であり、変更していない。全体visudo検証は未成功のまま区別する。
当初設定と旧Debug packageは未変更で、PID38060のdrive FD0/race0を独立に観測できた。

01:33:02 UTCにSTOPPED/AUDIO_READY・capture IDLE/job0/解放済み・FD0/race0を再確認した。
温度60.686度、vcgencmd throttled flags0x0だった。
正本deploy.sh/package_profile.pyのbyte一致コピーをprivate stagingから実行し、
既存containerでのRelease profile検査、候補upload/dpkg、active service維持と実WPE起動を確認した。
元のpackage-container/Debug生成物は変更せず、新規build/downloadも行っていない。
sourceは候補merge commit `24c5485366d06abe01f11254db1491b0eee1fc4b`、旧基準ec9fe964と同じtreeであり、
現在head470ca4fの全source treeから作ったpackageとはしない。

01:34:43 UTCの照合で、配備packageは
`51723eb11b10dd942c9bb049e76632d4fe0d4d828acbe7231c75795d710b394c`、
install先daemonは`35edfaf78bd60cdbac322b73213355d9b34f216bcc004e3cd99f93e64c5968ba`で一致した。
HTTP JS `48749538...`/CSS `3d418784...`、REPEAT要求/実効・pending=false、背景ON/savedを保持し、
設定fileはprivate backupとbyte一致した。helper観測対象は新daemon PID40393へ移っていた。
この時点のmedia_busy/drive FD1は通常media処理もあるため、取得readerの観測とは扱わない。

### 一時offset0と未送信の直前確認

限定helperで元引数を保持したまま明示offset0だけを追加し、通常daemon/kiosk再起動を行った。
01:35:49 UTCのAPIはSTOPPED/AUDIO_READY、offset0/USER_CONFIGURED、capture IDLE/job0、
REPEAT/pending=false・背景ONを示した。0は校正済み値とは扱わない。
この時点はmedia_busy・PID40731のdrive FD1であり、開始要求は送らなかった。

開始を予約しない8秒上限のread-only観測を行い、01:37:01 UTC台の最初の標本で
can_start=true・拒否理由なし・同PIDのFD0/race0・capture解放済みを観測した。
利用者の合図後、01:46:18 UTCの直前確認はmedia_busyだったためguardで停止した。
そのときFD0でもownerのmedia_idle不成立を無視せず、start/cancelは各0回だった。
再びread-onlyで01:47:38 UTC台にcan_start=true/FD0/race0を観測し、その後の利用者は
追加の手動要求を1回だけ、再度不成立なら復元終了する条件を承認した。
02:05:14 UTCの直前確認もmedia_busy・FD0で、2回目もstart/cancel各0回のまま終了した。
API202、job作成、取消時刻、実受理frame、取得reader解放の正例は一つも得ていない。
観測窓の成立と後の要求直前の成立は別時点であり、前者を後者の保証にしてはならない。
read-only標本はAPI→FDの順、直前guardはFD→state API→取得statusの順で、各標本全体に約0.66-0.72秒かかっている。
同一時刻の原子的観測ではなく、この順序/時点差の影響も未確定の診断点として残す。
予約・自動再試行・取得gateの緩和は行わなかった。

privateな一回限りのscriptは10秒job/早期取消/有限解放待ち用だったが、実行は両回とも
preflightで止まっており、取消/解放待ち部分の実機動作は未検証である。
1回目のcode/logを保全した上で、2回目は別logへ保存した。root観測の実測時間を踏まえた
観測timeout調整も行ったが、開始gate・2秒取消上限・POST回数制限は変更していない。
生PCM、外部照合、予約jobや自動再生は追加しなかった。

### 復元と終了状態

合意どおりこれ以上試さず、限定helperで元のoffset未指定へ戻し、通常daemon/kiosk再起動を行った。
02:07:50 UTCの最終APIはSTOPPED/AUDIO_READY・offset null/NONE、capture IDLE/job0・
busy=false/reader_released=true/call_inflight=false/waiting_for_access=false・accepted frames/bytes0・
offset_unknown/外部照合NOT_CHECKEDだった。
REPEAT要求/実効・pending=false、背景ON/saved、HTTP JS `48749538...`/CSS `3d418784...`も維持した。
daemon PID41322（02:06:33 UTC起動）・kiosk PID41352（02:06:34 UTC起動）はactive/running・NRestarts0。
live offset指定0組、custom-ui一組、設定file全体のprivate backupとのbyte一致を確認した。
温度62.300度、throttled flags0x0をこの終了時点で確認した。連続した温度/電源監視の保証ではない。
終了FD1は通常mediaアクセスがあり得る基線として保持し、存在だけで取得readerとはしない。

Release package `51723eb1...`/daemon `35edfaf7...`を維持し、旧Debug package `dd1b0c54...`のprivate backupも保持した。
専用helperのcleanupで、自分のroot helperとsudoers ruleだけを撤去し、両path不在を確認した。
root/userのprivate backupとcode/logは保持し、既存deploy ruleの0640警告は別課題として変更しなかった。
取得/取消/再生のPOST合計は全て0回。音声、TV、物理入力、取消後の再生復帰は未確認である。
今回の実機事前確認・権限手続き・利用者応答待ちは当初の短い枠を超えたが、取得jobは開始していない。
新head470ca4fのCI run40148726750は終了時のmetadata再取得でSUCCESSだった。実機試験や旧候補の証拠とは別にする。

次の診断点は、周期的media処理・公開can_start snapshot・要求直前の時点差を有限の同runで照合することである。
FD0だけをmedia_idleやplayback解放の保証へ昇格させず、開始予約/自動再試行で回避しない。
今回の記録は新たな未commit差分であり、追加commit/push・master統合・PR ready/merge・Issue closeは行っていない。

## PR #256 Gate C事前確認と権限待ち（2026-10-25）

利用者は画面/物理リモコンなしでの取得試験を依頼し、候補Release配備・既存sudoによるPID限定FD観測・
一時offset0・10秒jobを一度/遅くとも2秒で一度取消/最大10秒の解放待ち・元offset未指定への復元を承認した。
音声確認はできないため、解放後の再生復帰は別の合図でAPI進行だけを確認し、音声成功は主張しない。
画面/物理入力は後回しのままとする。

headは`470ca4f63bb60f908d786a82feb035b7ff5a0478`・開始時clean、PR OPEN/Draftを再取得した。
新headのCI run40148726750は確認時IN_PROGRESSで、旧CIの成功で代用していない。
候補ZIP `1518a3c9...`/Release deb `51723eb1...`の現物hash一致と、既存arm64検証image
`sha256:035cc877b414fcfe36f7e1f279f67653433e5cf877240d30d2cacbe401b4d90d`の存在を確認した。
新規download/buildは行っていない。候補は引き続き旧基準ec9fe964と同じsource treeである。

01:04:49 UTCの既存pin厳格照合read-only観測はSTOPPED/AUDIO_READY・offset null/NONE、
capture IDLE/job0/busy=false/reader_released=true/call_inflight=false/waiting_for_access=false/accepted_frames0・offset_unknown。
REPEAT要求/実効・pending=false・背景ON/saved、daemon PID38060/kiosk PID38105ともactive/running/NRestarts0だった。
旧package `dd1b0c54...`/install先daemon `1137505a...`は不変。Piのhomeと/usr/localは約45.8GB空きだった。

既存生成物`package-container/`のDebug packageは動かさず、private directory
`pr256-gatec-exec-20261025.flreD6`内へ正本deploy.sh/package_profile.pyをbyte一致でコピーし候補だけをstageした。
一時SSH wrapperは既存pinを強制し、container wrapperは既存image/pullなし/networkなし/rootfs read-only・
CPU2/memory1GiB/tmpfs512MiBで検証する計画とした。正本配備script本体は変更せず、まだ実行していない。

01:12:30 UTCからPiのprivate `picdplayer-gatec-20261025-flreD6`（0700）へ現在設定のbyteコピーと
旧packageを0600で保全した。後続のread-only照合で設定backupとliveの一致、旧packageのhash一致を確認した。
`sudo -n -l`のcommand照会は成功したが、実際の承認済みroot FD観測は失敗した。
FD操作を再実行せずsudo policyをprivateに照合したところ、dpkgにはNOPASSWD規則がある一方、
findによる診断とinstallによる設定更新/復元にはNOPASSWD規則がなかった。許可対象であることを
非対話実行可能と誤認してはならない。別identity/認証迂回やsudoers変更は行っていない。
温度/電源の確認はその後に予定していたが、この失敗でそこまで実行しておらず未確認。

並行するhost側profile再検証も失敗した。正本validatorの同条件診断で、展開先/tmpのcdplayerd実行が
PermissionErrorとなることを確認した。候補CLIは実行前に止まっている。
mount情報は/tmpがrw,nosuid,nodev,noexecであり、wrapperでexecを明示しなかった一時領域の制約だった。
候補破損や再build必須とは判定せず、exec許可を自動追加して再試行もしていない。
既存CIのprofile成功・前日のoffline検査と、今回の実行未確認を区別する。

この時点では候補をPiへ転送/配備せず、offset変更・service restart・取得/取消・再生操作も0回。
old Debugと元設定を維持したまま、限定診断/設定変更の認証方法と検証tmpfsのexec許可を再確認する。
helper/ruleの恒久追加や一般sudo権限の拡大は行わない。失敗・未実施を成功に変更しない。

利用者は、一度の対話sudoによる一時限定helper/ruleの設置手順と、検証tmpfsのexecだけを明示承認した。
networkなし/rootfs read-only/capabilityなし/CPU2/memory1GiB/tmpfs512MiBを維持してexecを明示したところ、
正本package_profile.pyによる実CLI検証は成功し、build_type=Release/profile=Release/version=0.1.0を確認した。
新規buildやdownloadは行わず、Pi/hostで候補binaryを直接実行していない。

repository外に一時helper `picdplayer-gatec-flreD6`と固定4 commandだけのsudoers source/設置scriptを準備した。
許可対象は対象PIDのFD read-only観測、固定offset0の追加、保全した元設定への復元、自分の許可/実行fileの撤去だけ。
root所有の保全先・設定hash一致guard・STOPPED/取得解放guardを持ち、任意path/offset/commandは受け付けない。
private backupは撤去後も保持する。helperはsource treeへ追加せず、root所有に設置するまで有効なsudo許可はない。
helper SHA-256は`68d9274bfac2a84f5c1dd4bc45b5d7928248c05440e3545d6591dc34a228e495`、
rule SHA-256は`d59c7651c24f5d4cb1e446cef92f51adb93dc03db569dc64c762789cea59e356`。
helper/設置scriptの構文、引用付き元引数保持と既存0/非0 offsetの拒否をローカルで確認し、
Pi上では転送hash一致と実設定形式の純粋変換だけを非rootで確認した。実設定は書き換えていない。
利用者の対話sudoによる設置待ちであり、候補配備・offset0適用・取得/取消は引き続き未実施。

## PR #256保存点（2026-10-25）

利用者指示でUI実装/回帰試験/機能仕様を`e4d86e1`へ保存し、実機検証記録/backlogを別単位で保存する。
保存前にMacでcontrols/renderの2 Node回帰、player/controls/renderの3 JS構文検査、`git diff --check`を再実行して成功した。
今回C++全build・全CTest・新headのCI成功は確認しておらず、旧CI結果で代用しない。
この保存作業ではPiへ接続せず、実機状態の最新確認は2026-10-24 18:00:49 UTCの記録である。
Settingsの二軸移動・上下ループ・初期scroll・見出し統一・設定値と操作focusの分離を実装し、
Node回帰、実WPE合成、利用者による限定代表確認を記録した。最終配色とv1全体の物理受入は未完了。
実機UIはJS `48749538...`/CSS `3d418784...`を確認済みだが、backendは旧Debug package
`dd1b0c54...`/install先daemon `1137505a...`のまま、offsetは未指定である。
CI候補Release package `51723eb1...`は取得/offline検査済み・未配備。source tree一致を確認した対象は
旧PR head `ec9fe964...`と候補merge commit `24c548...`であり、今回のUI保存後の新head全体ではない。
Gate Cの配備/診断権限/offset0/取得・取消・実解放待ち・手動再生復帰は別承認で未実施。
#238/#239はOpen、PR #256はDraft/master未統合を維持する。privateなpackage/設定/key/画像/helperはGitへ含めない。
以下の「未commit」「未配備」等は各run時点の記録として保持し、後続の保存/反映と混同しない。

## PR #256 scroll修正反映と下端ループの追加指示（2026-10-24）

利用者がscroll修正の実機反映と継続を明示指示した。
基準は`ec9fe964a9dcc31ac8cd26e1ced593c3d92f77e8`に未commit UI差分を加えたもの。
15:15:29 UTCはSTOPPED/AUDIO_READY・offset null・SINGLE要求/実効/pending=false・背景OFF/saved、
capture IDLE/job0/busy=false/reader_released=true/accepted_frames=0・offset_unknownを再確認した。
直前JS `98c343f8...`を既存upload内の`player.before-scroll-20261024-1515.js`へ非上書き保全し、hash一致を確認した。
scroll修正JS `92b46dee4965a368fa03d5203e8233934e6332bd4b3130dbd2b30570a75c618b`だけを転送した。
CSS `30a94864...`と他5 assetは不変で、7 assetの通常file・0644・hash/余分なentryなしと
custom-ui一組・元引数保持・その他設定行不変を確認した。
15:16:08 UTCのmedia_busyでは再起動しなかった。15:16:23 UTCの単発再観測は
STOPPED・capture解放済み・offset_unknownであり、daemon再起動を1回実行した。
HTTP HTML/CSS/JSの配置先とのbyte一致を確認後、kioskも1回再起動した。

15:17:32 UTCから実WPE合成操作で初回open直後/GET完了後、PCM focus後の再open直後/GET完了後を確認した。
4点ともscrollTop0・policy-single focus・見出し上端87.84px/surface上端20px・見出し可視だった。
下部focusではscrollTop126になり、下部からの再表示を実際に通した上で0へ戻った。
3見出し16pxも維持し、request観測は3 APIのGET各2件・POST0件。
通信遅延の注入はせず、実際のGET完了後を観測した。人工的な遅延条件はローカル回帰と区別する。
合成操作後はSettingsを閉じ、一時Inspector forwardを停止した。

物理確認に対し利用者は「基本的に問題は無いが、最下部（Local PCM）の下側は最上部（Back）にループしてほしい」と回答した。
scroll/基本操作の肯定を利用者観測として記録し、下端ループは追加要望として分ける。
Local PCMの下入力だけBackへ戻し、Back上入力/左右端では停止する方針を明示して継続した。
取得条件・offset・package/binaryの変更やPCM取得試験の承認とは扱わない。

### 下端ループから上下双方向ループへの反映

Local PCMの両buttonから下をBackへ送る最小差分を追加し、controls/render Node回帰・JS構文・diff検査は成功した。
下端版JSは`3f04dafe2f5cfbc72e5a3239d66bd1c29c6b2c3f7a4c090fea5189954ae25c32`。
15:22:17 UTCはSTOPPED/AUDIO_READY・offset null・capture IDLE/job0/busy=false/reader_released=true・
accepted_frames0/offset_unknownだった。直前JS `92b46dee...`をupload内の
`player.before-bottom-loop-20261024.js`へ非上書き保全した。
15:22:55 UTCに7 assetのhash/通常file/0644と停止解放状態を確認してdaemon再起動を1回、
HTTP HTML/CSS/JS一致確認後kiosk再起動を1回実行した。CSSと他5 assetは変更していない。
15:23:55 UTCの実WPE合成確認では、Acquire/Cancelのどちらからも下入力でBack focus・scrollTop0・見出し可視となり、
Backの上/左右は停止、下はpolicy-singleへ移動した。API GET3件/POST0件、終了時にSettingsを閉じる合成clickを行い、
一時forwardを停止した。

利用者は下方向のループを確認し、上方向がループしないと報告した。また、Gate Cは計画へ進むことだけを承認した。
このためBackの上入力を並びの末尾Local PCM Cancelへ送る1行差分を追加した。
左右端停止・行間の列保持・初期scroll reset・取得条件は維持する。controls/render Node回帰・JS構文・diff検査は成功した。
双方向版JSは`4874953872f69b13ee47cf15767a404f59d4d87ed06f24c54267c2b71d24bb81`。
15:29:58 UTCのcaptureはIDLE/job0/busy=false/reader_released=trueで、start_rejectionはmedia_busyだった。
直前下端版JSを`player.before-bidirectional-loop-20261024.js`へ非上書き保全した。
15:30:24 UTCのSTOPPED・解放済み・offset_unknownと7 asset整合確認後、daemonを1回再起動し、
HTTP一致確認後kioskも1回再起動した。いずれもUI-onlyでありpackage/binaryは更新していない。

15:31:18 UTCの実WPE合成確認は、policy-single → 上Back → 上pcm-cancel → 下Back → 下policy-single →
下background-off → 下pcm-start → 下Backが期待どおりだった。Back到達時は全てscrollTop0・見出し可視。
APIはread-policy/artist-background/complete-pcmのGET各1件・POST0件だった。
終了時にSettingsを閉じる合成clickとNetwork domain無効化を行った。
最新双方向版の物理入力確認と、最後の再起動後のservice/API終了snapshotは利用制限による中断前に未取得である。
旧snapshotで代用せず未確認として残す。

### 中断後の照合とGate C計画の前提

17:26 UTC台の利用者再開指示後、保存worktreeとlive PRを再取得し、双方head `ec9fe964...`、
PR OPEN/Draft、同headのCI SUCCESS、対象6ファイルの未commit差分を確認した。
local JSは`48749538...`、CSSは`30a94864...`で中断直前の反映内容と一致する。
中断で一時Inspector SSH forwardが残っていたため停止し、local listener不在を確認した。
合成確認script自体は中断前に正常終了しており、中断中の継続計測は行っていない。
Piの現在状態・最新UIの物理確認・利用可否は過去の観測で代用しない。

Gate Cは計画だけが承認され、offset変更・取得・取消・取得用restartは未承認/未実施である。
調査では、現配備package `dd1b0c54...`/daemon `1137505a...`は2026-10-20の未commit source由来で、
正確なclean commit/immutable dirty-source manifestへの対応は未確定だった。
checkpoint `997f866...`からhead `ec9fe964...`のsource差分はsettings読込のO_NONBLOCK追加だけだが、
これを現配備binaryとcheckpointの同一性の証明にしてはならない。
また、capture IDLE時のreader_released=trueはplayback readerの実解放の独立証明ではない。
Gate C計画では対象backendのsource identityと実reader観測の可否を先に解決し、
10秒のjob上限・早期cancel・有限release待ち・元のoffset未指定への復元・手動再生復帰を別承認する。
HTTP202やowner状態だけで実reader取得/実解放の成功を主張しない。

利用者は現在利用可能・接続変更なし・操作中ではないことを再確認した。
17:30:14 UTCの既存pin厳格照合によるread-only観測ではSTOPPED/AUDIO_READY・offset null/source NONE、
capture IDLE/job0/busy=false/reader_released=true/call_inflight=false/waiting_for_access=false・accepted frames/bytes0、
can_start=false/offset_unknown・外部照合NOT_CHECKEDだった。
ただしpolicy要求/実効はREPEAT・pending=false、背景要求/実効はON・savedへ変わっていた。
中断前のSINGLE/OFFへは戻さず、今後の保全/復元基準は現在値として扱う。
daemon PID37236（15:30:24 UTC起動）・kiosk PID37284（15:30:35 UTC起動）はともに
service user picdplayer・active/running・NRestarts0、実processはcdplayerd/picdplayer-wpeだった。
live daemonのcustom-ui一組・期待path・offset指定0組を確認した。
7 assetはhash/通常file/0644/uid-gid1000一致・余分なentryなし、HTTP JS `48749538...`/CSS `30a94864...`も一致した。
package `dd1b0c54...`、install先daemon `1137505a...`、profile Debugは不変であり、
元引数・custom-ui一組・その他設定行不変もprivate比較で確認した。
対象PIDのFD directoryを既存権限で観測する試みはPermissionErrorとなり、別identity/sudoで迂回していない。
独立した実reader観測の可否は未解決である。再開後のrestart・設定変更・取得/取消は0回。

### Gate C計画用の候補identityと資源確認

ダウンロードはせず、run39688998298のartifact metadataだけを取得した。
候補は`picdplayer-0.1.0-24c5485366d06abe01f11254db1491b0eee1fc4b`（artifact ID12388001225）、
59,942,271 bytes、未失効、archive digestは
`sha256:1518a3c927aee6924b2ee7ed241f4c0375083b390901406ffe7fdefd7758bfd6`だった。
workflow/manual上で保持される候補はReleaseだけであり、Debugへ黙って読み替えない。
候補sourceはPR headそのものではなくGitHubのmerge commit `24c548...`で、parentは05a1a3e.../ec9fe964...。
GitHub commit metadataのtreeを両者で照合し、候補とPR headがともに
`e61fc27a89fbf14b7e04dba7284174d383ed36aa`であることを確認した。
これによりPR headと同一source treeのCI候補を選べるが、download後のarchive/manifest/package/payload検査と
別承認の配備を行うまでは、現配備binaryのidentity問題が解決したとは扱わない。
新規C++/SDK buildやPi compileを計画の既定にせず、この約60MB候補をprivateな一時保存先へ取得・検査する準備段階を先に提案する。
候補準備はoffset追加・取得・診断権限変更の承認とは別にする。

取得段階の案は明示`--read-offset-samples 0`（校正値ではない）とnormal restart、
can_startとSTOPPED/output reset/queue取消/playback reader解放/media idle/pendingなしの確認後にstart1回。
POSTは現行session_id/disc_generationとprofile `local-pcm-sha256-v1`、duration_ms10000を使う。
10000は全域jobのdeadlineであり、10秒分のPCM取得指定ではない。sourceの許容範囲は1..10800000ms。
実活動を観測した時点、遅くともstart後2秒で同じsession/job_idへcancelを1回送る案とする。
先に完了/失敗した場合は取消正例とはせず、再試行しない。実活動が未観測なら実reader取得も未確認とする。
その後は最大10秒の有限read-only観測でbusy=false/reader_released=true/call_inflight=false/waiting_for_access=falseを確認し、
承認された範囲の独立FD観測と組み合わせる。解放不明・異常時はplay/eject/restartで帳尻を合わせず中断する。
解放確認後に利用者が通常再生を一度手動操作し、進行と音声を確認して手動STOPする案とし、
最後に元のoffset未指定へ戻し、必要なnormal restart後にnull/NONEとREPEAT/背景ON/最新custom UIの保全を確認する。
この案全体は未実行/未承認であり、取得条件を満たしたという主張ではない。

### 別承認された候補の取得・offline検査

利用者は最新UIの双方向ループ/scrollについて問題なしと回答し、一方で選択項目の視認性を指摘した。
追加確認では「設定値とfocusを混同する」という問題であり、別の未解決UI課題として保持する。
Gate Cについては候補取得/offline検査だけを承認した。Pi配備・profile変更・権限変更・offset変更・取得/取消は含まない。

private directory `pr256-gatec-preflight-20261024.PVL12v`（mode0700）を既存一時root内に新規作成した。
最初の2回の起動はbackground wrapperのshell/Python改行解釈エラーでdownload前に終了した。
private directory内の小さな取得helperへ切り替え、180秒timeout/64MiB file上限で55.4秒で取得した。
archiveは59,942,271 bytesで、GitHub metadataのdigest `1518a3c9...`と一致した。
ZIP内はmanifestとdebの2 regular fileだけで、path安全性/重複/保存容量上限を検査してから保存した。
保存総量は120,252,887 bytesで256MiB未満。Docker image/SDKのdownloadやbuildはしていない。

manifestはsource_commit `24c548...`/refs/pull/256/merge/source_dirty=false、Release/arm64/0.1.0、
publication=not-approved/hardware_verified=falseだった。PR headとの同一tree確認は前節のmetadataによる。
候補`picdplayer_0.1.0_Release_aarch64.deb`は60,302,100 bytes、SHA-256は
`51723eb11b10dd942c9bb049e76632d4fe0d4d828acbe7231c75795d710b394c`でmanifestと一致した。
Mac既存arとPython tar streamでcontrolのpackage/version/architecture/Dependsを照合し、
全22 payloadのhash/mode/symlink targetがmanifestと完全一致した。
regular payload 166,502,874 bytesはstreamで検査し、payload自体をfilesystemへ展開保存していない。
package markerはRelease、wrapperはdiagnostics=0で、両実binaryはELF64 little-endian AArch64だった。
daemon SHA-256は`35edfaf78bd60cdbac322b73213355d9b34f216bcc004e3cd99f93e64c5968ba`、
WPE SHA-256は`08d77e05f4caa01a66c2698ab0d0879e2393d1fab44ef43280f43cebcdbd9bd6`。
controlのInstalled-Sizeは481205 KiBであり、将来配備時のPi容量確認は別途必要である。
package内の実行file/maintainer scriptは実行していない。binary profileの動的検証は既存CIの結果であり、
今回のoffline検査をLinux実行/実機成功へ読み替えない。候補・helper・manifestはrepository外に保持した。
一時directoryの存在を将来の保全保証にはせず、再利用/配備前には現物とhashを再照合する。
候補は配備前で停止し、現配備package Debug `dd1b0c54...`を更新していない。

視認性の指摘に対し、requestedの青い選択表示は維持し、操作focusだけ明るい3px外枠へ分けるCSS最小差分を作成した。
JS/設定値/利用不能gate/POST動作は変更せず、controls/render Node回帰とdiff検査は成功した。
CSS SHA-256は`3d4187843746ee739156c5a79c9af23f5e12c2e166fb75e79bbdd2237e5c6d64`。
この時点では未配備で、候補Release packageの配備とは別のUI-only反映として扱う。

### 別承認されたfocus CSS-only反映

利用者はCSSだけの反映を承認した。17:51:11 UTCの停止assertが失敗したためbackup/転送/restart前で中断し、
17:51:25 UTCのread-only再確認でPLAYINGを観測した。こちらからSTOP/cancelは送らず、
利用者が通常操作で手動STOPしてから続行を指示した。
17:52:53 UTCにSTOPPED・capture IDLE/job0/busy=false/reader_released=true/offset_unknownを確認した。
旧CSS `30a94864...`をupload内の`player.before-focus-20261024.css`へ非上書き保全し、hash一致後にCSSだけ転送した。
7 assetのhash/通常file/0644・余分なentryなしを再確認した。

17:53:13/17:53:23 UTCのmedia_busy観測ではrestartを実行しなかった。
その後、media_idleは取得開始gateであり、UI-only通常restartの必須条件とはしていないことを説明し、
17:54:49 UTCのSTOPPED・capture IDLE/job0/解放済みを確認して承認済みnormal daemon restartを1回実行した。
取得用gateや製品codeを緩和したのではなく、取得POSTは0回である。強制kill/電源断/繰返しrestartはしていない。
HTTP HTML/CSS/JS一致後にkioskを1回restartし、JS `48749538...`と他5 assetは維持した。

17:55:51 UTCから実WPEの合成focus操作で、requested REPEATは青rgb(130,184,239)の文字/境界線、
別の操作対象Singleは明るいrgb(237,242,242)・solid 3px外枠/offset3pxとなることを確認した。
初回の診断assertは非focus側のoutline-widthを0と仮定して失敗したが、WebKitはoutline-style:noneでも
computed width3pxを返していた。UIを再変更せず、枠の有無をoutline-styleで判定して再確認に成功した。
selected=true/focused=falseとselected=false/focused=trueを区別し、scrollTop0・GET3件/POST0件だった。
合成操作後はSettingsを閉じ、Network domain無効化と一時SSH forward停止を行った。
利用者は「色自体の良し悪しは残課題だが、今は区別できるのでこのまま進める」と回答した。
物理視認性の現時点の肯定と、最終配色の残課題を分けて保持する。

18:00:49 UTCの終了GETはSTOPPED/AUDIO_READY・offset null/NONE・REPEAT要求/実効/pending=false・背景ON/saved、
capture IDLE/job0/busy=false/reader_released=true/call_inflight=false/waiting_for_access=false・accepted frames/bytes0・media_busy。
daemon PID38060（17:54:49 UTC起動）・kiosk PID38105（17:55:00 UTC起動）はactive/running・NRestarts0。
HTTP JS `48749538...`/CSS `3d418784...`を確認した。
配備packageはDebug `dd1b0c54...`、install先daemon `1137505a...`のまま、
元引数・custom-ui一組・他設定行不変・offset指定0組もprivate比較で確認した。
候補ReleaseのPi配備・診断権限変更・offset0追加・取得/取消は未承認/未実施である。

## PR #256 見出し統一・二軸反映と初期scroll不具合（2026-10-24）

利用者がRead policyの見出しを他項目へ揃え、残りの確認も進めるよう指示した。
Read policyだけの25-36px指定を除き、他2項目と同じ16pxのCSS規則へ統合した。
HTMLのh2/h3階層・dialog labelは変えていない。
controls/render Node回帰とJS構文・diff検査を先に成功させ、二軸JSとCSSの2 assetだけを反映した。
source基準は`ec9fe964a9dcc31ac8cd26e1ced593c3d92f77e8`に下記の未commit差分を加えたもの。

14:57:38 UTCはSTOPPED/AUDIO_READY・offset null・SINGLE要求/実効/pending=false・背景OFF/saved、
capture IDLE/job0/busy=false/reader_released=true/accepted_frames=0・offset_unknownだった。
直前JS `88e89c4d...`を元のupload内の`player.before-2axis-20261024-1457.js`へ非上書き保全してhash一致を確認した。
元のCSS `41e1a5e0...`と初期uploadの7 asset・設定保全fileも保持している。
配置先7 assetの通常file・0644・hash、余分なentryなし、custom-ui一組・既存引数保持・その他設定行不変を確認した。
14:59:33 UTCのSTOPPED・capture解放済み・offset_unknown確認後、daemonを1回、
HTTP HTML/CSS/JSの配置先とのbyte一致確認後にkioskを1回再起動した。
反映したJSは`98c343f8bd61f4d9281cf613a3e452f9ab3f71b6590746031ca5d1ff4f10ff76`、
CSSは`30a94864f9ea34f287ae2d0af179af0b94f8804b2ca98fb09e77f7f330678dac`。
他5 assetは下記Gate A表から不変で、package/binaryの更新はしていない。

既存Inspectorへの一時forwardで実WPEの短い合成UI操作を行った。
Settingsを開き、computed styleで3見出しとも16px/weight700/system-ui,sans-serifを確認した。
can_start=false/busy=false/job0・両取得button aria-disabled=trueを確認してから、合成keydownの
下/右/下/左がbackground-off/background-on/pcm-cancel/pcm-startへ移ることを確認した。
利用不能なAcquire/Cancelを各1回合成clickし、offset_unknown/No active acquisitionの理由と
「No request sent」を確認した。実request観測はread-policy/artist-background/complete-pcmのGET各1件、
POST0件で、観測経路がGETを受信した上での限定無POST正例である。利用不能gateは緩めていない。
feedback下端とsurface下端の余白は約38.8pxだった。操作後はBackでPlayerへ戻した。
これは実WPEの合成操作であり、物理リモコンの操作や音声の成功とは区別する。
request本文・key・画像は収集せず、Inspector設定やUI previewも変更していない。

物理確認を案内したところ、利用者は「settingsを開いたときのスクロール位置がおかしい」と報告した。
見出し/二軸/余白について独立した物理成功申告は得ておらず、Gate Bを未合格のまま保持する。
追加restartはせず、read-only DOMを確認した時点ではSettingsは閉じていた。
続く限定合成再現で、初回openのscrollTop125・policy-single focus・見出し上端-37.16px/surface上端20px、
PCM focus後126、閉じて再open後125を観測した。初期buttonのnearest scrollだけでは先頭へ戻らず、
前回位置が残ることで見出しが隠れる不具合を実WPEでも再現した。再現後はSettingsを閉じた。

一時forwardは両回とも停止し、追加配備/restartをせずに実機runを終了した。
返答待ちと限定再現を含め約10分枠を超え、終了GETは15:09:20 UTCとなった。
終了状態はSTOPPED/AUDIO_READY・offset null・SINGLE要求/実効/pending=false・背景OFF/saved、
capture IDLE/job0/busy=false/reader_released=true/accepted_frames=0・offset_unknown。
daemon PID35939（14:59:34 UTC起動）・実cdplayerd、kiosk PID35981（14:59:44 UTC起動）・実WPEで、
両service active/running・NRestarts=0、live custom-ui一組・offset指定0組を確認した。
package `dd1b0c546dadf17a48576739559e1be3829a8635f354a9c2abcc0b90a20678b0`、
install先daemon `1137505a78862ad9c5de1be71cc0a99d1400e3bcebeb50efb579eae61a975f91`は不変。
反映済み二軸JSと統一見出しCSSを維持し、復元はしていない。
offset変更・PCM取得/取消・直接drive probe・provider操作・C++ build・全CTestは実施せず、Gate Cは別承認のまま。

### 初期scrollのローカル修正

`openSettings`で初期focusを置いた後にsurfaceのscrollTopを0へ戻す最小修正を加えた。
controls試験で初回/再表示時の残留125/200が0になることと、非同期初期GET後も先頭を維持することを確認した。
初回のrender試験は新たなquerySelectorにfixtureが未対応でTypeErrorとなったため、
既存2箇所のDOM mockへ必要なselectorだけを追加した。製品側にtest用fallbackは追加していない。
最終controls/render Node回帰、player/controls/renderの3 JS構文検査は成功した。
resetだけをmemory上で除いた負例はcontrols182行で125 != 0のassert失敗となり、回帰を検出した。
機能設計と全diffを点検し、`git diff --check`を確認した。
修正版JSは`92b46dee4965a368fa03d5203e8233934e6332bd4b3130dbd2b30570a75c618b`で、未commit・未配備。
PiのJSは`98c343f8...`、CSSは`30a94864...`のままであり、scroll修正の実機正例は未確認である。

## PR #256 修正版UI反映と二軸navigationの指摘（2026-10-24）

利用者の継続指示により、約10分・反映一組のGate Bを再開した。
sourceは`ec9fe964a9dcc31ac8cd26e1ced593c3d92f77e8`に下記の未commit巡回順修正を加えたもの。
14:24:52 UTCの読取でSTOPPED/AUDIO_READY・SINGLE要求/実効/pending=false・背景OFF/saved、
capture IDLE/job0/busy=false/reader_released=true/accepted_frames=0・offset nullを確認した。
start_rejectionはこの時点ではmedia_busyで、続く観測ではoffset_unknownだった。
custom-ui一組・既存引数保持・その他設定行不変、旧JS復元元の通常file/hashも再確認した。

変更したのは配置先`/srv/picdplayer-ui-dev-20261020-aria/player.js`だけで、元のuploadは保持した。
最初のrsyncはMacの2.6.9が`--chmod=F644`を拒否して転送前に終了した。
同optionを外し、既存fileの権限を維持する転送で成功した。7 assetのhash・通常file・0644・
uid/gid1000/1000と、余分なentryなしを確認した。JS以外の6 assetは下記Gate A表から不変。
再起動直前のmedia_busy観測ではcommandを実行せず止め、次の観測でもbusyなら中断する旨を伝えた。
14:26:30 UTCのSTOPPED・capture解放済み・offset_unknown観測後にdaemonを1回再起動した。
HTTP HTML/CSS/JSの配置先とのbyte一致を確認してから、kioskも1回再起動した。
修正版HTTP JS SHA-256は`88e89c4d06baf6838006640e81d665863bb0e031837b74c33c58f5b73295d6fd`。
14:26:59 UTCにdaemon PID35479・kiosk PID35524、実process cdplayerd/picdplayer-wpe、
両service active/running・NRestarts=0、live custom-ui一組・offset指定0組を確認した。
package/binaryをPR headのbuildへ置換したという意味ではない。

既存Inspectorへの一時forwardで、90秒限定のNetwork request method観測を行った。
観測は90.124秒で正常終了したが、初期/終了DOMともSettingsは閉じており、API requestは0件だった。
利用者の物理操作と観測窓の重なりを確認できず、0件を無POST成功には扱わない。
request本文・key・画像は収集せず、screenshotやUI preview変更も行っていない。

利用者は「上下キーでは各項目の移動、左右キーではボタン間の移動が自然」「上下キーで現在は
ボタン間も移動」「Local PCMでは上下キーと左右キーで動作が異なる」と報告した。
現行sourceは上下左右を同じ一列ringとして扱っている。順序修正だけでは要求を満たさず、
二軸navigationの仕様指摘としてGate Bを未合格とする。Local PCMの差異の原因は未確定である。
余白・理由表示・Back復帰・実機無POSTについて、個別の成功申告は得ていない。
二軸仕様の端・列保持・Back到達を合意する前に、追加修正や再反映を行っていない。

利用者の返答待ちを含み約10分枠を超え、終了read-only snapshotは14:42:16 UTCとなった。
90秒observerは自動終了しており、返答待ち中の追加反映や取得操作は行っていない。
終了時はSTOPPED/AUDIO_READY・offset null・SINGLE要求/実効/pending=false・背景OFF/saved、
capture IDLE/job0/busy=false/reader_released=true/accepted_frames=0・media_busy、
両service active/running・同PID・NRestarts=0だった。
package `dd1b0c546dadf17a48576739559e1be3829a8635f354a9c2abcc0b90a20678b0`と
install先daemon `1137505a78862ad9c5de1be71cc0a99d1400e3bcebeb50efb579eae61a975f91`は不変。
一時SSH forwardも停止した。修正版UIの反映を維持し、旧JSへの復元は実施していない。
offset変更・PCM取得/取消・直接drive probe・provider操作・C++ build・全CTest・package配備は未実施。
Gate Cは引き続き別承認であり、取得/取消/実release/再生復帰の正例は増えていない。

### 合意後の二軸navigationローカル修正

利用者は、上下の行移動時は左右の列を維持する案を採用した。
上下はBack/Read Policy/Artist Background/Local PCMの行間、左右は行内button間、端では停止し、
Backから下は要求policyのbuttonへ戻る。Tabは7 buttonの順逆巡回、物理戻るはPlayer復帰を維持する。
Local PCMでの相違は「focusの移動先が違う」と追加回答されたが、実機の原因は未確定のまま扱う。
この回答では追加実機反映を行わない旨を明示し、以後はローカル修正/検証だけを行った。

`handleNavigation`のSettings方向入力だけを二軸へ変更し、選択/POST・取得条件・表示/scroll処理は変更しない。
既存controls試験を行/列の意味に合わせ、semantic/keyboardで同じ代表経路、列保持、端での停止、
利用不能PCMへの到達、Backから要求policyへの復帰、focusの一意性と移動時無POSTを確認した。
Tab正逆巡回、背景/policyの決定先、取得/取消・無POST・遅いGET等の既存回帰も保持した。

- `node tests/ui_controls_test.js`: 成功。
- `node tests/ui_render_test.js`: 成功。
- `node --check ui/default/player.js`、`node --check tests/ui_controls_test.js`: 成功。
- 方向入力だけを直前の単一ringへmemory上で戻した負例は329行のassertで失敗し、旧操作を拒否した。
  fileの書き戻しやPi操作はしていない。

二軸修正版JS SHA-256は`98c343f8bd61f4d9281cf613a3e452f9ab3f71b6590746031ca5d1ff4f10ff76`。
未commit・未配備であり、今回Piへ反映した単一ringの`88e89c4d...`と区別する。
機能設計を合意した操作へ更新し、全diffと`git diff --check`を確認した。
ローカルのsemantic/keyboard成功を、TV/物理リモコンの二軸動作や実機無POSTの成功へ読み替えない。

## PR #256 Gate B巡回順の修正・再確認（2026-10-24）

下記Gate Bの不具合と最小修正案を提示後、利用者が継続を指示した。
対象をSettingsのpolicy2個 → background2個 → PCM2個 → Backの巡回順と対応回帰試験に限定し、
UI-only再反映一組で確認する。取得条件・payload・C++ binary・offsetは変更しない。
反映前のJS `2b16d64635daf6046ce2b5a130e5810abf22b035bc7d45c9589354f5448ebd53`は
既存配置元`~/picdplayer-ui-upload-20261020-aria/player.js`に通常fileとして残り、復元元のhashを確認した。
この配置元は今回上書きしない。

既存Debug WPEのloopback Inspectorが有効なため、一時SSH forwardで接続した。
有効化/設定変更/追加restartはしていない。最初の`/json`照会はHTTPErrorとなったが、これは
Chromium型endpointの誤りであり、既存toolどおりroot `/`でWebPage targetを確認できた。
既存`inspect-wpe.py`で1920x1080・ready complete・connection connectedを確認した。
続くread-only DOMではSettingsが閉じていることを確認し、Network domainのenable/disableが成功した。
さらにpageから明示read-only GETを1回送る観測器smokeで、complete-pcmのrequest methodがGETとして
観測できることを確認した。取得POSTは送っていない。request本文・key・画像は収集しない。
修正版の反映・物理操作結果は以下へ別途記録する。

約10分枠の終了が近づいたため、未検証の修正版を実機へ反映せず、このrunの実機操作を終了した。
今回のasset転送・設定変更・restartは0回であり、14:15:36 UTCのHTTP JSは引き続き
`2b16d64635daf6046ce2b5a130e5810abf22b035bc7d45c9589354f5448ebd53`だった。
同時点でSTOPPED/AUDIO_READY・offset null・SINGLE要求/実効/pending=false・背景OFF/saved、
capture IDLE/job0/busy=false/reader_released=true/accepted_frames=0、両service active/running・
PID34661/34714・NRestarts=0を確認した。ただしstart_rejectionは`media_busy`であり、
14:10:07 UTCの`offset_unknown`と区別する。media idleが継続したとは扱わない。
一時Inspector SSH forwardは停止した。有効化設定やUI previewは変更していないため、その復元対象はない。
修正版のHTTP/DOM/物理操作・余白/理由表示・実機無POSTは未確認、Gate Cは未承認/未実施のままである。

### ローカル修正の最終点検

担当処理は14:17 UTCに、JSの巡回順/関連indexと試験側のID列だけを変更し、試験未実行の状態で返却した。
実機枠は延長せず、親がローカル差分と既存Node回帰だけを追加確認した。
最初の`node tests/ui_controls_test.js`は旧順序を期待する329行でassert失敗した。
既存のBack到達とTab期待を訂正し、同じ試験内で方向キー/Tabの正逆一巡・focusの一意性・
移動だけではPOSTしないことを確認した。既存の利用不能なAcquire/Cancel決定時の無POST、
policy/backgroundの決定先・遅いGET・取消/release待ち等の試験は保持した。
これは既存UI試験の更新であり、新たな物理CEC E2E基盤や全状態matrixの追加ではない。

- `node tests/ui_controls_test.js`: 最終成功。
- `node tests/ui_render_test.js`: 最終成功。同値snapshot・metadata/artwork/background等の既存回帰を含む。
- `node --check ui/default/player.js`、`node --check tests/ui_controls_test.js`: 成功。
- 更新試験へGit HEADの旧`player.js`をmemory上だけで差し替えた負例は330行のassertで失敗し、
  旧巡回順を拒否することを確認した。worktree fileは負例のために書き戻していない。

修正はSingle/Repeat → Off/On → Acquire/Cancel → Backの巡回順と、それに依存するBack focus・
semantic selectの振分け・background clickのfocus位置だけである。初期のrequested policy focus、
利用不能buttonのfocus可能性、payload/取得条件は変更しない。
修正版JS SHA-256は`88e89c4d06baf6838006640e81d665863bb0e031837b74c33c58f5b73295d6fd`。
これは`ec9fe964...`からの未commit差分であり、PR headや実機HTTP hashとは区別する。
ローカル修正/試験は実機枠を超えて終了したが、追加Pi接続・反映・restartは再開していない。
機能設計に巡回順を追記し、最終全diffと`git diff --check`を確認した。

## PR #256 UI-only反映 Gate B（2026-10-24）

Gate A結果と約10分・反映一組の操作/終了/復元条件を提示後、利用者が「続けてください」と指示した。
14:00:02 UTCにSTOPPED/AUDIO_READY・SINGLE要求/実効・pending=false・背景OFF/saved、
capture IDLE/job0/busy=false/reader_released=true/accepted_frames=0・offset null/offset_unknownを再確認。
7 assetの配置元/配置先hash・0644権限と、custom-ui一組・既存引数保持・その他設定行不変も再照合した。
source基準は`ec9fe964a9dcc31ac8cd26e1ced593c3d92f77e8`、UI 7 hashは下記Gate A表と同じ。

承認済みの`systemctl restart picdplayer.service`、続いて`picdplayer-kiosk.service`を各1回実行した。
daemonは14:00:19 UTC起動/PID34661、kioskは14:00:35 UTC起動/PID34714。
14:00:29 UTCにHTTP `/player`・`/player.css`・`/player.js`が配置先/sourceの各hashとbyte一致した。
14:00:48 UTCにlive daemonのcustom-ui一組と期待path、kioskの実process `picdplayer-wpe`、
両service active/running・NRestarts=0を確認した。実行binaryの最新PR buildへの置換はしていない。

TV/物理リモコンではSettings内の7 button一巡、利用不能な取得/取消での理由確認、Back復帰を案内した。
policy/背景buttonの決定や取得可能時の取得は案内していない。
利用者は「一巡できますが順番がおかしいです。Read policy -> Back -> Artist background のように移動します」と報告した。
全項目への到達は利用者観測として記録するが、巡回順は不具合としてGate Bを合格にしない。
source `ui/default/player.js` の`settingsActions`はpolicy2個・Back・background2個・PCM2個の固定順であり、
報告と一致する。既存`tests/ui_controls_test.js`もその順序を期待していた。
修正候補はpolicy/background/PCMを連続させてからBackへ巡回する最小変更だが、このrunで実装/再反映はしていない。
余白・利用不能理由・無POST・Player復帰について独立した利用者確認は得られておらず、未確認のまま残す。
Inspector/DOM/screenshot/HTTP request traceは取得していない。Node等の既存自動試験も今回再実行していない。

14:04:13 UTCの終了GETではSTOPPED/AUDIO_READY・offset null、SINGLE要求/実効・pending=false、
背景OFF/saved、capture IDLE/job0/busy=false/reader_released=true/accepted_frames=0・offset_unknown。
両serviceは同PID・active/running・NRestarts=0を維持していた。
package SHA-256 `dd1b0c546dadf17a48576739559e1be3829a8635f354a9c2abcc0b90a20678b0`、
install先daemon `1137505a78862ad9c5de1be71cc0a99d1400e3bcebeb50efb579eae61a975f91`は不変。
操作後job0の観測は、物理決定時にPOSTが送られなかったことの証明にはしない。

巡回順の不具合を残して今回の一組は終了した。custom-uiは承認どおり反映状態を維持し、設定復元や
追加restartは行っていない。Settingsが開いていれば物理BackでPlayerへ戻るよう案内したが、復帰完了は未確認。
read offset変更・PCM取得/取消・直接drive probe・provider通信の操作は追加せず、Gate Cは未承認/未実施のまま。
C++ build・全CTest・package再配備・commit/push・Issue closeも行っていない。

## PR #256実機再開 Gate A（2026-10-24）

live PRはOPEN/Draft、head `ec9fe964a9dcc31ac8cd26e1ced593c3d92f77e8`。
保存worktree `issue238-local-pcm-sha256`は同head・同branchで、観測開始時はstaged/unstaged/untrackedなし。
fetch後の`refs/remotes/origin/master`は`597cbca191f079af6714bb4836ae15d1fe9ff78f`で、
取込済み監査修正`05a1a3e`以降は文書6ファイルだけだった。master取込・build・deployは行っていない。
CI run39688998298のhead一致・SUCCESSと各step成功を再取得した。各72/72の件数はPR記録を参照し、
今回CI生ログの再検算はしていない。#238/#239はOPENを維持する。

利用者はPi/利用者とも利用可能・操作中でなく、前回から環境変更なしと回答した。
既存の専用ED25519 pinを使い、strict照合・host-key更新無効・既存共有connection非使用で接続した。
通常SSH設定の照合無効値は使用せず、設定自体も変更していない。接続先やkey/設定全文は記録しない。
13:50:12 UTC以降の短いread-only観測で次を確認した。

- daemon/kioskともactive/running、NRestarts=0。PIDは16497/16523、開始時刻は2026-10-20 15:53:49 UTC。
- packageは`picdplayer 0.1.0 arm64 install ok installed`。配置済みpackage SHA-256は
  `dd1b0c546dadf17a48576739559e1be3829a8635f354a9c2abcc0b90a20678b0`、
  install先daemonは`1137505a78862ad9c5de1be71cc0a99d1400e3bcebeb50efb579eae61a975f91`。
  15:54 UTCの旧配備記録と一致する。package markerとinstall先binaryの`--build-info`はDebug。
  `/proc/16497/exe`のhash取得はPermissionErrorで未確認。権限を拡張せず、実行中binaryのbyte一致は断定しない。
  live cmdlineからdaemonとWPEのprocess名は確認できた。最新PR headのbinary配備とは扱わない。
- 開始状態はSTOPPED/AUDIO_READY、SINGLE要求/実効・pending=false・保存値restored。
  背景は要求/実効OFF・saved。captureはIDLE/job_id=0/busy=false/reader_released=true、
  waiting_for_access=false/call_inflight=false、accepted frames/bytes=0。
  offsetはnull/source NONE、can_start=false/start_rejection=offset_unknown。
- 配置元`~/picdplayer-ui-upload-20261020-aria`と配置先`/srv/picdplayer-ui-dev-20261020-aria`の
  7 assetは全てregular file、uid/gid=1000/1000、mode0644。両者とPR sourceのhashが全件一致した。
  配置先の余分なentryはなく、rootは通常directory・owner/group udonchan・0755、親`/srv`はroot/root・0755。
- 保全設定`picdplayer.default.before-custom-ui`と現在設定をprivateに比較し、custom-ui一組だけの追加、
  指定path一致、元の引数列保持、その他の設定行不変を確認した。全文や秘密を含み得るargv/Environmentは出力していない。
- live daemonのcustom-ui指定は0組、read-offset指定も0組。HTTPのJS/CSSは旧配備hashのままで、
  配置済み最新UIは未反映だった。過去の「未再起動」申告だけでなく、現在のprocess/HTTPで照合した。

| UI asset | PR source / 配置元 / 配置先で一致したSHA-256 |
| --- | --- |
| manifest.json | `daf28cd2c46d23881344c213de258faec9a5241875602bc382771996fde26148` |
| player.html | `22999923d7b0b6c454bfbab352a34cab47785a8101804648efe4cecc88899aad` |
| player.css | `41e1a5e01199c84c43e5f264566c54f15179f656cd41b0fd75d114897aba0d74` |
| player.js | `2b16d64635daf6046ce2b5a130e5810abf22b035bc7d45c9589354f5448ebd53` |
| status.html | `32bec897f49bdba1e70050a460ad8b3e4aa3953f4152206551b3487b6c6d9049` |
| status.css | `5510642566bdcedb1b11236521bb49d732e48c09fe20d033af8b1e41b1ca5ca9` |
| status.js | `f8b4e2a61c2b4d480b6c029824cb96c74757565253fbfdb9d78ceede3a807d92` |

観測時HTTPのHTMLは`ecf6ed8a8eb9ae3db154d463879ac0dd6e0cef433004168cb2cd093b99dde989`、
JSは`d56867d8430229af2ee80d541576048bf93e7c9ba353749d68f50f37a1889e01`、
CSSは`8776b12ebb9dc12c3cfc5165d1efe8d37d84ace582a9caef7aa6369ddee4b2c8`。

操作列はservice/package照会、状態API GET、asset/hash/権限照会、設定のprivate比較、live引数の必要field確認だけ。
STOP/cancel・設定変更・restart・deploy・offset追加・直接drive probe/ioctl・Inspector接続は実行していない。
終了時も状態変更操作はなく、復元対象は作成していない。13:52:48 UTCの終了GETでは
STOPPED/AUDIO_READY・offset null・SINGLE/pending=false・背景OFF/saved、capture IDLE/job0・
busy=false/reader_released=true/accepted_frames=0と、両service active/running・同PID・NRestarts=0を確認した。
ただし終了時のstart_rejectionは`media_busy`だった。開始時の`offset_unknown`とは時点を分けて保持し、
media idleが継続した、あるいは全開始gateを満たすとは扱わない。直接probeや状態変更で解消させていない。
TV目視・物理リモコン・音声・最新7-button UIの実画面・無POSTの実機正例は未確認である。
Gate Bは約10分・反映一組の操作/終了/復元条件を別承認してから進める。
Gate Cの明示offset0・取得/取消/実release/手動再生復帰は別承認・未実施のままとする。
結果の意味はlocal_pcm_fingerprint・外部照合NOT_CHECKEDであり、外部MATCH等の根拠は増えていない。

## PR #260監査修正のDraftへの取込（2026-10-22）

利用者指示によりmaster `05a1a3e`をPR #256のbranchへ取り込む。設定FIFOのnonblocking拒否と
回帰試験・CTest timeout、共同保存/WPE依存・完了Issueの文書訂正を保持する。
競合はbacklogと本検証記録だけで、C/D実装・過去の証拠・下記実機再開点を保持して解消した。
master側の「C/D未実装」は監査基準masterの状態であり、このDraftの実装状態を戻す意味ではない。
同名local branch `origin/master`が旧commitを指しているため、remote refを完全名で指定した。
Draft状態は維持し、本PRのmaster統合・#238/#239のclose・実機接続/配備は行わない。
今回のbuild/CI結果は対応PRへ記録し、過去の72/72やPR #260の結果で代用しない。
取込後のsourceをnetworkなしLinux/aarch64でfresh Debug buildし、API/metadata ON・WPE OFF・
daemon/kiosk unit ONの全CTest 71/71成功を確認した。FIFO保存fallbackとC/D/Node回帰を含む。
これは実機取得/取消/表示やWPE launcherの今回のlocal検証ではない。

## Draft保存と実機再開点（2026-10-21）

利用者は旅行先へ開発machineを持参しておりworktreeへアクセス可能と確認し、commit/pushとDraft PR作成を依頼した。
この単位はソース保全・review・CIだけで、Pi接続・追加deploy・restart・merge・Issue closeは行わない。
実機とlocal agentが使えないという先の制約を、開発machine上のworktree自体が存在しない意味に解釈しない。

実装はCのlocal SHA-256とDの実session/API/Settings接続、最新の7-button一貫navigationを含む。
Settingsは利用不能でもbuttonへfocusでき、aria-disabledで操作不能を示す。説明文自体をcursor対象にせず、
決定時は理由表示だけでPOSTしない。最新UI差分は作成時のNode controls/renderで成功を再確認した。
過去の標準72/72等は下段の日付・候補の結果であり、最新UIの実WPE確認や、このDraftの新CIと混同しない。
実際のcommit・CI・Draft状態はGit/対応PRを正本とする。#238/#239には自動close指定を付けない。

### 最後の利用者申告と再開順

2026-10-20の最後の申告は、7 UI fileの配置と`--custom-ui /srv/picdplayer-ui-dev-20261020-aria`を
既存EXTRA_ARGSへ追加して保存済み、まだ再起動していない、という状態である。
配置元は`~/picdplayer-ui-upload-20261020-aria`、設定保全先はその中の`picdplayer.default.before-custom-ui`。
配置先のhash/権限、設定の差分、live processへの反映は申告後に照合していない。現在の実機状態は推測しない。
read offset0の追加は指示・実施しておらず、取得・取消・再生復帰の正例も未実施である。

1. 実機へ再接続できたら、まず読取だけでservice・再生・capture busyと、配置file/hash/権限を確認する。
2. 既存EXTRA_ARGSが保持されcustom-ui一組だけ増えたことをprivateに照合し、合意した範囲でdaemon/kioskを再起動する。
3. 最新UIのHTTP hash、7 buttonの一貫focus、利用不能時の理由表示と無POSTを確認する。配置検証前にrestartしない。
4. 取得を試す場合は、明示0が校正済みを意味しないことを合意し、設定変更/復元と短い有限budgetを別途承認する。
5. 最初は短い取得/取消/release待ち/再操作/再生復帰を確認し、長い全域完了・TV/音声は時間と復元を区切る。

これは既存残件の再開順であり、全driveや新しいmatrixを完了条件へ追加するものではない。
source/文書だけをDraftへ保全し、private screenshot・archive・設定・key・package・SDKは含めない。

## #239 明示local PCM jobの本番接続（2026-10-19〜20）

利用制限による中断後に利用者が再開を指示し、採用済みCに続いてDのlocal実装を進めた。
既存`feature/238-local-pcm-sha256`上のC差分・SDK・artifactは保全し、別機能のworktreeやPiには触れていない。
API有効時はA/B/Cと`CompletePcmSession`をcdplayerdへcompile/linkし、実raw factory/TOC probeと共有coordinatorを渡す。
APIのみ有効・metadata非compileでも成立させ、provider/network/PCM保存・自動取得は追加しない。

session ownerはcontrollerのSTOPPED、engineの成功した出力reset/cancel・保持PCM消去、
playback readerの実release、media idle、device/disc/policy/offsetを観測する。
正常STOP/自然終了のreaderは非同期退役し、PAUSEDや単なるqueue空を開始資格にしない。
HTTP/CEC/stdinの競合操作、policy/device/disc変更でcapture/resultを失効させ、release待ちの操作は
未実行として手動再操作を返す。reader incarnationとdevice観測generationは別に保持する。
probe/speed/spindown/ejectのdriveアクセスにもcaptureを含め、main-loop ownerのpoll/confirmでのみ確定する。

read-only状態/結果API、有限start/cancel、typed拒否理由と既存generic409互換を追加した。
Settingsは明示30分取得・取消と状態/解放待ち/結果位置を表示し、1秒pollはread-onlyかつpanel内だけ。
旧応答/旧dialog・POST重複を抑止し、閉じただけでは新たな開始/取消を送らない。
public manifestは一件・64 KiB以内、現在の全gate/identity一致時だけ返し、送信は既存4 KiB分割/同時8上限を使う。

既存API/engineの個別試験と本番sessionのfake取得・manifest・取消/release待ち、
実daemonのNo Discでの非自動取得/開始拒否、metadata非compile、Node操作回帰を実行した。
途中のUI試験は空表示を空文字と期待して失敗し、既存`set()`のplaceholder（em dash）を期待する形へ修正した。
初回multi-target buildはCMake再生成直後に新targetが未認識で停止したが、daemon自体のcompileは成功しており、
再生成後のbuildで新targetを含め成功した。これらを試験成功へ読み替えず記録する。

独立read-only backend reviewでP1/P2 findingはなく、後続の契約照合で古いA/Bのtest-only文言が残る指摘を受け、
当時の段階と現在のproduction linkageを分けて修正した。reviewは実機検証や試験再実行ではない。
最終点検では通常再生のregionがcaptureの75-frame/予約量制約を越す場合に開始可能と見える差を修正し、
coreと状態表示で同じpure preflightを使い、consumer/factory/driveを呼ばず`policy_budget_unsupported`を返す。
これは予約式を別layerへ複製したものではない。未知offset/非0の入力資格も変更しない。

2026-10-20の再開後、最終sourceで次を確認した。新しいcorner caseや自動E2E基盤を必須化せず、
既存のcore/worker/engine/HTTP/Node試験と、狭いowner統合試験を使った。

| 検証 | 結果・境界 |
| --- | --- |
| 標準WPE Debug build/package | `PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build PICDPLAYER_BUILD_TYPE=Debug ./scripts/build-container.sh`成功。SDK recipe一致、stage install、package/profileを確認 |
| 標準全CTest | Linux/aarch64、API/METADATA/WPE ON・PARANOIA OFF、network noneで72/72成功 |
| 最小全CTest | API/METADATA/WPE OFFで再buildし42/42成功。正常STOPのreader退役も含む回帰 |
| API ON/metadata非compile | daemonと対象をbuildしcore/consumer/worker/session/API/engine・実daemon/保存の対象8/8成功。enrichmentに依存しない |
| ASan/UBSan | consumer・production session/fake reader・engineの3/3成功。kernel/ALSA/OpenSSL全体をinstrumentした証明ではない |
| Node | controls/render成功。明示start/cancel payload、202後の状態確認、release待ち、拒否時非再試行、旧daemon/旧dialog応答とsemantic操作を確認 |
| 本番owner接続 | `complete_pcm_session_test`61 checks成功。fake readerによる全域取得→実close→confirm→manifest、観測世代変更での公開拒否、blocked read取消とrelease待ち、手動再開始、開始不適格時factory/probe未呼出しを確認 |
| 実daemon No Disc | 新GET/結果404、stale/不適格startと存在しないjob取消の拒否、job_id=0継続を実HTTPで確認。これは実driveでの正例ではない |
| HTTP transport | strict body/profile/予算、202/409/413、64 KiB結果の4 KiB分割、従来6 navigation actionを確認。network noneで外部peer probeはskip |
| Debian lifecycle | 標準scriptによるdisposable containerでの検査成功。Pi配備/systemd稼働確認ではない |

最終candidateは`package-container/picdplayer_0.1.0_Debug_aarch64.deb`、SHA-256
`47d1ce9ec226f05e5294ccba5d097fea8b2613e110fa6f1c19a22e7db1f09262`。
stage daemonは`9b3427f3997c945d05963a3e6088506cbb0b073672205b612fb358eeeab4a491`、
Player JSは`27911908a4fbe14a2d5c70531accd3e1ffc77600f520c29a3757f24742496a70`。
stageのJS/HTMLとsourceのbyte一致も確認した。これは以前Piへ配備した`70e7800d…`とは別で、local完了時点では未配備だった。
全diff/新規sourceと`git diff --check`を確認し、旧worktree・local master/同名origin/master・stashは変更しない。

このlocal完了単位では追加のPi/実disc/ALSA/CEC/TV検証、配備、commit/push/PRやIssue closeを行わず、
#238/#239をOpenのまま維持した。後述の配備は利用者の別承認による。
このlocal区間はC/Dをtest-onlyから実session/API/Settingsへ接続したが、実disc全域取得、実I/Oの取消/解放、
再生復帰と音声、TVでの新項目/物理リモコンは別承認の短い実機/探索的受入に残す。
外部照合・cache独立性・C2 trustを実装/保証したという意味ではない。

### 候補Pi配備と開始前状態（2026-10-20 15:35 UTC）

利用者は実機へアクセス可能となり、候補のdeployと操作案内を依頼した。開発機なので旧deploy packageの
追加backupは不要と明示したため、新たなpackage退避を行わず、既存private backupも削除しなかった。
照合済みhost pinでread-only確認し、STOPPED/AUDIO_READY・SINGLE・背景OFF・両service active、
旧版ではcomplete-pcm APIなし、read offset未指定であることを確認した。
設定は変更せず標準deploy scriptをblocking shellで実行し、両serviceはactiveを維持した。

15:35:26 UTCに実機package/daemon/HTTP Player JSが上記candidateの各hashと完全一致した。
daemon PID15672、WPE PID15701、両service active/running・自動restart0、STOPPED/AUDIO_READY、
SINGLE要求/実効・pendingなし、背景OFF/保存済み・source restoredを確認した。
新APIはIDLE・job_id0・busy=false・reader_released=true・accepted frames/bytes0であり、
自動取得を開始していない。can_start=false/start_rejection=offset_unknownは未指定設定に対応する。
設定保存先は維持され、read offsetを勝手に0へ補完していない。
ここでは新jobの取得/取消/再生復帰やTV/リモコン確認を合格にしていない。開始には別途、初期profileで扱う
明示0の試験上の意味と必要な設定変更/再起動・操作予算を利用者と確認する。

続く利用者のTV観測で、Read Policy/Artist Backgroundは見えるがLocal PCM説明の途中から画面外へ
はみ出すと報告された。両取得buttonが無効なためsemantic focusで飛ばされ、末尾の状態説明へ到達できない
経路を確認した。取得条件を緩めず、状態paragraphをtabindex=0のread-only focus点として追加し、
既存scrollIntoViewと同じoutlineを使った。決定操作はno-opとし、新たなPOSTや取消を生成しない。
Node試験で未知offset/両button無効でも到達・scrollでき、command数が変わらないことを確認した。
mockのparagraphをbutton扱いしていたdisabled属性と、Tab巡回の期待を実DOM/新focus点へ合わせた。
Node controls/render、標準build/package/profileと全CTest72/72が成功した。

利用者からUI変更に毎回再buildが必要か指摘があり、実機の`--custom-ui`未指定を確認した。
標準UIはconfigure時にbinaryへ埋め込むため、現行配備経路ではbinary更新を伴うが、UI試行ごとの
全build/全CTestは過剰だった。外部`--custom-ui`ならC++再buildは不要で、現在は起動時snapshot読込なので
asset反映にはdaemon再起動と画面再読込が必要である。設定の切替はまだ行わず、今後の短い確認方法として提示した。

既に生成した修正版を追加buildせず標準deployで再配備した。旧packageの新規backupは作成していない。
15:54:37 UTC、daemon PID16497/WPE PID16523、両service active/running・自動restart0、
STOPPED/AUDIO_READY・capture IDLE/job0/busy=falseを確認した。read offsetは変更しておらず、
直後のstart_rejectionはmedia_busyだった。取得/取消/再生はまだ実施していない。
packageは`dd1b0c546dadf17a48576739559e1be3829a8635f354a9c2abcc0b90a20678b0`、
daemonは`1137505a78862ad9c5de1be71cc0a99d1400e3bcebeb50efb579eae61a975f91`、
JSは`d56867d8430229af2ee80d541576048bf93e7c9ba353749d68f50f37a1889e01`、
CSSは`8776b12ebb9dc12c3cfc5165d1efe8d37d84ace582a9caef7aa6369ddee4b2c8`。
実機HTTPのJS/CSS、package/daemonの一致とHTMLのread-only focus点を確認し、TVでの到達は利用者確認待ちとする。

利用者は続いて「スクロールして読めたが、変なところが選択され、下側に余白がない」と報告し、
screenshot確認を依頼した。既存`inspect-wpe.py`と一時loopback SSH forwardで現在の1920x1080を撮影した。
PNGはrepository外の`private-pcm-ui-20261020/`へ0600、directory0700で保存し公開しない。
実画像ではread-only状態行に入力欄状のoutlineが付き、surface bottom1060pxに対し状態行bottom1058.91pxだった。
DOM観測はsurface scrollTop117/clientHeight1038/scrollHeight1241・padding上下38pxで、scroll余地が残っていた。

再buildをせず一時UI previewを行い、状態行のoutlineを除き、説明blockのheadingへ強調を移し、
scroll paddingを38pxに合わせて空のfeedbackを隠した。style tagによる試行は効かず、CSPを変更せずに
CSSOM property/既存stylesheetの試行を使った。preview後はscrollTop155、状態行bottom1020.91px、
surface bottom1060px、約39pxの下余白を確認した。private screenshotも撮り直した。
このpreviewは現在pageの一時状態で、diskの配備物やdaemon設定の変更ではない。

利用者が席を外し、手操作確認なしで可能な作業を進めるよう指示したため、同じ修正をlocalのHTML/CSS/JSへ反映した。
native/pointer focusもread-only位置へ同期し、遅れたpolicy GETがfocusを奪わず、決定でPOSTを発生させない。
空feedbackの非表示、heading強調、38px insetに連動するscroll paddingと併せNode controls/renderを確認した。
独立read-onlyのUI reviewで具体的findingはなかった。mock DOMでaddEventListenerを省略するrender試験には、
既存のoptional binding形式を使って対応した。今回のUI差分の確認では全build/全CTestを再実行していない。

既存のbuild済みdaemonをLinux container内で`--custom-ui /src/ui/default`指定で起動し、最新sourceの
HTML/JS/CSSがHTTPへ配信され、capture IDLE/job0のままであることを確認した。
初回の補助呼出はaudio optionの誤記で起動しなかったが、既存試験と同じ`--audio-device null --no-cec`へ直して成功した。
再buildせず外部UIを試せることの確認であり、Piの`--custom-ui`は依然未設定、read offsetも未変更である。
再配備済みdd1b0c54…と、その後のlocal UI差分/一時previewを区別する。

UI読込対象2/2（ui_bundle/ui_startup）も既存binaryのまま成功した。最初はtest logを書けないread-only mountで
CTestが開始前に停止し、通常のuser/writable test-log条件へ直して2/2を実行した。C++再buildは行っていない。
最新UIのHTTP配信hashはHTML `c70f4e1a177596a29513aedc80d8d1de7462f4a3f93003b7057b9212794a4514`、
JS `34a44befac477ce72127f29dd4289b9c5c57c8d0c0e3f08e5775b4df7c806a9b`、
CSS `d7a39dc05581905bd204687c01721b1a23ce3b476b9a576523f9e770594dc585`でlocal sourceとも一致した。
UI-only archive（manifestとsourceのみ、PNG/設定/key/binaryなし）をrepository外へ準備し、
SHA-256 `4303238dd3efc0ca43c7befab8e229c87f6a3a079735bdc95024d5e7fc9c52d2`を記録した。Piにはまだ導入していない。

16:33:17 UTCのread-only再確認でSTOPPED/AUDIO_READY・SINGLE/pendingなし・背景OFF、
capture IDLE/job0/busy=false/reader_released=true、offset_unknownによる開始不可を確認した。
UI previewはfocus/見た目だけを変更し、producer開始・背景ON・再生や設定POSTは送っていない。
最新の見た目を利用者が確認したという意味ではなく、再開時の手操作確認と外部UI/offset設定の承認は保留する。
検査用のloopback Inspector転送は解除し、既存共有SSH masterを切断しないようclientだけ終了した。
localの19223 listenerが残っていないことも確認した。page上の一時previewはreloadまで残る。

## #238 Local SHA-256 consumer/profile（2026-10-19）

利用者はintegrity機能をモンキーテスト直前まで進める方針の下、外部照合ではなく、
明示全域取得とlocal SHA-256指紋を採用した。Cのconsumer/profileを先に実装・自動検証し、
続けてD/#239の開始・取消・状態・結果を本番へ接続する単位に分けた。
通常再生の必須機能と未採用の外部照合を混同せず、network・PCM保存・自動取得・実機読取は追加しない。
master `c61faca`から`feature/238-local-pcm-sha256`を分離し、既存worktree/dirty/SDK/artifactを保持した。

local差分は固定stateのdisc/track SHA-256 consumer、`local-pcm-sha256-v1` profile、
既存coreからのcanonical入力/二段階完了接続試験、CMakeのtest targetと文書である。
空入力、標準56-byte vector、百万a vector、4-byte分割の結果一致、最大99 track/入力/result上限、
terminal consumer拒否、signed stereo S16LEとTOC track境界、reader未解放時の結果非公開、取消/失効を
新試験の32 checksで確認した。algorithm vectorは完全取得receiptの成功ではない。
canonicalの期待hashは別のPython hashlib計算とも照合し、provider互換性・外部MATCHを主張しない。

consumerの所有stateは当該Linux/aarch64で21,408 bytes、resultは固定16 KiBである。
OpenSSL 3.5.7の固定SHA256_CTXを使い、opaque provider allocationの上限を推測せず、独自hashも実装しない。
low-level APIのdeprecated属性抑止は当該sourceだけとし、state/RSS/stack/library全体の保証を混同しない。
test buildでOpenSSL Cryptoを明示検出し、Linux直接buildのlibssl-dev前提も文書へ反映した。

| 検証 | 結果・境界 |
| --- | --- |
| 対象core/consumer/worker | Linux/aarch64、network noneで3/3成功 |
| 最小構成全回帰 | API/METADATA/WPE OFFのDebugをbuildし42/42成功。新consumer実行で32 checks/stateサイズも確認 |
| 標準WPE Debug | 標準build scriptでSDK cache一致・build/stage/package/profileを確認し成功。network noneの全CTest71/71成功 |
| ASan/UBSan | 新consumer/core接続試験をinstrumentして1/1成功。OpenSSL自体の再build・instrumentや全libraryの安全証明ではない |
| Debian lifecycle | 生成Debug packageをdisposable containerで検証して成功。Pi配備や実systemdの確認ではない |
| 独立review | 新consumer/header/test/CMakeのread-only passで具体的findingなし。review自体はbuild/test再実行ではない |

標準CTestの最初の呼出はworkdir誤記でprocess spawn前に停止し、正しいworkdirで71/71を実行した。
新targetにwarningはなく、既存ApiCommand/fixtureのmissing-field initializer warningは本単位で修正しない。
全diffと`git diff --check`も確認した。ここで生成したpackageは新しい取得機能の配備物ではない。

このC単位の時点では新test executableだけへconsumerをcompile/linkし、cdplayerd/HTTP/UIへは接続しなかった。
C部分だけを本番の全域取得完成やモンキーテスト準備完了とは扱わない。
Cの後に実装する対象は実STOPPED/ALSA停止、playback reader解放、capture中のdrive access、全command ingressの取消と
未実行理由、device観測世代、confirm前の有効条件、bounded manifestとした。後続Dでの接続・検証結果は上段に別記する。
今回Pi・実disc・外部providerにはアクセスせず、compile/test以外のruntime変更も行っていない。
source/文書は未commit・未push・未統合で、#238/#239はOpenのまま維持する。

## #257/#259 監査修正（2026-10-22）

`c61faca`基準のlocal branch `fix/audit-257-259`で、設定読込をno-follow・nonblockingのopenと
取得fdの`fstat`検査にした。writerのいないFIFOも待たずに拒否し、既存の起動値fallbackへ戻す。
2秒alarm付き子processでFIFO拒否、単体試験でdirectory/過大fileを追加し、既存のv1/v2・不正JSON・
symlink回帰を維持した。CTestの単体試験にも10秒timeoutを設定した。
実daemonのloopback API試験へFIFO/directory/symlink/過大fileからのfallbackを追加し、
破損内容・symlinkが自動上書きされないことも確認した。
新単体試験を修正前のHEAD sourceへlinkした負例はFIFO childがalarm終了し、親assert失敗
（exit 134）となった。修正版は成功した。旧`13485ee`向けにも同じ修正と回帰を適用した。

検証はnetworkなしの`picdplayer-profile-review-build`、Linux/aarch64、fresh Debug、
API/metadata ON、WPE OFF、daemon/kiosk unit ONでbuildし、全CTest 69/69成功。
WPE library/launcherは今回buildしておらず、実機・実drive・ALSA実出力・CEC・TV・配備は未検証。
元の設定保存/復元のhardware受入範囲や電源断耐久性を広げるものではない。

#257は機能設計の共同保存要約、build manualのWPE依存、backlogの#121統合済み・#177/#178完了を
修正した。#238/#239はmaster未実装とIssue報告のlocal候補を区別し、候補自体は検証・変更していない。
過去の未commit/未統合・未確認記録は当時の証拠として保持し、#184等を完了に変更しない。
全diff・リンク先と`git diff --check`を照合した。両修正の保存commitは`7a4ea22`で、
続くPR作成・CI・統合依頼の対象は#257/#259に限定する。統合・終了の現在状態と今回PRのCIは
対応PR/Issueを正本とし、上記local試験や過去のCIを今回のCI成功へ読み替えない。
旧候補固有の#258は`9ac4dcb`として別branchへ保存・push済みで、現行WPEへ持ち込まない。
PR作成前の再照合で#238/#239の候補がPR #256へ保存済みと確認し、backlogの保存状態を更新した。

## #219 v1移行完了とV2復旧への移管（2026-10-19）

PR #253の必須CI成功とmaster `75df663`への統合を確認した後、#219の残件を読み取り照合した。
WPE primary化・標準build/package/deploy、Inspector PNG、Debug/Release分離は既に統合済みであり、
当日のPiでも実WPE process・package/profile/hashを確認済みである。#226の待機予算・deploy期限も
Completed済みで、古い本文の「未修正」は当時の記録である。

残る独自作業は初回document取得失敗と表示後API/WS切断の区別、説明可能な失敗表示と有界なPlayer復帰、
異常終了・再起動時のDRM解放/排他、WPE child残留とcleanup、daemon/音声非阻害の受入である。
wrapperのTCP待受は`/player`取得成功の保証ではなく、WPE load失敗時は異常終了する。
restart制限・cgroup管理・WS再接続とfixtureの存在だけで、上記の実機受入を成功とは扱わない。

利用者が「残った作業はV2に移行」と明示したため、[#254](https://github.com/udonchan/picdplayer/issues/254)を
V2以降・非blockingの唯一の追加WPE復旧所有先として作成し、#219のv1移行範囲をCompletedで終了した。
#188から#219へ継承した未完了の失敗表示/復帰も#254へ移管する。旧本文・未完了checkbox・過去の
FAIL/未確認はhistoryとして保全し、全matrixをPASSへ変更した終了ではない。#254は現時点で着手しない。

この移管は#219固有の追加復旧だけを対象とし、通常cold boot/継続運転#184、物理リモコン#252、
写真・TV・試聴#51、正式runtime供給#220等のv1受入は元のIssueで維持する。cursor#28、表示#164、
保存#177、Settings#178のCompletedと、#124のUI改修後Pendingも維持する。
Chromium/Cageはv1対象外だが既存選択/fallbackコード・既定値は変更しない。
Integrity固有のV2 hardening#250は別所有のままで、本移管に混ぜない。

この移管単位で変更したのはGitHubの所有範囲とbacklog/本記録だけであり、製品コード・runtime設定・配備物は
変更していない。追加local build/test・Pi操作・restart・入力・背景ONも行っていない。
文書はmaster `75df663`から`docs/219-v2-recovery-close`へ分離し、移管時点では未commitだった。
その後の利用者の文書統合指示で、復旧経路と既存試験を再読し、v1で必要な追加コード差分がないことを
再確認した。WPE初回load失敗時の説明可能な復帰や異常時cleanupの改善は#254のV2範囲であり、
この文書PRには実装を含めない。文書だけをcommit・PR/必須CI経由で統合する。
文書検証はIssue/link/close状態照合、全diffと`git diff --check`を用いる。PRの実際のCI/merge状態は
GitHubを正本とし、過去CIを今回の新試験として流用しない。

## #28 WPE移行によるv1完了（2026-10-19）

利用者が、不要なmouse cursorはWeb runtimeをChromiumからWPEへ移行する際に解消したと
実機の状況を報告し、記録した上で#28をcloseするよう指示した。v1の対応・受入対象は
WPEPlatform DRMとし、Chromium/Cageを対象外とする方針も明示された。
この報告と対象範囲に基づき#28をCompletedでcloseした。

旧Cageのinput調査と、2026-10-05のWPE直接DRM PoCでもcursorが残った観測は当時の履歴として
保全する。今回の報告は現行利用環境での解消を示すが、解消したcommitやmechanismの特定、
旧受入matrix全体・cold boot・全input deviceの再検証を行ったという意味ではない。
cursor終了単位では追加Pi観測・TV目視・CEC操作・build/testを行っておらず、利用者の実機報告と
agentによる自動検証を区別する。製品コード、udev/input policy、fallback既定値を変更せず、
既存のChromium明示選択・fallback実装も削除しない。後続#177/#178の配備・service設定は別単位である。

別課題#124のscrollbarは解消扱いにせず、利用者の方針によりv1向けUI改修後の確認までPendingとする。
このUI改修はまだIssue化しておらず、今回新規Issueや追加の受入条件を作成しない。
先行文書は`docs/28-wpe-v1-close`のworktreeに保全し、後述の統合承認でこのPRへ整合させた。

## #177/#178 背景選択の保存とOFF未保存契約（2026-10-19）

利用者が#178と#177を協調して進める方針を確認し、OFFの保存失敗時は「取得・表示を止め、
未保存と明示する」を採用した。master `e08d178`から独立worktreeの
`feature/177-background-persistence`を分離し、既存branch/worktree・dirty差分・artifactは保持する。
先行#28の終了文書は当初`docs/28-wpe-v1-close`へ分離し、後述の利用者承認で今回の統合単位へ含めた。
元のworktree・未commit差分も保全する。

対象はRead Policyと背景要求値の一組保存、version 1読込とversion 2書込、保存ONとavailabilityの分離、
OFF停止を保存失敗で妨げない契約、背景API固有の保存状態とSettings案内、代表software操作試験である。
Read Policyのpendingはeffectiveではなくrequestedを保存し、他方の設定変更で失わない。
保存ONが起動時に利用不能なら要求ON・実効OFFとして理由を返し、通常のON拒否は維持する。
保存先の自動有効化・service設定変更、key管理、画像disk cache、provider追加、UI再設計は対象外とする。

CEC調査では`cec_input_test`の変換/filter、`api_server_test`の実WebSocket配信、
`ui_controls_test.js`のmock semantic入力が存在する一方、物理bus受信からPlayer/UIを通す
tracked自動E2Eは見つからなかった。同一adapterの送信成功を外部受信の証明にしない。
新しいvirtual CEC基盤や別送信装置をv1必須にせず、既存の実WebSocket試験を6 actionの順序付き配信へ、
既存UI試験をsemantic入力だけのSettings開閉・ON/OFF代表操作列へ拡張する。
TVリモコン・実表示・試聴は後続で具体的な操作列・時間・復元方法の承認を得てから別途確認する。
今回は最初の60分を実装・自動試験区間の上限とし、未完なら残件を示して停止する。
試行は初回と一組の修正・再確認を基本とし、新しいコーナーケースを完了条件へ積み増さない。
利用制限で中断後、11:22 UTCに利用者が再開と約1時間の不在中の続行を指示したため、
同じscopeのlocal実装・検証だけを再開した。実機操作やIssue完了の承認とは扱わない。

Pi操作・配備・service再起動、commit/push/PR、Issue closeは当初のlocal実装単位には含めず、
実機操作は後述の別承認で行う。#177/#178をOpenのまま維持し、#51の実写真受入は別単位とする。

最初のbackend/UI委譲差分には未完成の編集があり、07:30 UTC頃の親レビューで
`player_session.cpp`の重複宣言・関数断片混入と、背景ではなくRead Policyに追加した保存案内を確認した。
この差分はbuild・配備しておらず受入に使わない。担当へ編集・build停止を指示し、書込終了の確認後に
親担当が一組の修正として引き取った。これ以前の最小構成41/41は事前基線であり、新保存実装の成功ではない。

### Local実装・自動検証結果

- version 2はRead Policyと背景要求値を一組で保存し、version 1の旧policyは背景OFFで読む。
  起動時に旧fileを書換えず、次の明示保存で移行する。未知version/fieldと背景型不正は不採用とする。
- 復元ONとavailabilityを分離し、利用不能時も要求ONを保持して実効OFF・理由を返す。
  ONの新規要求は利用不能・保存失敗なら409で不変。OFFは保存失敗でも停止して204、
  背景GETは`persistence_state=unsaved`、`requested_source=session`を返す。
- Read Policyと背景の相互更新で他方を保持する。OFF未保存後のRead Policy保存成功でも現在のOFFを保存し、
  Settingsは背景自身の再GETによって未保存案内を解消する。保存先未指定は従来どおりsession限り。
- `read_policy_persistence`は実daemon・loopback API・No Discを使い、旧schema無書換え、移行、
  両方向の値保持、保存ONの利用不能復元、通常ON拒否、rename失敗時のOFF204/旧file保全/未保存、
  policy再保存による復旧、次回OFF復元、ON保存失敗の不変、session限定を確認した。
  keyは合成fixtureであり、全CTestは`--network none`のcontainerで実行し、provider照会や実写真を使わない。
- 既存`api_server`試験は6 semantic actionを一接続で順序付きに受信し、各1回とstate channel非混在を確認。
  `ui_controls_test.js`はsemantic入力だけでSettings→ON→未保存OFF→policy保存による解消→Back/focus復帰を確認。
  これは層別software試験であり、物理CECからWPEまでの自動E2Eとは呼ばない。

最初の全CTestは68/70だった。追加fieldへ未追随の`player_daemon`fixtureを修正し、
Ninja未導入の既定imageによる`build_profile`失敗は、既存の検証用imageを正しい
`PICDPLAYER_BUILD_IMAGE`で指定して再確認した。最初に用いた`BUILD_IMAGE`はscriptの変数ではなく、
既定imageが選ばれていた。build toolやproduct codeをこの環境差のために変更していない。

| 検証 | 結果・境界 |
| --- | --- |
| macOS Node | `ui_controls_test.js`、`ui_render_test.js`成功 |
| Linux/aarch64標準WPE Debug | `PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build PICDPLAYER_BUILD_TYPE=Debug ./scripts/build-container.sh`成功。SDK recipe一致のcache再利用、stage install、package生成、profile一致を確認 |
| 標準WPE Debug全CTest | 最終70/70成功。API/METADATA/WPEはON、PARANOIAはOFF。`--network none`、SDK mountと`LD_LIBRARY_PATH`を使用 |
| 最小構成 | API/METADATA/WPEをOFFにした最終sourceの再build、全CTest 41/41成功。事前基線だけでなく修正後も再実行 |
| metadata非compile・API ON | `build-navigation`でdaemon/store/API testをbuildし、`api_server`/`read_policy_store`/`read_policy_persistence`の3/3成功 |
| Debian lifecycle | 同じDebug/image指定で`./scripts/test-package-lifecycle.sh`成功。disposable container内の検証でありPi配備・systemd稼働確認ではない |
| 差分・静的レビュー | 全diffと`git diff --check`確認。独立したread-onlyレビューで具体的なbehavioral findingなし。新しい物理E2E基盤は追加しない |

既存`api_server.cpp`の`ApiCommand::session_id`欠落初期化warningは残るが、今回このsourceは変更しない。
packageは`package-container/picdplayer_0.1.0_Debug_aarch64.deb`であり、SHA-256は
`70e7800dc2ffd6e89737c4a9d838bee6c24b573f4f239fd52edc56698651ea89`。
stage daemonは`205d8144dad0302207287c4b45dee22910de053ee1fca32c597e697a79d45f19`、
Player JSは`a14dc2f6756223006cced2878470f35bd24ad980b83d0a8c591fac2c278b81bf`。
stageのPlayer JS/HTMLとsourceのbyte一致も確認した。生成時点ではlocal candidateであり、
後述12:26 UTCの配備確認で同じhashを実機と照合した。

### 実機に残す代表受入

同一TV・通常CD一枚でSettings開閉・Back/focus、policy変更のpendingと停止後反映、
背景ON/OFF、短い試聴を確認する。保存・再起動は稼働serviceの`--settings-file`有無を確認してから
操作列・時間・復元方法を合意する。保存先未設定なら設定変更と再起動の別承認が必要であり自動で有効化しない。
今回の自動試験を実service/cold boot/電源断/物理remote/TV可読性/音声の確認へ読み替えない。
Settings/保存の代表受入は約10分枠を基本とし、セットアップが必要なら別途区切る。
#51の写真複数枚・切替・TV・試聴は別枠のままとし、新たなcorner caseを完了条件に加えない。

12:09 UTCの続行指示後、alias `picdplayer-pi`でservice/保存先/profile/hash/APIを読み取ろうとしたが、
解決先のED25519 host keyが既知でなく、`StrictHostKeyChecking=yes`で接続前に停止した。
`ssh-keygen -F`のaliasと解決先照会でも既定known_hostsに一致はなかった。
照合緩和・自動鍵登録・別addressへの接続は行わず、信頼できるhost identity確認を利用者へ求める。
remote commandは実行されておらず、現在のPi状態・保存先・配備hashを観測したとは扱わない。
配備・設定変更・restart・再生/背景/CEC操作も行っていない。

12:14 UTC、利用者がPi consoleで確認したED25519 fingerprintを提示した。取得した公開鍵の指紋と
完全一致を確認し、repository外の作業専用known_hostsへpinして`StrictHostKeyChecking=yes`で接続した。
通常のSSH設定・既存known_hostsは変更せず、鍵自動更新も無効とした。
12:15 UTCのread-only観測では標準WPE Debug、両service active/running、自動restart 0、
STOPPED/AUDIO_READY、track 1、SINGLE要求/実効・pendingなし、背景OFF/available=true、
電源flag `0x0`、61.2°Cだった。`--settings-file`は稼働daemonに付いておらず保存は未有効である。
package/daemon/HTTP Player JS/wrapperのhashは引継ぎの旧配備記録と一致し、新しいlocal候補ではない。
最初のmedia抽出は旧snapshotの`media.state`を誤って参照してnullだったが、公開presentationの
`disc.state`でAUDIO_READYを再確認した。nullを媒体なしの観測へ読み替えない。
この確認でも配備・設定変更・restart・再生・背景ON・CEC入力は行っていない。
次は標準deploy（activeだった両serviceのrestartを含む）、保存先有効化、代表操作と復元の具体案を
利用者と合意してから進める。管理者権限やNOPASSWDを仮定せず、password/ticket共有を求めない。

### 承認した配備・保存先の有効化

利用者は準備・配備10分＋代表受入10分、初回と修正一組、背景ON各90秒以内、
保存確認のdaemon再起動1回を承認した。成功時は新packageと保存有効化を残し、
STOPPED/SINGLE/背景OFFへ戻す。commit/PR/Issue close、#51写真切替、cold bootは含めない。
旧packageは`~/private-177-178-20261019T1220/previous.deb`へprivate退避し引継ぎhashとの一致を確認。
利用者が同directoryへ`/etc/default/picdplayer`を保全し、既存settingsがあれば退避する手順を実行した。
今回は旧settingsの退避物はなく、既存EXTRA_ARGSに保存先だけを追加した。
編集前後の引数を値を公開せず比較し、`--settings-file /var/lib/picdplayer/settings.json`の一組以外は
一致することを確認した。key設定は変更・公開していない。

最初のdeployはtracked-process実行側のシェル展開で一時SSH関数の引数が誤解釈され、
ControlPath長エラーでSSH前に停止した。転送・install・restartはなかった。
blocking shellで同じ関数の引数とread-only SSHを確認し、一組の修正として再試行した。
標準`./scripts/deploy.sh`は変更せず、ephemeral SSH関数と`RSYNC_RSH`へ照合済みpinを渡した。
Debug/profileとpackage hashを確認して通常のdpkg経路で配備し、両serviceはactiveを維持、
実process `picdplayer-wpe`の起動確認までscriptは成功した。

12:26:12 UTCのpost-deploy読取では両service active/running、自動restart 0、
STOPPED/AUDIO_READY、SINGLE要求/実効、pending=false、背景OFF/available=trueだった。
両設定APIの`persistence_configured=true`を確認し、背景は新規保存前のため
`persistence_state=unsaved`/`requested_source=startup`である。保存失敗を観測したという意味ではない。
daemon/package/HTTP Player JSのhashは上記candidateと完全一致した。
保存・実service再起動・リモコン・目視・試聴の代表受入はここから別に確認する。

### リモコン受入の延期と一本化

最初の「リモコンだけでSettingsを開く」案内に対し、利用者は現在実機の近くにおらず操作できないため、
リモコン動作確認を一つのIssueへまとめ、可能なときに着手すると指示した。成功の報告ではない。
依頼に基づき[#252](https://github.com/udonchan/picdplayer/issues/252)を作成し、物理リモコンの
Settings開閉/選択/Back/focus、代表再生/停止・policy pending、背景ON/OFFの唯一の受入記録先とした。
#178は画面実装・表示/API回帰、#51は写真・切替・TV可読性・音声、#177は保存/実serviceを所有し、
それぞれのIssue本文先頭を更新して同じリモコンrunを重複要求しないことを明記した。
旧本文・広い受入条件はhistoryとして保全し、3 Issueをclose・合格に変更していない。

12:31:24 UTCの最終read-only観測で両service active/running・自動restart 0、STOPPED/AUDIO_READY、
SINGLE要求/実効・pendingなし、背景OFF/available=trueを再確認した。
保存先は有効だが背景は`unsaved`/`startup`のままであり、今回追加の設定POST・保存後restartはない。
リモコン入力・背景ON・再生・試聴・TV確認も行っていない。
新packageと保存先有効化は承認どおり残し、旧package/configはprivate backupに保全した。
後日の#252着手時に操作列・基本10分・試行上限・背景ON期限・復元を再確認する。
今回の承認を無期限の入力操作や新matrixの承認に読み替えない。

### 実serviceでの保存・再起動復元（12:37 UTC）

利用者が自動確認を進め、残件を既存/新規Issueへ集約して閉じる方向で進めるよう指示した。
先に承認済みのdaemon再起動1回・背景ON90秒以内を使い、リモコンや再生を使わない代表受入を実施した。
事前にSTOPPED/AUDIO_READY・SINGLE・背景OFF・保存先指定を確認し、serviceの`User=picdplayer`、
実processのuid一致、`StateDirectory=picdplayer`/mode 0700をread-onlyで照合した。

12:37:25 UTC、policyをREPEATへ変更し背景ONを保存した。ONの`persistence_state=saved`と
source `saved`を確認後、`sudo -n /usr/bin/systemctl restart picdplayer.service`を1回実行し成功した。
password/ticket共有、権限変更、sudoers迂回は行っていない。
daemon PIDは13389から13571へ変わり、active/running・自動restart 0で復帰した。
再起動後はRead Policyの要求/実効がREPEAT・両sourceが`restored`、pending=false、
背景の要求/実効がON・source `restored`・state `saved`であり、両設定の実service復元を確認した。
同じAPI値を同じPIDで読み直しただけの試験ではない。

終了処理で背景OFFと元のSINGLE全5値をPOSTした。ON要求前からOFF受理までの上限は約0.99秒で、
90秒を大きく下回る。kiosk PIDは13440のまま、両service active/running・自動restart 0だった。
ただし直後の終了assertは失敗し、当該箇所の値を出力していなかったため、原因や一時的なpendingを
観測したとは断定しない。このcommand全体は非0終了であり、そのままPASSへ変更しない。

追加POST・再起動をせず、12:37:48 UTCに独立したGETで再確認した結果、
STOPPED/AUDIO_READY、SINGLE要求/実効（75 frames / matches 2 / attempts 3 / budget 10000ms）、
pending=false・両source `saved`、背景要求/実効OFF・source `saved`・state `saved`を確認し、
この最終状態のassertは成功した。保存/再起動復元の成功、即時終了assertの失敗、後続の安全状態確認を
区別して残す。検証器を厳密化するための追加restartや新matrixは行わない。

#177のv1代表保存・復元はこの結果とlocal失敗/移行回帰で確認済みとし、通常cold bootを含む
継続運転は既存#184、実リモコンは#252、写真/TV/試聴は#51へ残す。
電源断耐久性を今回のv1終了条件へ追加せず、未検証・非保証を維持する。
#178は実装/表示/API回帰の完了範囲と#252の物理受入を分ける。
この観測時点ではsource/文書が未commit・未push・master未統合のため#177/#178をcloseせず、
Git保存・PR/必須CI経由の統合の承認を得てから終了処理へ進む。

### Git統合とv1終了範囲の承認

利用者は先行#28文書も整合させたcommit・push・PR作成・必須CI成功後mergeと、
#177/#178のcloseまでを一括承認した。v1終了範囲は保存・復元実装と代表自動/実service確認、
Settings実装・表示/API回帰とする。物理リモコンの未実施は#252、写真・TV・試聴は#51、
通常cold boot・継続運転は#184へ明示的に残し、これらを完了扱いにしない。
このPRは#177/#178の終了対象を参照するが、#252/#51/#184を閉じるものではない。
Git/PRの実際の保存・CI・merge・Issue状態はそれぞれを正本とし、この承認だけで統合済みとは扱わない。

## 以前の記録の保存経緯

2026-10-17、利用者がここまでのcommit・pushを承認した。Issue整理は`docs/issue-cleanup-a1-a2`、
診断修正の保存先はそのcommitを土台にする`fix/audio-first-diagnostics`とし、master直接更新・PR作成は含めない。
以下の未commit/未push/未配備/文書未統合という記述は各作業時点の履歴で、現在の保存状態はGitのbranch/履歴を参照する。
未実施の音声/CEC/長期・物理受入は、この保存操作でも完了へ変更しない。

当時の保存branchでは整理文書commit `7b8d099`を土台に、配備用worktreeの差分を3-wayで照合した。
重複したbacklog/verificationは記録を保持して整合し、以下に詳細な音声候補の再現・回帰・配備記録を併記する。
当時のcommit対象の実装・test・CMakeは配備sourceとbyte一致であり、その保存のための再配備やPi操作は行わなかった。

## #164 policy/strategy表示の限定単位（2026-10-18 UTC）

開始時にGitHubの最新master `e6a7f59`、PR #244、必須`build-and-test`成功
（run `39111181094`、head `bcc4413`）とtree一致、#226のCompleted closeを再確認した。
過去の未commit/未統合という記録はその時点の履歴として扱い、#226を再実装しない。
新規worktreeの`fix/164-policy-strategy-display`はこのmasterから分離し、既存worktree・branch・
stash・untracked・build/package・SDK cache・private証跡を保持する。

08:44:38 UTCのPi読み取りで標準WPE Debug、STOPPED/AUDIO_READY、両service active/running、
自動再起動0、flag `0x0`、60.1°Cを確認した。package `dba45279…afa14d`、daemon
`dcd958f8…32dda2`、HTTP Player JS `353e58fb…13fb38`、wrapper `1e792264…ed807`は
配備記録と一致した。追加の読み取りでpackage installedと背景requested/effective=false、available=trueを確認した。
snapshotはpolicy要求/実効SINGLE、direct C2 pointer strategy、速度要求2x・実速度null、offset未指定であり、
全既定値の基線や実速度測定とは扱わず設定を変更しない。
`/api/settings`は存在せず404だったため、実装の正しい`/api/artist-background`で確認し直した。
同payloadのため再配備せず、service・再生・媒体・設定操作や過去試験helperの再実行は行わない。

非BuildrootのOpen Issueを比較し、#178/#124等の実機受入や#177/#238/#239の追加機能・採用判断ではなく、
#164の公開済みpolicy/strategyを標準Monitorへ投影する表示専用単位を選ぶ。
SINGLE/REPEATの要求・実効・保留、strategyの4項目・欠損・互換field不一致を扱い、
Settings操作・schema・read engine・二列構図は変更しない。pending=falseを要求mechanismの成功やPCM品質へ読み替えない。
最初の実装委譲は空の成果報告で終了し、UI/testの差分もなかった。完了と扱わず主担当で実装した。
Read policyのSINGLE/REPEATを同時に並べ、EFFECTIVE/REQUESTEDの文字と枠線でも選択状態を示す。
既知modeでは重複するmode文章を省き、保留状態を別行に示す。未知/欠損modeは要求・実効を明記する。
Strategyは4項目を個別に示し、欠損時も互換fieldをauthoritativeな実効値として補完しない。
互換値が異なる場合はMISMATCH、正本が欠損ならその旨と互換値を併記する。追加取得やpolicy操作はない。

| 検証 | 観測結果と境界 |
|---|---|
| Mac Node | `ui_integrity_test.js`の従来map/世代/503 retryの7 scenarioと追加configuration検査が成功。全降格reason、SINGLE/REPEAT一致/相違、同mode/同strategyでpending、null/欠損/不正型、未知・長文・markup文字列、互換不一致、旧表示消去、同値DOM更新なし、追加map取得なしを検証 |
| 既存UI回帰 | `ui_render_test.js`、`ui_controls_test.js`、`ui_boot_test.js`、`ui_status_test.js`成功。同値STOPPED 60 snapshotでDOM write attempts 0。Settings/背景/CEC経路の自動試験であり実機受入ではない |
| Negative control | 保全済み#226の旧Player JSへ新しいIntegrity試験を適用すると新しい選択ラベル検査が失敗しexit 1。保全元は読み取りのみ。最終新JSでは成功 |
| Linux build | recipe一致のSDKを新worktreeへ非破壊clone copyし再利用。`PICDPLAYER_BUILD_TYPE=Debug PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build ./scripts/build-container.sh`で標準WPE/API/metadata有効のbuild/stage/package生成成功。Piへ配備しない |
| 最終全CTest | 同じLinux/aarch64 imageで`ctest --test-dir build-container --output-on-failure`、70/70成功、51.45秒。UI埋込assetを最終修正後に再生成・再buildして実行。途中のUI以外62/62や暫定版70/70のみで完了としない |
| Package | 同Debugの`test-package-lifecycle.sh`成功。source/stage/archive内のPlayer HTML/CSS/JS 3ファイルがそれぞれbyte一致し、staged daemonは`build_type=Debug profile=Debug` |
| Mac Chrome | headless Chrome 155、公開schemaの有効な1曲layout/2region map・capture時刻・markerを確認する合成fixture。1920×1080/390×844/720×720とnormal/pending/missing/long/hostile/mismatchの18条件が成功。全条件で横overflow・overflowによる文字切れ・例外・想定外の変更POSTなし、要求/実効のaccessible labelと切断後の状態保持/RECONNECTING表示を確認 |
| 1080p退行修正 | 最初は通常条件で縦overflowが発生し、同fixtureの変更前にはないことを確認。新設policyの重複文章とstrategy注記位置・行間/局所spacingだけを調整し、内容を隠さず再試験。最終normal/pending/missing/hostile/mismatchでは縦overflowもなく、全Monitor要素がviewport内。longの220文字級識別子/reasonは折返し、縦scrollを許容する |
| 独立レビュー | 全code/test/docs差分と上記の局所修正を再レビューし、code/testの実質的指摘なし。backlogの未実装表記指摘はこの限定実装と未受入を分けて修正。Node5 scriptとdiff checkも別担当で成功。browser/build/Piは独立に再実行したという意味ではない |

最終local Debug packageは`package-container/picdplayer_0.1.0_Debug_aarch64.deb`、SHA256
`9e2a82768db82a79f3ad89421d8215cd891aabf1c7ddf00f0fc7d8fa8cfb7906`。
最終Player HTML/CSS/JSのSHA256はそれぞれ
`2867953032fbe2918cfc0eaa3e3aecac600386d8d251d702669f781807c512a7`、
`00a6412d47e4b6f1ba590eede1fa13689b5cb903d554e3e0e55d41b425d7bb5b`、
`9297369cbf259d01243d184c83c21348e3afd8fb58f7defe86c4c62d892f0ccf`。
Pi上の既存packageとは異なる。local検証終了時点では未配備・未commit/未push/未統合で、
GitHubのIssue/PR状態を変更していなかった。

browser helperは新しい一時ファイルだけを使用した。初期readiness判定が旧UIを許したsmoke結果は
受入から除外し、厳格化後に新UIのDowngrade labelも認識するよう判定を補修した。
空のtracks等のfixture不備も直し、上表は有効なlayout/mapの描画を必須にした最終結果である。
owned Chrome process/profile/loopback fixture serverは終了・撤去し、asset不変を確認した。
本検査は画像なしfallbackとWebSocket stubによるlocal browser試験であり、実transport/背景共存やPi表示の代替ではない。
Release再build・sanitizer、背景0/1/複数/fallbackとの全面状態別受入、Pi/WPE・TV表示・音声/CEC・長期試験は
この単位で未実施。全長文を1080p一画面へ収める保証や#124のscrollbar原因解消とも扱わず、#164はOpenを維持する。

後続の2026-10-18の確認で、利用者が対象8ファイルのcommit・push、`Refs #164`のPR作成と
必須CI確認を承認した。merge・Issue close・Pi操作は含めない。上記の未commit/未push等は検証当時の履歴であり、
実際の保存・PR・CIの現在状態はGit履歴とGitHubを正本とする。承認だけでそれらの成功を主張しない。

## #164 背景共存の合成検証と長文metadata実験（2026-10-18 UTC）

利用者の追加承認でPR #245を通常mergeし、master `faab0f0`とCI成功head `c7cf88f`のtreeが
`8c31a8c3fe95816546dd16710564a9b164343ae0`で一致することを確認した。必須CI run `39114996518`は成功。
自動削除されたremote branchを同じheadで保全し直し、local branch/worktree・元master・古い
同名origin/master branch・stash・#226保存branchは保持した。#164はOpenであり、Pi配備は含めなかった。

後続依頼では同じ#164の残受入から合成背景との共存を選び、masterから新worktree/branch
`test/164-background-coexistence`を分離した。新機能・schema・二列構図・背景保存・長時間実機試験へ広げない。
09:46:42 UTCのPi読み取りでは旧#226の標準WPE Debug、STOPPED/AUDIO_READY、両service active、
自動再起動0、flag `0x0`、60.1°C、package/daemon/HTTP JS/wrapper hashが以前の配備記録と一致した。
policy要求/実効SINGLE、direct C2 pointer、速度要求2x・実速度null、offset未指定、背景OFFを再確認し変更しない。
新UIの配備はまだない。設定fileのhash/参照先とInspectorのloopback listenを読み取り、内容・秘密を公開しない。
`sudo -n -l`の許可表示だけで実installやsystemctlのNOPASSWDを保証したとは扱わない。

背景検証は新しい一時helper `issue164-background-browser-20261018.py`を用い、既存helperは変更しない。
生成した白/黒/checker PNGだけを同一loopback originで実decodeし、画像0枚のcover/neutral、1枚、
3枚のplaylist、HTTP 404、decode失敗、NoDiscの遅い旧応答、世代置換、切断、pending/UNKNOWN/警告を
1920×1080/390×844/720×720で観測した。transportは明示したWebSocket stubでありdaemon接続の受入ではない。

初回委譲runは検証器のCSS数値表現・DOM id・切断後snapshot・virtual-time待機の不備があり受入に使わない。
合意した修正1回で止めた最終runは86 case/1429 assertion、exit 1、32失敗だった。
内訳は短いalbumのline box寸法差28件（glyph欠落の確証なし）、実際の長文metadata省略3 viewport、
仮想時間下のcrossfade描画1件。数値の大きさや合格assertionだけで全体合格へ変更しない。
親担当も合成PNGと実CSSを確認し、album/album artist/track/track artistの4か所が既存line-clampで
省略されることを確認した。これだけを除去し、既存のwrap・通常font/spacing・grid・背景timer/APIを維持する案を実験した。
この案では非常に長い内容に縦scrollが必要となる。既存`ui_render_test.js`へ4 ruleのwrap/no-clamp静的guardを追加し、
旧CSSを指定するとexit 1、新CSSでは従来DOM dedup/画像 lifecycle/slideshow試験を含め成功することを確認した。
静的guard単独をbrowser表示の証明にはしない。以下の成功は実験の検証であり、採用判断とは区別する。

| 最終追加検証 | 観測と限界 |
|---|---|
| 同じbrowser helperで実験CSSを再測定 | 86 case/1429 assertion、2.610秒。metadata clip flag 31件（実際の長文3件と寸法warning 28件）は再現せず、横overflowなし、通常1080pのNow Playing/Monitorが収まり、狭幅は一列。長文は縦scrollで保持。exit 1は残るcrossfade描画1件であり、全体成功とはしない |
| 背景・状態の限定観測 | layer/source/実decode、404/不正画像fallback、NoDiscで旧画像参照の失効、切断ラベルとsnapshot保持、policy/strategy/UNCERTAIN等のDOMを観測。JS例外/console error/想定外POSTは0。実写真・実WS transport・音声・TVの受入ではない |
| 描画・色の残確認 | productionの45秒dwell/4秒fadeを変えず98,040msを仮想進行し3 sourceを選択/decodeしたが、最初の切替でtarget opacity 0.24に対しcomputed opacity 0。virtual compositorの限界とproduction欠陥を区別できず、全面crossfade合格/不具合確定のどちらにもしない。Chrome `color(srgb ...)`を誤解析するhelperのcontrast比は根拠に使わない |
| 判定器の限界 | pendingの部分文字列assertはNOT PENDINGも含み得るため、このrunだけで正しい保留表示の受入を主張しない。既存`ui_integrity_test.js`の厳格な文字列/boolean検査と分離する。画像request上限検査も短時間の粗い観測で、資源安全性/耐久の証明ではない |
| Linux/aarch64 Debug実験build | 編集後に標準WPEのbuild/stage/packageを再生成し、全CTest 70/70成功、52.26秒。package lifecycle script成功。Release再build・sanitizer・新headのGitHub CIは未実施。実験artifactは配備候補にしない |
| 独立レビューと不採用判断 | 通常1080p geometryは変更前と一致し、4 fieldの省略除去という限定効果は確認できた。一方、functional-designの既存条件は長文でも一次情報/概要を縦scrollなしで確認することで、実験では極端な長文時にMonitorがy約2157〜2748へ押し出される。これは受入未達であり、単に縦scrollを許可する設計追記は条件緩和になるため取り消した。新しいlayout/操作設計へ無断で進まず不採用とした |

修正前の最終private evidenceは`issue164-background-browser-evidence-20261018-e9i2zjc5/`、
`acceptance.json`のSHA256は`faf54358d2a3c86f4bc80995a836d920eb8a30680a07026e308ce8e4736999fb`。
`review-caveats.json`は`af83f903bdc75cde6035a8666fa9130d78c3ebfc2463d296458b155f07bb3746`。
新CSSの再測定は別directory `issue164-background-browser-evidence-20261018-5ct_g55f/`で保存し、
`acceptance.json`は`d734d2e5f93f55abec2d079f5459645a8d64cd7c1e77a76cf0e35d220b8daef4`。
helper内の「line clamps remain unchanged」という固定注記は修正前向けで、後者には当てはまらない。
両方のraw結果や無効なcontrast比を全面成功の根拠へ転用せず、ここに記載した限定観測として扱う。
owned Chrome/profile/loopback serverの終了、実行中assetと旧helperの不変を確認した。

不採用実験のlocal Debug package SHA256は
`515d9cba9ff367ce61f3db3b4edc23c00e3afe9f389545242260f73e428f698c`、
Player CSSは`d72ad5d8f296bb455eec8891e00e003c7eea2ad28cabfd2584fb2c9cb9d3df67`。
以前に検証した#245 packageを新worktreeへclone copyした後、本修正のbuildで新しいpackageへ再生成した。
元の#245 packageや#226のbuild/packageは再生成・上書きしていない。
独立レビュー後、今回自分が編集したCSS/testと条件を緩める設計追記だけをapply_patchで取り消し、
実装・test・設計はmasterと同じ状態へ戻した。利用者/他担当の変更を戻す操作ではない。
実験のsource/testとpackageは新worktreeの`experiment-metadata-wrap-evidence/`へ保存し、
archive名は`picdplayer_0.1.0_Debug_aarch64.not-for-deploy.deb`とした。build/stageは
`build-metadata-wrap-experiment-evidence/`、`stage-metadata-wrap-experiment-evidence/`へ退避し、
移動したCMake treeは証跡専用で再利用しない。package-containerの標準filenameは保全済み#245の
`9e2a82768db82a79f3ad89421d8215cd891aabf1c7ddf00f0fc7d8fa8cfb7906`へ戻し、実験版の誤配備を避けた。
その時点の追加差分は検証/backlog記録だけで未commit/未push。前の承認を次の保存・PRやPi操作へ流用しない。
背景共存の全面受入・実WPE/TV・音声/CEC・長期試験は引き続き未完了とし、#164をOpenで保持する。

その後、利用者は統合済み#245 Debugの限定配備とSTOPPED中最大2分のWPE読み取りを承認した。
対象はpackage `9e2a8276…b7906`で、実験版を除外し、新private backup、標準deploy、既存active serviceの
postinst再起動、異常時の旧package復元だけとした。再生/媒体/背景ON/設定変更/Issue closeは含めない。
見込み3〜5分、配備10分超・認証要求・状態/電源/温度異常等で停止する手順を確認した。
ただし新規preflight/backup helperは参照session値の誤りで停止し、1回の再試行も設定digest参照値の
不一致で停止した。いずれもbackup作成より前であり、Piへのpackage転送・install・restartは未実施、
新private backupもまだ作成していない。これは新規補助toolの参照値不備で、Piの故障や設定変更と断定しない。
10:09 UTCの追加読み取りでは旧payloadのままSTOPPED/AUDIO_READY、service active、再起動0、flag 0、58.0°C。
実APIと設定digestを読み直してlocal helperだけを修正し、独立レビューで参照値一致・guard後の新規private保存のみ・
install/service変更なしを確認した。さらにcopy元を検証した絶対pathへ揃え、copy後package digestの実照合を追加した。
修正ガードはまだPiで再実行していない。自己申告した再試行上限を超えて黙って続けず、追加試行の確認待ちとした。

利用者の再開承認後、修正ガード1回が成功し、新しいprivate backupを作成した。旧package copy後の
SHA256 `dba45279…afa14d`を検証し、設定tar/hash・before API状態/policy/背景・serviceをprivateに保全した。
その後、標準`deploy.sh`でpackage `9e2a8276…b7906`を配備し、実install・service verifierが成功した。
この実行では非対話の固定dpkg installが成功したという事実であり、一般のsystemctl操作や将来の認証を保証しない。
権限回避、sudoers変更、手動service制御や別sessionへのticket引継ぎは行っていない。

| 実機の限定確認 | 観測結果と境界 |
|---|---|
| 配備payload | package `9e2a82768db82a79f3ad89421d8215cd891aabf1c7ddf00f0fc7d8fa8cfb7906`、daemon `29e3e8c5d3ac2b5dcd209921b472433c94e747bf9df4b79812fd34f18d7eac63`、HTTP Player JS `9297369cbf259d01243d184c83c21348e3afd8fb58f7defe86c4c62d892f0ccf`が検証済み候補と一致。wrapperは`1e792264…ed807`のまま、profile/build typeともDebug。実験版`515d9cba…698c`は配備していない |
| serviceと電源 | daemon/kioskともactive/running、自動再起動0、実WPE processを標準verifierが確認。起動時刻10:22:45 UTC。配備後flag 0・62.3°C、最終読み取りflag 0・59.1°C。これ以降の長期安定を主張しない |
| 設定・媒体 | 設定hash照合とoptional kiosk設定file不在を再確認。before/afterのpolicy/background JSONをparseした値は同一（byte列は末尾改行のみ異なる）で、これを設定変化としない。SINGLE・direct C2 pointer・速度要求2x・offset未指定・背景OFFを維持。14 track TOC、track 1/position 0、STOPPED/AUDIO_READYがbefore/after/finalで一致 |
| read根拠 | current/latestはnullのまま、16個のread statsは全て0で一致し、coverageも0。実WPEではcurrent/latestをNOT AVAILABLEとし、CLEANへ補完していない。restartでsession IDは変化し、stream generationは3のまま。非zero失敗履歴の保持を実証したという意味ではなく、before記録をprivate backupに残す |
| 実WPE表示 | 既存loopback Inspectorを一時SSH forwardし、既存pageのread-only DOM/geometryとPNGを取得。1920×1080、SINGLEのEFFECTIVE/REQUESTED、REPEATの表示、Pending: NOT PENDING、strategy要求/実効・pending・Downgrade NONE、warning NONE、STOPPED/CONNECTEDを観測。5領域が全てvisible/viewport内、横/縦overflowなし。REPEATの選択属性や全状態をこの観測だけで受入済みとはしない |
| 時間・終了 | capture markerは10:23:22〜10:24:00 UTC（38秒）、Inspector接続1.733秒。service起動から最終runtime読み取りまで75秒で、承認した2分以内。capture markerはsnapshot exposure時刻の独立証明ではない。観測後SSH forwardを終了し、restart/再生/媒体/背景ON/設定変更/追加観測loopは行わない |
| 独立なlocal証跡照合 | 別担当がlocal before/after/final JSON・TOC・old archive hash・geometry・PNG header/bytesを確認。実SSH commandやTVを再実行したという意味ではない。private directoryは0700、PNG/観測JSONは0600。final state JSONだけ0664で作成されていたため、新規artifactのremote/local両copyを0600へ修正し確認。内容変更や再runtime観測はせず、元から0700のdirectory内だった |

新しいremote private backupとlocal private証跡directoryへ旧archive・設定・JSON・PNGを保全した。
画像やraw JSON/configはGitへ含めず、公開しない。rollbackは不要で、統合済み#245 DebugをPiへ残した。
TVの目視・試聴・PLAYING/pending/NoDisc/長文等の実WPE各状態・実artist背景の0/1/複数/failure受入・
crossfadeの全面描画・CEC・長期耐久は実施していない。#164は引き続きOpenとする。
local検証終了時点のtracked差分はbacklog/verificationの2文書だけで未commit/未push、GitHub Issue/PRは変更していなかった。
その後、利用者はこの2文書だけのcommit・push・`Refs #164`のPR作成・必須CI成功後の通常mergeを承認した。
Issue close・追加Pi操作・実験版採用・private証跡のstage/公開は含めない。これら未commit等は当時の履歴であり、
保存/PR/CI/mergeの実際の状態はGit履歴とGitHubを正本とする。承認だけで成功を主張しない。

## #164 合成2画像の実時間crossfade限定確認（2026-10-18 UTC）

最新master `6c7ca85`から`test/164-real-time-crossfade`を分離し、既配備#245 head `c7cf88f`から
実装/UI/testに差分がないことを確認した。前節の仮想時間での描画未確認を補う目的で、Mac headless Chromeの
1920×1080・normal motion・合成白/黒PNGの最初の1切替だけを実時間で観測した。
公開契約の45000ms dwell/4000ms fadeは変更せず、HTTP/WS transportだけをloopback fixtureに差し替えた。
productionのclock/timer、render/image callback、styleは差し替えず、Pi操作・再配備・実再生は行わない。

新しい一時helper `issue164-wall-clock-fade-20261018.py`を作成した。初回runは51.296秒で画像切替を
観測したが、正常なboot POST 5種類を同一pathへの過剰要求と誤判定しexit 1（30/31成功）。
継承fixtureの不完全なCLEAN evidenceとevent windowも見つかったため、1回の修正にまとめ、null current/latest・
STOPPED/IDLE・position/queue 0・空event window・降格理由と整合する合成C2 NOへ変更した。
初回のCLEAN表示をPCM品質や正しいread evidence保持の根拠にはしない。初回証跡は上書きせず保存した。

修正後の1回だけ再測定し、exit 0、32/32検査成功、Chrome phase 51.291秒（各run上限60秒）を確認した。
従来のNode `ui_render_test.js`も全3 PASS group成功。同値STOPPED 60 snapshotのDOM write attemptsは0。

| 観測 | 限定結果 |
|---|---|
| 実dwell | 白→黒GETのserver monotonic差45.015156208秒、CDP差45.003097秒。44秒付近では旧画像opacity 0.24、新画像source未設定 |
| 実fade | midpointの旧/新computed opacityは0.118002/0.121998。new layerのtransitionstart→endは4000.900ms、CSS elapsedTimeは4秒。約4.5秒後と後続sampleで旧0/新0.24。旧srcは保持され、削除されたとは扱わない |
| 実paint | before/mid/after PNGの同じ16×16 margin領域の平均RGBは約(43.25,46.25,46.81)→(26.35,28.91,30.31)→(14.09,16.62,17.62)。独立担当もPNGのCRC/decodeと値を照合し、全時間非表示ではないことを確認。可読性/WCAGの証明ではない |
| foregroundと要求 | 全6 sampleでpolicy/strategy、warning NONE、STOPPED/IDLE、position 0、current/latest NOT AVAILABLE、接続表示が不変。GETは8 path各1回、POSTは同じpage idの期待するboot event 5種類各1回だけ。想定外POST/外部origin/JS errorなし |
| 不変・終了 | UI 3 asset・旧helper・CDP source・初回証跡の11 hashが不変。worktreeの実装/testに差分なし。owned Chrome/profile/HTTP serverを終了し、証跡directory 0700・各file 0600。独立レビューは再実行ではなくcode/保存記録/合成PNGの照合 |

helperのSHA256は`98d34619adc6254b9f73fc6bf303056e6f894ce32f99bfdd1cc7a4dc1629b8bf`。
最終private証跡directoryは`issue164-wall-clock-fade-20261018-88aozu9w/`、`report.json`のSHA256は
`bf2a556ec669ced1032fbae9f1113f3fce38d804e73da282b898418ccf72d648`。合成PNG/JSON/helperはGitに含めない。
これで確認したのは同じproduction UIのMac Chrome実時間2画像1切替であり、以前の仮想時間下失敗の原因を
virtual timeだけと断定しない。3枚shuffle/繰返し/refill、他状態、実写真contrast/WCAG、Pi/WPE/TV、音声/CEC、
長期受入や#164全体の完了ではない。実装修正・追加Pi操作は不要と判断し、#164はOpenを維持する。
local検証終了時点の追加tracked差分はこの記録/backlogだけで未commit/未pushだった。
その後、利用者が今回の2文書だけのcommit・push・`Refs #164`のPR作成・必須CI成功後の通常mergeを承認した。
Issue close・追加Pi操作・private証跡のstage/公開は含めない。保存/PR/CI/mergeの現在状態はGit履歴とGitHubを正本とし、
検証時点の未commit等を現在状態へ読み替えず、承認だけで実行成功を主張しない。

## #164 長文名の縮小と末尾省略

2026-10-18 UTC、利用者が「ある程度まで詰め、それでも長い末尾を`...`へ置き換える」方針を明示した。
最新master `b484154`から`fix/164-fit-long-metadata`を分離し、album/album artist/track/track artist/drive名の
5欄だけを対象に、実幅で通常値→90%→80%→75%（12px未満にしない）の1行表示とgrapheme単位の末尾省略を実装した。
省略時の全文はtitle/aria-labelに残し、元snapshot・計測値・warning/pending/降格理由を変更しない。
名前変更、幅/viewport変更、初回fonts.readyで一つのrAFへ集約して再評価し、短文や広幅へ戻れば復元する。
以前の単純なclamp除去とscroll許容の不採用案とは異なり、今回は名称の省略を明示承認された条件として設計に反映した。

最初の実装委譲は途中で切れた報告のみで、UI/test差分もなかったため完了とは扱わず主担当で実装した。
試験側の別委譲は4個の既存Node試験を更新し、他のJSON fixture/既存assertを弱めず必要なbrowser API mockと
独立した幅近似fixtureを追加した。利用制限で中断した後、worktreeと最新masterを照合して再開した。
一時browser helperにも補修が残っていたため、説明だけで完了とせず内容を確認して実行した。

| 最終検証 | 観測結果と限界 |
|---|---|
| Node 5 script | render/integrity/controls/status/bootが全て成功。既存の60同値STOPPEDはDOM write attempts 0、60進捗更新は72を維持。追加の60同値metadataを含むrevision/position/公開済みread_calls更新はmetadata write 0・fitting read 0。縮小/省略、Unicode、literal markup、raw値維持、同じ省略prefixの末尾変更、NoDisc、幅0、resize/fonts.ready、ROの高さfeedbackを確認 |
| Negative control | 保存済み旧JSへ新しいrender試験を適用すると、名前5欄の観測検査が失敗してexit 1。保存元は読み取りのみ。新JSでは成功 |
| 最初の実寸検証・修正 | 狭幅の`1fr`がnowrap文字列のintrinsic幅へ広がり、省略されない問題を検出。独立レビューで固定列幅時のresponsive font cache失効漏れと、検証器のfont相対letter spacing補正漏れも確認。`.details`のmin-width 0、native resizeでのfont再評価、検証器のcross-size測定を一組で修正した。body/cardのoverflowを隠して成功にしていない |
| Mac Chrome 最終36 case | 1920×1080、390×844、720×720で短文・中程度・長いLatin/CJK/grapheme/markup・置換・60同値更新・短文化・NoDiscを検証し、幅変更6 caseも含め失敗/JS error 0。通常/長文の1920はNow Playing/Monitorがviewport内、狭幅は一列・横overflowなし。中程度は全文のまま90%、長文は下限とASCII `...`、title/ARIA全文を確認。数値・warning・pending/reasonは非省略 |
| 幅変更と通常比較 | 初回font確定を待ち、native resizeだけで狭→広→狭を検証。1920→1280→1920ではalbum列792px一定でもfontが36→34.56→36pxへ再計算。変更前と短文のfont 48/24/32/16/17px・各欄rect・Now Playing/Monitor geometryは一致するが、CSSのnowrap/overflow指定まで同一という意味ではない |
| 描画上の限界 | grapheme caseにinkがtight line boxを越すwarning 5件あり、overflow visibleで実際のclip ancestor逸脱はない。警告0とはしない。実font差し替え後のloadingdone、全OS/font、Pi/WPE/TVの描画や可読性は未確認 |
| Linux/aarch64 Debug | 標準WPE SDKのrecipe一致を確認して新worktreeへ非破壊cloneし、build/stage/packageを新規生成。全CTest 70/70成功（初回52.06秒、追加fixtureの診断更新を公開済みread_callsへ揃えた最終再検査53.30秒）。package lifecycle成功（legacy fixture→candidate upgrade→同artifact reinstall→purge）。candidate単独のfresh installを別に実施したとはしない |
| Artifact・独立レビュー | source/stage/archiveのHTML/CSS/JSがそれぞれbyte一致し、staged daemonのprofile/build typeはDebug。独立レビューと一組の修正後の再レビューで実質的な残指摘なし。review担当のbuild/browser再実行や実機受入を意味しない |

browserのbaselineは`issue164-fit-SYNTHETIC-yfhzlhn1/`、最初の失敗は`issue164-fit-SYNTHETIC-s612df4w/`、
最終結果は`issue164-fit-SYNTHETIC-i68j6fz8/`へ上書きせず保存した。最終reportのSHA256は
`996257042b7ebbc72f8c964c187d5dcaa1fa6355045ebe6bbd240bf8f3395276`、
helperは`97c475115885cd6d2d8356e4b5f10db0d6961fdf9c6e61d8b8fec8739469a137`。
各runは実行中の入力hash不変とworktree status/HEAD不変を検査し、owned Chrome/profile/serverを終了した。
raw reportにはstatus_after自体を保存していないため、status前後の独立再照合まで記録済みとはしない。
合成文字列/PNG/JSON/helperはGit対象に含めない。

local Debug package SHA256は`cc3ac5151c252ac86f4a6313d50155b0d4edd1381868ba4e536cd582b045f08b`、
CSSは`56ebde8298d501294c55cf8ac185f1ccb9f14026dad198689a45405be7d105f4`、
JSは`d8b0f62e5c536e4a762cd5d8060760cdca588508d4c415b36fa11eb9546bec09`。
local検証終了時点ではPi操作・再配備・commit/push/PRを行わず、未commit/未統合/未配備で保持した。
その時点でRelease再build・sanitizer・新headのCI、実WPE/TV確認、背景共存の全面受入は未実施だった。
その後、利用者は対象10ファイルだけのcommit・push・`Refs #164`のPR作成・必須CI成功後の通常mergeを承認した。
Issue close・追加Pi操作/再配備・private証跡のstage/公開は含めない。保存/CI/統合の現在状態はGit履歴とGitHubを
正本とし、当時の未commit等を現在状態へ読み替えず、承認だけで成功を主張しない。#164はOpenのままとする。

## #164 背景・状態別受入の準備と停止

PR #248の必須CI run `39176250497`成功後に通常mergeし、master `659c6fc`と実装head `cf04f14`のtreeが
`ce5f2570e75e8358afb166626b089d465826ca1d`で一致することを確認した。保存branch/worktreeは保全した。
その後の依頼に対し、同masterから`test/164-state-acceptance`を分離し、配備と背景・状態別確認を準備した。
2026-10-18 23:44:29 UTCのPi読み取りでは旧#245 Debug package `9e2a8276…b7906`、daemon `29e3e8c5…7eac63`、
HTTP JS `9297369c…2f0ccf`、wrapper `1e792264…ed807`。STOPPED/AUDIO_READY、両service active、再起動0、
flag 0、58.5°C、SINGLE/direct C2 pointer・速度要求2x・offset未指定・背景OFFだった。以後の状態は仮定しない。
新候補`cc3ac515…5f08b`を新worktreeへclone copyし、profile Debugとarchive内UIがmaster sourceとbyte一致することを
確認した。archive内daemonは`1ee3eb9e4855f3325fa6dabb33df126e6dd29295d7236fb7992c7a8df54e79c9`で、旧payloadと区別する。
新private backup・標準deploy・一時WPE fixture画面・最大30秒再生と復元の具体案を提示したが、選択回答はなく実機操作の
承認は未確定。Piへのbackup作成/転送/install/restart/画面切替/再生/背景設定/媒体操作は一切行っていない。

ローカル専用の新一時helper `issue164-state-acceptance-20261018.py`を作成した。初回は1.90秒、cover-onlyの
1観測を試みたがscrollHeight 53/clientHeight 50というline box差をclipと誤判定しexit 1、合格観測0。
独立レビューは実clip判定、接続/例外監視、CSP、同sessionのrevision/cache、N/A、C2 fallback、NoDisc/切断保持、
request budget、収束・deadline/cleanupの不足をまとめて指摘した。helperだけを1回補修し、productionに変更はない。

補修後の許可した1回はexit 1、cleanupを含め1.354秒でbootstrap中に停止した。desktop 0/16、狭幅0/4、
切断/再接続0/2、同context 0/4で、準備したmatrixの合格観測は0。5件の`Network.loadingFailed`が
`type=Fetch`、`canceled=true`、`net::ERR_ABORTED`でFATAL判定された。Runtime例外/console errorは記録されていないが、
未実行matrixの正常性や製品不具合の証明にはならない。server側には5種類のboot POSTに対する204があるものの、
request IDとURLの対応をreportに残しておらず、5件のcancelがそのboot要求だったとは確定できない。
fatal gateにより失敗時PNG取得も止まり、補修後のscreenshotはない。FAILをPASSへ変換せず、再試行上限で停止した。

初回証跡は`issue164-state-SYNTHETIC-2pw080ep/`、補修後は別の`issue164-state-GROUPED-SYNTHETIC-6o5rdtcq/`へ保存。
補修後helper SHA256は`56ca6b33e9d4d29a771056cef1d0640ba7278f9c438b9f71f5acffa757c44f1b`、
reportは`9262bddc4bf1903dd08d0e5f80490924a5f1dfd27471c4fc157b5e10c349d139`。
8 cleanup/検証stage成功、owned Chrome終了/profile除去/server close、入力hash/Git status不変を記録し、
親担当も現在のsource差分なしとhashを照合した。独立再レビューは保存code/reportの確認であり再実行ではない。
補修したclip/error/CSP/context等のmatrix動作はruntime未検証。未使用`--serve`のdefault 180秒/max 300秒は、
local runの120秒上限と別の将来用server modeであり、この回で実行していない。

次はrequest ID→URLの相関を保存する最小bootstrap診断で、cancelの原因と安全ゲートの分類を特定する必要がある。
予期しないJS/CSP/通信失敗を一括で無視して受入を通す修正はしない。追加診断は新たに範囲/試行上限を定めて行い、
この判定器を合格済みとしてPiへ持ち込まない。#248配備と実WPE確認には実機操作の明示承認も必要なままである。
helper/合成画像/raw JSONはGitへ含めず、その時点の追加tracked差分はbacklog/verificationの記録だけ。未commit/未pushで保全し、
#164はOpenを維持する。今回1〜3の受入を完了したとは報告しない。

## #164 新Debug配備と実背景1枚の停止中確認

2026-10-19 UTC、利用者が実機検証の継続を依頼した。00:23:56の読み取りでは旧#245 Debug、
STOPPED/AUDIO_READY、両service active、再起動0、flag 0、59.1°C。対象・操作列・時間・中断条件・復元方法を
具体化し、利用者が新private backup・標準Debug配備・通常WPE確認・背景ON最大90秒/OFF復元を明示承認した。
未合格fixture、実再生、媒体/policy/速度/offset/volume操作、cold boot、TV目視、Issue closeは含めない。

現在値から作成した新guardで旧package/config/policy/媒体を照合してから新private backupを作り、copy後の
旧archive `9e2a8276…b7906`を実照合した。標準`deploy.sh`とservice verifierが成功し、新#248 Debugを残した。
実機packageは`cc3ac5151c252ac86f4a6313d50155b0d4edd1381868ba4e536cd582b045f08b`、daemonは
`1ee3eb9e4855f3325fa6dabb33df126e6dd29295d7236fb7992c7a8df54e79c9`、HTTP JSは
`d8b0f62e5c536e4a762cd5d8060760cdca588508d4c415b36fa11eb9546bec09`、HTTP CSSは
`56ebde8298d501294c55cf8ac185f1ccb9f14026dad198689a45405be7d105f4`と候補に一致した。
wrapperは`1e792264…ed807`のまま。固定dpkg呼出しの成功を一般のsudo/systemctl権限保証へ広げず、権限回避はない。

| 実機での限定確認 | 実際の観測と境界 |
|---|---|
| 通常画面 | 既存normal Playerだけをread-only Inspectorで観測。1920×1080、CONNECTED、5領域がvisible/viewport内、横/縦overflowなし。名前5欄はnormal font 48/24/32/16/17pxで実幅内、dirty/fit scheduledはfalse、fonts loaded。source文字列と表示が一致。これは実長文の省略/resize動作を実機確認したという意味ではない |
| 初回背景ON/OFF | APIのONを観測し、ON要求前からOFF確認まで64.059秒、controller全体68.579秒。30 sample、エラー0。候補URLは0→1枚だが、ON撮影が到着前だったため保存PNGはcover fallbackと同一。controller成功を実画像表示成功とせず、追加ONは自動実行しなかった |
| 追加承認と実画像 | 利用者が追加で最大90秒のON/OFFを1回承認。変更しない安全controllerで64.062秒後にOFFを確認（全体67.883秒）、30 sample、エラー0。別read-only observerはONから待機して1回撮影し、30.028秒で完了。API/currentSnapshot/DOMの同じ候補1枚がcomplete、naturalWidth 1920、opacity 0.24、cover opacity 0になり、主担当もprivate PNGで背景とforegroundの配置を確認した |
| 表示・根拠 | 実画像ありと通常画面のmetadata/診断表示・共通geometryが一致し、1920×1080で5領域がviewport内、横/縦overflowなし。current/latestはNOT AVAILABLE、warning NONE、16 read statsは全て0のまま。CLEANへ補完せず、SINGLE・direct C2 pointer・速度要求2x・offset未指定・track 1/position 0・14-track TOCを維持した。実速度やPCM品質の試験ではない |
| OFF・終了状態 | 2回ともrequested/effective=falseへ復元。artist両layerのsrc消去/opacity 0とcover opacity 0.24を保存。最終markerは01:00:56 UTC、daemon PID 11762/kiosk PID 11788、両active/running、自動再起動0、flag 0、58.5°C。設定hashとoptional kiosk設定不在を主担当が再照合し、SSH forwardを終了した。new Debugを保持しrollback不要 |
| 独立local照合 | 別担当がAPI/WPEのrevision/artwork/player一致、source/currentSrcと候補の対応、画像decode/opacity、状態/policy/TOC/stats不変、private directory 0700/file 0600、旧archive hashを照合。SSH/deploy/TVの独立再実行ではない。ON PNGとOFF PNGのbyte hash差も確認したが、decoded pixel差分やWCAG contrastの証明ではない |

背景制御はlocal curlの全体timeoutと各操作の単調時計deadlineを使い、観測予算をON+72秒までとしてOFF/確認/fallbackの
予算を確保した。Inspector処理は独立したread-only subprocessを12秒上限とし、停止してもOFF制御を塞がない構造を
独立レビューした。これはホスト停止や通信断でもOFFを保証するものではない。今回の実測では両回とも90秒未満で復元した。
準備/承認待ち時間と実ON時間は分け、連続耐久試験として扱わない。この作業によるservice再起動はpostinstの2 serviceだけ。

新remote private backupとlocalの`issue164-normal-live-20261019-YW65c9/`、
`issue164-background-live-20261019-6naXHT/`、`issue164-background-repeat-20261019-pBjs6H/`、
`issue164-image-live-20261019-izuLst/`に前後JSON/PNG/旧archive等を保全した。実写真・個人metadata・URL・raw configは
Gitに含めず公開しない。中間の旧観測/初回の到着前撮影や未合格fixture証跡も上書きしていない。
得られた候補は最大1枚で、複数画像/crossfade/失敗fallback、PLAYING/pending/NoDisc/長文/接続断等の実WPE各状態、
TV目視/試聴/CEC/長期受入は未確認。未合格matrixはFAILのままで、#164全体をcloseする根拠にはしない。
local検証終了時点のtracked差分はbacklog/verificationだけで未commit/未pushだった。
その後、利用者は今回の2文書だけのcommit・push・`Refs #164`のPR作成・必須CI成功後の通常mergeを承認した。
追加Pi操作・Issue close・private証跡のstage/公開は含めない。保存/PR/CI/mergeの現在状態はGit履歴とGitHubを正本とし、
当時の未commit等を現在状態へ読み替えず、承認だけで実行成功を主張しない。

## #164 v1完了とv2以降への移管

2026-10-19、利用者は#164の検証がコーナーケースと検証器の厳密化へ広がっていることを指摘し、
v1完了を優先して追加hardeningをv2以降へ延期する方針を明示した。既存Issueとの非重複の所有範囲を照合し、
新規フォローアップ1件・#164のCompleted close・記録2文書のcommit/push/PR/必須CI成功後mergeを承認した。

v1の完了範囲はPR #245のpolicy/strategy表示、PR #248の長文名の段階的縮小/末尾省略、欠損・不明値を成功へ
補完しない既存意味の維持、代表自動/browser試験と限定実WPE確認、関連文書の整合とした。
根拠は上記実装PRとCI、Mac代表18/36条件、Linux/aarch64試験、PR #246/#247/#249の記録であり、
今回の終了判断のために追加実装・検証器改修・新たな実機試験は行っていない。

GitHubの#164本文に現行v1範囲・根拠・限界・移管先を明記し、当初本文と未完了checkboxは履歴として保全した。
06:17:03 UTCに`state=closed/state_reason=completed`を確認した。元の全受入条件が全platform/全実機状態で
成功したという意味ではなく、利用者が承認したv1範囲を完了し追加条件を延期した終了である。

| 残課題の所有先 | 今回の扱い |
|---|---|
| #250（新規、v2以降） | Integrity Monitor固有の深い状態×背景×platform/viewport、非典型font/長文条件、fixture/network分類と正負例の追加hardening。非blocking・現時点では着手しない。全直積/全OS/fontを新しい完了条件にしない |
| #51/#184 | 実写真・切替・TV可読性と限定音声/CEC、通常運転・操作・継続受入は既存の責務を維持。Issue全体をv2へ延期・完了扱いにしない |
| #49/#178/#124/#83/#177 | 画像資源、Settings、scrollbar原因、性能、保存は既存所有先のまま。新しい重複条件を追加しない |

旧matrixのFAIL・未評価、未確認の実機状態、private画像/JSON/config・build/package・保存branch/worktreeは保持する。
追加bootstrap診断の未commit記録は元の`test/164-bootstrap-network`worktreeへ保全し、今回の終了記録に混ぜて
受入PASSとしない。未実装classifierや今後のmatrix再試行は#250の将来検討であり、#164を再びblockedにしない。
通常利用で具体的な再現bugが見つかった場合は、そのbugだけを別途判断し、包括hardeningの完了待ちにはしない。
今回は記録2文書だけの更新で、製品コード・公開API・標準レイアウト・Pi配備を変更しない。保存/PR/CI/mergeの実際の状態は
Git履歴とGitHubを正本とし、過去のOpen/未commit記述は各観測時点の履歴として読む。

## #226 deployの起動待機契約（2026-10-18 UTC）

master `1a44aaf`から専用worktree/branch `fix/226-deploy-startup-wait`を作成し、
deployの約35秒判定とwrapperのAPI待機最大300秒の不整合を扱う。最初のlocal作業ではPiの操作・配備・
service変更、設定/sudoers変更、commit/push/PRを含めず、後続の別承認による通常配備を本節末尾に分けて記録する。
最初の実装委譲は利用制限・接続切断で中断し、再開時点の差分は親担当の3文書だけだった。
その状態を実装完了や自動試験成功とは扱わず、同じ限定scopeで再開した。

委譲終了後も実装4ファイルに差分がないことを確認したため、主担当で実装・試験を行った。
wrapperを起動待機予算の正本とし、deployはインストール済みwrapperの内部照会
`--startup-wait-budget-seconds`から上限を取得する。現在は最大API待機300秒、APIのkill猶予1秒、
WPE version確認5秒とkill猶予1秒で307秒。deployの確認猶予5秒を加えたmonotonic期限を使う。
設定が既定30秒の場合も共通の保守的な上限で、systemdの実効環境を別layerで再解釈しない。
既定値・許容範囲・runtime選択・fallbackは変更しない。

観測コマンドとsleepは残りcentisecondの`timeout --signal=KILL`で制限し、遅れた観測を成功にしない。
queryは別の5秒＋kill猶予1秒で制限する。これはruntime確認区間の期限であり、SSH転送・dpkg全体や
OS schedulingに対するhard real-time保証ではない。inactive kioskを起動せず、query/観測失敗、
不正な予算、期限超過、停止/失敗・PID不在、WPE/Chromium不一致を失敗として扱う。

| 検証 | 結果・境界 |
|---|---|
| Bash構文 | deploy・wrapperの`bash -n`成功 |
| 仮想時間mock | 実heredocと実template queryを実行。40秒/300秒相当の遅延、期限直前/同時/超過、待機したまま、遅れた観測、wrapper予算変更への追従、query/観測失敗・不正値、fallback/明示Chromium、inactiveを検証 |
| Negative control | fixture内だけに旧masterのdeployを入れ、40秒/300秒の2 subcaseが失敗することを確認。保全worktreeの変更はなし |
| 実timeout | Linuxの実GNU timeoutが、残り0.01秒の観測でblockするcommandを終了し、PID観測失敗にすることを確認 |
| Wrapper | 予算queryはXDG/設定なし・不正設定下でも307だけを返す。API待機1/300秒を受理し0/301秒を拒否。既存profile/fallback等を含む11/11成功 |
| 標準WPE Debug | `picdplayer-profile-review-build` arm64 imageとコピー先の既存SDK cacheを使用。標準`build-container.sh`で最終sourceを再生成・build/stage/package成功 |
| 全CTest | Linux/aarch64、API/metadata/WPE ONで70/70成功（49.97秒）。`deployment_scripts`はgenerated maintainer scriptを含め35/35、skipなし |
| Package | 標準`test-package-lifecycle.sh`成功。archive内wrapperはgenerated scriptとbyte一致し、XDGなしでquery=307を確認 |
| 独立レビュー | 空PIDを既定値へ置換するfixtureと、丸め/kill猶予による期限超過を指摘。両方を修正し、限定再レビューで残指摘なし |

最初のfixture試験では空PID caseが失敗し、上記の修正後に成功した。追加archive照合の最初の実行は
shellの引用符処理で展開先を渡せず失敗したため、成功へ読み替えず、専用helperでbyte一致とqueryを再確認した。
変更対象外66件と暫定packageの先行成功だけで完了にせず、最終sourceで全70件とpackage回帰を実行した。
このDebug検証時点ではRelease build・sanitizerは再実行していない。後続のRelease検証を以下に記録する。

新規worktreeのlocal検証用Debug package SHA256は
`dba452796904dba50c419838ade6dd9f75829e0f328031a840389c46ffafa14d`。
このlocal回帰時点ではPiへ配備していないartifactで、#121の配備packageとは異なる。元worktree/cache/branch/stashは保持した。

### Release回帰（2026-10-18 UTC）

後続の継続依頼で、製品コードを変えずReleaseでも同じ標準手順を実行した。先に今回のDebugの
build/stage/packageを専用worktree内の`build-issue226-debug-evidence/`へclone copyし、packageのbyte一致と
上記SHA256を確認した。コピーしたCMake build treeは証跡保存用で、別pathから再configureして再利用しない。
元の保全対象worktreeのartifactは変更せず、専用worktreeの通常出力だけを標準scriptでReleaseへ更新した。

- `PICDPLAYER_BUILD_TYPE=Release PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build ./scripts/build-container.sh`成功。
- 同じLinux/aarch64 imageで全CTest 70/70成功（50.20秒）。wrapper/profile・deploy/mockも含む。
- 同profileの標準`test-package-lifecycle.sh`成功。archiveのbuild-profileはReleaseで、wrapperはgenerated scriptとbyte一致。
- archiveのwrapperはXDG設定なしでquery=307を返す。保持したDebug packageのSHA256は不変。

Release packageは`package-container/picdplayer_0.1.0_Release_aarch64.deb`、SHA256は
`c2066cc5997663145d8b81aeb7312bbf6d076d4bf50a41c66d03b0a981623533`。
Release回帰終了時点の`build-container/`・`stage/`・`package-container/`はこのReleaseの出力を指し、Debugの証跡は上記別directoryに保持した。
この時点では両方ともlocal検証用で、Pi配備・GitHub公開・版付きrelease候補生成は行っていない。
sanitizer・実機の長い待機は未実施のまま。

local回帰完了時点では未commit・master未統合・未配備で、GitHub IssueはOpenのまま変更していない。

### 通常Debug deployの限定実機確認（2026-10-18 UTC）

利用者が実機確認を依頼した後、現在状態を読み取り確認し、Debugを維持した通常deployを1回だけ行う
対象・時間・restart・保全・中断条件を別途確認した。再生/CEC/媒体/cold boot・設定/sudoers変更、
35秒超の実systemd遅延注入、自動rollback/再配備、dpkg途中の強制終了は承認範囲に含めない。
70°C以上・電源/USB異常・想定外の状態/設定/hash・配備/観測失敗では追加操作を中止する条件とした。

Releaseの通常出力を`build-issue226-release-evidence/`へ保持し、Debugの証跡から通常出力へclone copyした。
コピー元Debugと保持先Releaseのpackage SHA256は上記値と一致し、上書き/削除した保全対象worktreeはない。
現在の通常`build-container/`・`stage/`・`package-container/`はDebugで、両profileの証跡を別directoryにも保持する。
Debug archiveのdaemonとPlayer JSは以前の配備と同じhashで、wrapperのみ新しい予算照会を含むことを確認した。

| 項目 | 観測 |
|---|---|
| 直前状態 | STOPPED/AUDIO_READY、WPE Debug、両service active。private backup準備完了時63.4°C、flag 0 |
| 保全 | targetの新しいprivate directoryに旧package、実効設定/optional不在、unit/引数/policy/背景/boot、API state、journalを保存。開始前12 files、終了後18 filesでdirectory0700/files0600・ownerを確認。以前のprivate backupは変更しない |
| 配備 | `PICDPLAYER_BUILD_TYPE=Debug PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build ./scripts/deploy.sh`を1回実行して成功。active→activeを維持し、標準dpkg処理に伴うdaemon→kioskの新invocationと開始時刻順を確認 |
| 実runtime | deploy自身が`expected=wpe process=picdplayer-wpe`を確認。version/profileの標準検査も成功。実wrapperのquery=307 |
| package SHA256 | `dba452796904dba50c419838ade6dd9f75829e0f328031a840389c46ffafa14d` |
| daemon / HTTP Player | `dcd958f8a9f6e1e8c1638e622fee8b845e927f317f443fdf1f2a8b2ff532dda2` / `353e58fbd4203669ee9ce3986f9e4db8c00fa43363663380c886566f8a13fb38`。配備前後で確認 |
| wrapper SHA256 | `1e792264e1cc2c2c8af9299adbba16691419371d74e31d273dc4ee04115ed807`。Debug archive/generated script/実配備が一致 |
| 設定維持 | 取得した実効設定・unit内容/参照先・起動引数・policy全体・背景requested/effective/available・bootが前後一致。捕捉した設定源の比較であり、未知の全persisted stateの完全backupとは主張しない |
| 終了後観測 | 30秒の観測区間で約5秒間隔の7 sample。検査・保存処理を含め33.119秒で07:06:15 UTCまで確認。STOPPED/AUDIO_READY、実WPE、両service active、auto restart 0、観測最大65.0°C、flag 0 |
| journal | 読み取り権限警告なしでservice/kernelを取得。配備前cursor以降、stall/underrun/read error、電源/USB異常、service WARN/ERROR等のguarded eventは該当0。raw証跡はtarget private directoryのみ |

通常deployが失敗したという報告はなく、新しい契約を実Pi上で実行できた。ただし起動を意図的に遅らせていないので、
35秒超の実systemd待機・上限/期限・API未起動/fallback/inactiveの実機受入まで成功としない。
これらはmock/限定Linux試験の成功と区別する。再配備後のplay/pause/seek・CEC・試聴・TV/DRM表示・長期試験も未実施。
設定内容・API key・画像・raw journal・private backupの実pathをGit/GitHubへ含めない。
この通常deploy確認時点では未commit・master未統合でGitHub Issue #226はOpenを維持した。#219の復旧matrixや#184の受入も別枠のまま。

### 45秒実待機の開始前中断（2026-10-18 UTC）

後続の継続依頼で、kioskだけをrestartし、試験専用`/run` drop-inで待機先を予約したloopback port、
待機上限75秒へ一時変更する1回の試験を別途承認した。実APIの8080は停止せず、portを起動45秒後にlistenさせ、
deployからそのまま抽出したruntime verifierで実wrapper→実WPEを確認する条件だった。
試験上限90秒・70°C/flag/log/stateによる中断、成功/失敗のいずれも専用変更だけを撤去してkioskを元設定へrestartし、
30秒確認する。daemon restart・package再配備・再生/CEC/媒体/cold boot・既存設定/sudoers変更は含めない。

実行前にhelperのmarker解釈・後始末・期限・設定差分/衝突・復元確認を独立レビューし、指摘を修正した。
Linux上の限定helper試験4件（PAM sessionのmarker/PID帰属、WARN捕捉、実行中/終了済みobserverの子process終了）も成功。
これはhelperの試験であり、45秒の実機受入の代替ではない。

07:37:26 UTC、最初の`sudo -n systemctl edit --runtime ... --stdin`が失敗した。
復元経路の`sudo -n systemctl daemon-reload`も失敗したため、承認した中断条件に従ってroot操作は追加試行しなかった。
sudo監査2件はいずれも`a password is required`。事前の`sudo -n -l <command>`成功はコマンド実行許可の確認であり、
NOPASSWDの保証ではなかった。無対話で実行できると解釈した事前判定を訂正する。

07:38:18 UTCの読み取り確認では、以下が成立した。

- 試験drop-inとそのruntime directoryは存在せず、最初の編集は成立していない。
- daemon/kioskともactive、PID・invocation・開始時刻・設定参照先は試験前と同一、auto restart 0。
- 取得した設定hashとunit environmentも不変。STOPPED/AUDIO_READY、61.2°C、flag 0。
- kiosk restart・45秒待機・TCP listen解放には到達していない。package再配備やdaemon restartも行っていない。
- 新しいtarget private directoryにbefore/失敗summary/読み取り事後確認を保存した。旧private backupは変更しない。

結果は**開始前中断・未実施**で、正常な復元操作が完了したという意味ではない。
変更自体が成立していないことを別の読み取り確認で確かめた。通常deploy成功やmock成功を、この実待機の成功へ読み替えない。
sudoers変更、dpkg経由の迂回、資格情報の取得、root操作の自動再試行は行わない。
次にこの条件を実施するには利用者側の対話認証/手動操作を含む段取りが必要で、現時点の非対話権限だけでは続行しない。

後続の手順提示依頼で、同じ対話SSH端末から`sudo -v`で認証し、一般userのままhelperを実行する段取りを用意した。
最終helperのLinux限定4試験とbundle構文を再確認し、Piの新しいユーザ領域directoryへ0700/0600で配置、転送前後hashも確認した。
root実行・非TTY・認証ticketなしでは変更前に中断するguardを付けた。配置だけでhelper自体は実行せず、service/root操作も行っていない。
パスワードをagentへ渡したり、別SSH sessionへticketが引き継がれると仮定したり、sudoersを変更したりする手順にはしない。
手順提示時点ではこの対話実行の結果は得ていなかった。後続の限定成功を以下に分けて記録する。

### 対話実行による45秒超の実待機成功（2026-10-18 UTC）

利用者が一般userの同一対話SSH端末で認証し、配置済みhelperを1回実行した。
07:53:56〜07:55:34 UTCの出力を受領後、主担当はroot/service操作をせずprivate証跡と現在状態を読み取り照合した。
agentの別SSHへsudo ticketが引き継がれたとは扱わず、sudoers変更・資格情報の共有/取得はない。

| 項目 | 観測・証跡 |
|---|---|
| 開始条件 | STOPPED/AUDIO_READY、実WPE Debug。preflight温度60.7°C |
| 35秒超の観測 | verifier起動から36.160秒、service開始から37.117秒の時点で確認処理がまだ待機中だった |
| TCP待機解除 | service開始45.707秒で専用loopback portをlisten。接続1件を観測。実API 8080は停止していない |
| 実wrapper marker | 同じMainPIDの`api_wait_start=160`、`api_ready=46000`、`wpe_exec=46000`（各wrapper_elapsed_ms）。実待機差分45,840 ms |
| 実verifier | deploy.shから無変更で抽出したruntime確認処理がexit 0、`expected=wpe process=picdplayer-wpe`を確認。保存sourceのSHA256とtrial記録も一致 |
| 復元 | 試験drop-inと作成したruntime directoryは撤去済み。元の設定hash・unit参照先/environment・policy/背景/bootを確認し、復元後WPEと30秒観測が成功 |
| daemon | restartを要求しておらず、試験前後でMainPID・invocation・開始時刻が同じ。session/stream/disc世代もguard対象 |
| 電源・温度 | 最大観測68.8°C、70°C中断上限未満。throttling/guarded journal異常による中断なし。ただし長期の温度安定性は未確認 |
| 結果 | `LIMITED_REAL_WAIT_PASS`、failureなし、restored=true。試験区間90秒上限内に正の待機確認を完了し、復元確認は別区間で実施 |
| private証跡 | 15 files、directory0700/files0600・ownerを読み取り確認。設定・raw journal・sudo情報・実pathは公開しない |

08:02:35 UTCの追加読み取りでも、両service active、auto restart 0、STOPPED/AUDIO_READY、実WPE、flag 0、60.7°C。
daemonは試験前と同一invocation、両serviceは保存した復元後記録と一致。一時file/directory不在と、捕捉した設定hashの不変を確認した。
配備package `dba45279…afa14d`とwrapper `1e792264…ed807`も従来記録と一致し、再配備は行っていない。

この成功は、意図的にTCP readinessを遅らせた条件で、35秒を超えても実systemd wrapperを待ち実WPEを受理できることに限定する。
実daemon/APIの起動そのものを遅らせた試験、最大300秒・期限超過・API永続未起動・fallback/inactiveの実機matrix、
再生/試聴・TV/DRM表示・CEC・長期試験は未実施。既存のmock/自動試験成功と区別し、#121/#184の全受入には広げない。
この限定検証を記録した時点では未commit・master未統合で、GitHub Issueの更新/close・push/PRは行っていなかった。

### 保存・統合・closeの承認

2026-10-18、利用者が#226の対象8ファイルのcommit/push、`Refs #226`のPR、最新headの必須CI成功後の
保護規則に従うmerge、master反映確認と公開可能な完了記録・backlog整合を経たIssue closeまでを承認した。
Piの再配備/restart/再生操作は含めない。既存worktree/branch/stash/cache/private証跡を保持し、
自動head削除時は同じSHAで作業branchを保全する。
この承認自体はGitHubでの実行結果ではない。実際のcommit・PR・CI・統合・Issue状態はGit履歴と#226の対応PRを正本とする。
以上の未commit/未統合/未配備等は各検証時点の履歴であり、後の保存・統合を未実装と読み替えない。

### 実機journal帰属の補正

実機では`PAMName=login`によりkiosk stdoutがserviceとは異なるsession scopeへ帰属し、
serviceの`_SYSTEMD_INVOCATION_ID`だけを指定するとwrapper markerを取り落とすことを確認した。
markerの実形式は`event=api_wait_start/api_ready/wpe_exec`と`wrapper_elapsed_ms`であり、`boot_ms`ではない。
この試験helperでは開始前cursor以降、実MainPIDと`SYSLOG_IDENTIFIER=picdplayer-kiosk`を対応付けて読む。
service `_SYSTEMD_UNIT`だけでなく当該identifierのWARN/ERRORも確認対象とする。
通常deploy区間（07:04〜07:06:16 UTC）もこの帰属で読み直し、対象7 recordsにguarded pattern/priorityの該当0を確認した。
先のservice invocation抽出だけで全kiosk stdoutを網羅していたとは扱わず、この追加確認を分けて記録する。
raw journal・sudo policy出力・private証跡の実pathはGit/GitHubへ含めない。

## #121 retry修正版の実機配備（2026-10-17 UTC）

後続の明示依頼で`fix/audio-first-review`から標準WPE Debug packageを作成・配備した。
元worktree・旧packageは保持し、旧package/実効設定3ファイル/起動引数/runtime状態をtarget側private directoryへ保全した。
SDKはrecipe一致の非破壊コピーを再利用。既存標準imageのNinja不足で初回profile試験だけ失敗したため、
source/packageを変えず既存review imageで全70/70を再実行し、package lifecycleも成功してから配備した。

配備package SHA256は`0333ffbb321583f20a7c915f0edc646608faeabf46fcd15fe111c5c9aebe4616`。
package/daemon/HTTP Player JSのhash・Debug/WPE identity、設定3ファイル・optional file不在・起動引数・policy・
背景requested/effective/availableの維持、同boot・STOPPED・低電圧flag 0を確認した。
map 200/history 503→200のcapture/世代整合、実WPEのconnected/新helper/取得済みmap/取得時刻を読み取り、
1920×1080 rendered-page snapshotをprivateに保存・目視した。今回のSTOPPED条件ではoverflowなし。
23:07:40 UTCまでの新service invocationにstall/underrun/WARN/ERROR検索該当なし、自動再起動0、kernel追加entry0。

play/pause/stop/seek/CEC/tray/hotplug・音声/試聴・負荷/長期試験は行っていない。PNGは物理TV/DRM scanoutではない。
写真/key/設定/raw journalはGit/GitHubへ公開しない。#121と#184等はOpenを維持し、総合受入に読み替えない。
詳細は修正用worktreeの`docs/development/verification.md`へ保持した。commit/push/PR・文書の一括統合は未実施。

## 整理後の#121独立レビュー進捗（2026-10-17 UTC）

後続のコード作業はIssue整理とは別単位で実施した。元の音声優先診断worktree・配備済みpackageを保全し、
基準master `55263c4`から作った`fix/audio-first-review`へ候補22ファイルをbyte一致で複製した。
2担当の読み取りレビューでpublisher/狭いengine統合には具体的指摘がなく、UIの503 retryで2不具合を再現・修正した。
旧contextにretryを奪われる停止中の未取得と、成功後も旧timerが残って100 ms間隔になる冗長取得を扱う。
同disc/stream変更でも追加snapshotや手動Refreshなしに有界間隔で新streamを取得する。
旧返答によるmap採用・retry予約・新要求のloading解除を拒否する。

最終Node testを保全元Playerで実行すると元の1 caseは成功・追加6回帰は失敗、修正版は7 case成功。
修正版をread-only mountした既存Linux/aarch64 imageの新規tmpfs buildで、WPE OFFのDebugは
API/metadata ON 69/69、OFF 41/41成功。既存package/buildの結果を新sourceの証拠に代用していない。
この独立レビュー段階では実機・標準WPE build・sanitizerを再実行せず、配備/音声/CEC/長期受入完了とはしなかった。
後続配備は上記の別承認・別検証記録で扱い、#121はOpenを維持する。

詳細な再現・red/green・補助CMake手順とCustom UI/API手順の整合差分は、`fix/audio-first-review`の
`docs/development/verification.md`等に未commitで保持する。Issue整理6文書との差分はまだ一括統合しておらず、
commit前に双方を照合する必要がある。独立レビュー段階では配備/Pi操作を行わず、commit/push/PRは引き続き未実施。

## Issue整理 B/C/D/Eと残課題（2026-10-17）

A1/A2に続けて整理を進める利用者承認に基づき、同じmaster `55263c4`の既存実装・PR・コメント・
設計と検証記録だけを照合した。以下のClosedは今回新たにhardware検証したという意味ではない。
過去節のOpen・未統合・承認境界は当時の記録として保持し、現在の所有先は本節とbacklogを参照する。

| 単位・Issue | 結果・根拠 | 残る条件と所有先 |
|---|---|---|
| B: #214/#215/#216 | 各限定scopeでCompleted。元コメントのPi Chromium合成fixture/decode/DOM/実HTTP-WS/cleanup、label mutation 1→0とNode回帰を確認。旧Draft #217からPR #223へ継承済み | 元の固定SHA・artifact・失敗・hidden/headlessの限界を保全。通常daemon/光学/ALSA/WPE写真/TV/音声/CEC/長期の完成ではない |
| B: #50 | Completed。PR #175/#223/#230のmetadata先行・artwork/背景独立status・世代照合、実HTTP/WSの1→2→3枚と失効回帰 | 取得安全性#49、写真受入#51、通常長期#184、性能#83 |
| C1: #17/#18/#21 | Not planned/superseded。古い親trackingを終了し、元本文/リンクを保全 | #40の設計判定、#42/#177/#178、#45/#46/#47/#66〜#68/#147をそれぞれ所有先にし、親のCloseを全機能成功にしない |
| C2: #25/#229 | Not planned/superseded。PR #230の実API・WPE写真・基本再生報告と未確認を保全し、重複tracking終了 | 取得/decoded資源#49、実写真切替・時間条件・TV可読性・音声/代表CEC#51、保存#177、Settings#178、通常長期#184、性能#83 |
| D2: #188 | Not planned/superseded。WPE採用で解決したとはしない | 初回document取得失敗、generic browser error残留/復帰、有界retry・非退行を#219へactive checklistで移管。追加復旧検討は必要時まで保留 |
| #40 | 設計Completed。詳細設計の同一TOC非識別、player_sessionのLOADING遷移時だけ失効、MetadataSessionの世代拒否と同一TOC再要求fakeを元の2受入条件へ対応付け | 同一TOCの実物別discを対象Piで確認していない。未観測交換/物理同一性を保証せず、必要時の観測#88・通常再挿入#184を参照 |
| E: #74 | 調査Completed。#75〜#78・PR #218の保全と#198〜#200への分割で元scope完了。Open索引維持を文書/固定SHA索引へ置換 | #198/#199/#200は追加研究保留。製品採用/最終build/Pi周辺機器未検証を維持。元branch/worktree/試作は削除しない |

GitHubの#83/#124/#164から旧親#25のblocked-byを削除した。背景runtime/UIはPR #223/#230で利用可能で、
disk cache・設定保存・長期受入を開始条件に積み増さない。#51の#49全体待ちも解除し、実装済み供給部分と
未完了の資源安全性を本文で分けた。Closed #150/#50等は充足済み前提として関係を保持する。
#47→#46と#239→#238/#88、充足済みA/Bの関係、親子リンクは保持した。

#49/#51/#177/#178の古い包括的許諾gateは、現行の利用者判断・製品無保証・既定OFF・明示ON・写真非同梱へ
明示的に訂正し、旧本文を履歴欄に保持した。APIアクセス制約と写真の許諾は別で、未確認を確認済みにしない。
背景保存/次回起動復元は#177で未実装、decoded寸法/memoryは#49で未保証として残す。

#10は補正実装/限定Pi成功と試聴/TV端表示待ち、#88はsoftware統合と物理hotplug受入待ち、
#121は未commit診断修正と過去control/CEC未解明、#184は通常継続受入未完了としてOpenを維持した。
直近は#121→#184、独立tooling #226、次順位#164に絞る。外部照合#238/#239、quiet boot/read-only root、
Custom UI更新、image/compliance、正式WPE供給とBuildrootは必要性/供給段階/安全環境待ちの保留条件を明記した。
全Open Issueに非空本文があること、終了16件（Completed 10件・superseded 6件）のreasonと移管先、
残る34件の集合/状態/依存とmaster不変をGitHubから機械確認した。変更6文書のローカルリンク/anchor 20件、
Issueの現行説明に追加した固定commit sourceリンク/anchor 22件の検査と`git diff --check`が成功した。
source/include/tests/UI/build/systemd/CIの差分が空であることも確認した。

作業中、保留13件（#19/#20/#42/#46/#47/#68/#69/#147/#198/#199/#200/#238/#239）の本文生成で
`gh --jq`へ非対応の引数を渡し、空本文を送信してしまった。直ちに編集履歴から更新前本文を取得し、
末尾改行を含む全文一致を13件すべてで機械確認して復元した。その後は本文非空・元本文保持・更新後全文一致を
検査して位置付けを追記した。状態・コメント・元の受入条件は消去せず、編集履歴にも経緯が残る。

今回はbuild/CTest/sanitizerや物理試験を再実行せず、実装・公開schema・Pi・key・画像/private rawを変更しない。
新規Issue・commit・push・PRは作らず、文書を前回と同じ専用worktreeの未commit差分へ集約した。
音声優先診断worktreeの未commit差分と元repositoryの保護refs/stash/untrackedは別に保持する。

## Issue整理 A1/A2（2026-10-17）

利用者が根拠付き整理案を承認したため、最新master `55263c458dda1adabf61d206aa05ec1bcc2e4958`と
GitHubの本文・後続コメント・merge履歴を照合し、次の4件を限定scopeの完了としてCloseした。
以下の元の設計・実装記録にあるOpenやIssue close対象外は、当時の状態・承認境界として保持する。

| Issue | 完了判定の根拠 | Closeに含めないもの |
|---|---|---|
| [#156](https://github.com/udonchan/picdplayer/issues/156) | [PR #234](https://github.com/udonchan/picdplayer/pull/234)、merge `2e82168`。STOP継続、他3案の不採用理由・由来・有限予算・最終STOP・再検討条件を設計とmessage contractへ統合 | 追加fallbackの実装・実機確認。採用候補がないため新しい実装Issueは作らない |
| [#160](https://github.com/udonchan/picdplayer/issues/160) | [PR #235](https://github.com/udonchan/picdplayer/pull/235)、merge `1b65683`。完全取得入力・canonical・I/O調停・取消/解放・容量/期限・再起動破棄の設計と#236〜#239への分割 | production capture、checksum/provider、実機全域取得。子の未実装を親設計の未完了にしない |
| [#236](https://github.com/udonchan/picdplayer/issues/236) | [PR #240](https://github.com/udonchan/picdplayer/pull/240)、merge `5c266de`。pure input coreのfake・ASan/UBSan 71ケース、標準Linux 67/67・最小39/39等の当時の成功記録 | 実reader・thread・runtime/API・provider。memoryの宣言/予約検査をprocess RSSの保証にしない |
| [#237](https://github.com/udonchan/picdplayer/issues/237) | [PR #241](https://github.com/udonchan/picdplayer/pull/241)、merge `5a80077`。owner/coordinator・取消と解放待ち・旧結果拒否のgate付きfake、ASan/UBSan・TSan各57ケース、標準Linux 68/68・最小40/40等の当時の成功記録 | production factory/API/providerと実drive/Pi。開始gateはcallerの申告、ioctl強制中断や物理同一性の保証ではない |

今回の整理ではbuild/CTest/sanitizerを再実行せず、上記を新しい実行結果として扱わない。
新しいsource・test・公開fieldを追加せず、#238/#239の未実装、#88の物理hotplug/reset受入待ち、
#13がClosedでも外部照合未実装という境界を維持する。親子・blocked-by関係は削除せず、A/Bの限定実装が
完了したことをIssue状態で表す。Pi操作・providerアクセス・配備も行っていない。

文書は最新masterから分離した`docs/issue-cleanup-a1-a2` worktreeの未commit差分として保持し、
今回commit・push・PR作成は行わない。GitHubのClose時点で、この整理文書はmaster未統合であることも明記する。
音声優先診断の未commit修正は別worktreeへ保持し、本整理へ混在させない。
## retry修正版Debugの配備と読み取り確認（2026-10-17 UTC）

利用者の実機deploy・可能な確認を進める依頼により、`fix/audio-first-review`の未commit sourceから
標準WPE Debug packageを作成・配備した。元の`feature/audio-first-diagnostics`と旧packageは変更しない。
元SDK exportを新worktreeへ非破壊コピーしrecipe一致を確認したため、WebKit再buildは行わなかった。

標準`build-container.sh`のpackage生成・profile検査は成功した。既存の`picdplayer-build` imageには
現Dockerfileで要求するNinjaがなく、初回CTestはbuild_profileだけ失敗した（69/70）。製品不具合や実機失敗ではない。
source/packageを変えず、Ninjaのある既存`picdplayer-profile-review-build`と同じSDKで全70試験を再実行し成功
（37.35秒）。`PICDPLAYER_BUILD_TYPE=Debug`の標準package lifecycleも同image指定で成功した。
失敗した試験を除外したり、過去70/70を今回結果へ代用したりしていない。

配備直前はSTOPPED/AUDIO_READY、両service active、`get_throttled=0x0`を再確認した。
旧package（`1bee8096…`）・実効設定3ファイル・起動引数hash・policy/背景API・boot IDを
target側の新規mode 0700 directoryへ保全し、設定とraw記録は0600で保存した。
未存在のoptional kiosk EnvironmentFileは作成せず、実際のEnvironmentFiles/drop-inをsystemdから照合した。
今回の保存内容・key・raw journal・画像はGit/GitHubへ含めない。

標準`PICDPLAYER_BUILD_TYPE=Debug ./scripts/deploy.sh`で同版更新し、両service active→activeと
WPE main process/2.54.0/Debug identityを確認した。service再起動は配備に伴うものだけで、Pi rebootはしていない。

| 配備同一性 | SHA256 |
|---|---|
| package | `0333ffbb321583f20a7c915f0edc646608faeabf46fcd15fe111c5c9aebe4616` |
| daemon | `dcd958f8a9f6e1e8c1638e622fee8b845e927f317f443fdf1f2a8b2ff532dda2` |
| source/stage/target/HTTP Player JS | `353e58fbd4203669ee9ce3986f9e4db8c00fa43363663380c886566f8a13fb38` |

設定3ファイルとoptional file不在、起動引数、policy全体、背景requested/effective/availableは配備前後で一致した。
同boot、STOPPED、低電圧flag 0、service自動再起動回数0を確認した。offset/背景保存等の設定を追加していない。
`/api/disc-map`はhistoryなし200、`/api/read-history`は503→2.1秒後200で、session/stream/disc世代と
正のcapture sequence/時刻を照合した。503を音声失敗や最新観測の保証へ読み替えない。

既存Debug Inspectorへ専用loopback tunnelを張り、実WPEでready=complete/connected・修正helper読込み・
同discのmap取得完了・UTC capture表示・未処理retryなしを読み取った。resource記録はmap GETが
開始937 ms/3100 msの2回、詳細history GETは0回だった。初回map GETのHTTP statusまではこの記録で確定しない。
STOPPEDの1920×1080 rendered-page PNGをprivateに保存して目視し、この条件ではdocument overflowなし。
画像はGit/GitHubへ含めず、PNGを物理TV/DRM scanout確認とは扱わない。今回のtunnelだけを終了した。

23:07:40 UTCまでの新service invocationではdaemon 30 entry/kiosk 1 entry、underrun/main_loop_stall/read_stall/
WARN/ERRORは検索上0件、保全開始後のkernel追加entryは0件、`get_throttled=0x0`だった。
停止中の短い確認であり、音声継続や低電圧問題全体の解消ではない。
play/pause/stop/seek・CEC・tray・hotplug・負荷/長期試験は実行せず、#121/#184等を受入完了としてCloseしない。
commit/push/PRは未実施、Issue整理文書との一括統合もまだ行っていない。

## 音声優先診断の独立レビュー（2026-10-17 UTC）

Issue整理後の別作業単位として、基準master `55263c4`に対する未commit候補を2担当で読み取りレビューした。
publisherの所有権・active/pending上限・旧context拒否・解放と狭いengine統合には具体的な新規不具合を認めなかった。
これはhard real-timeや物理audioの保証ではなく、shutdown join・同期state/WS・他APIの残る境界は維持する。

Playerの503 retryでは2件を実source/既存Node harnessのメモリ内実行で再現した。

- P2: STOPPEDのAが503でtimerを持ったまま新session/disc Bへ移り、Bも503になるとBのretryが予約されない。
  Aのtimerが旧contextとして終了した後、Bに追加snapshotがなければ10秒経っても`Map not obtained`のまま。
  同discのstream変更だけでは停止中の自動取得は直ちに発生せず、明示Refreshが503になる場合に同じ欠落を確認した。
- P3: 503が100 msで返り2100 msのretryを予約した後、2000 msの通常取得が成功しても古いtimerが残る。
  自動要求時刻`[0, 2000, 2100]`をassertし、100 ms間隔の冗長要求を確認した。

最初の再現driverはP2新session/discを確認後、同disc stream変更の自動取得に関するhelperの誤った期待で失敗した。
期待を修正した2回目のdriverは成功し、明示Refreshを伴う同disc条件とP3を確認した。helperの失敗を製品不具合に数えない。

元の`feature/audio-first-diagnostics`を変更せず、tracked差分19ファイルと指定済みuntracked 3ファイルだけを
`fix/audio-first-review` worktreeへ複製し、22ファイルのbyte一致を確認してから修正を開始した。
この複製段階ではbuild/package/SDK/private証跡は複製・削除・上書きしていない。
この時点のUI修正は旧配備package（`1bee8096…`）に含まれず、上記の後続承認で新packageを配備した。
旧package SHA256とPlayer JS SHA256が下記配備記録に一致することをローカルで再確認したが、Piへは接続していない。

修正前候補を既存Linux/aarch64 image `e8373978b572`へread-only mountし、networkなし・tmpfs buildで
API/metadata ON・WPE OFFのDebugを再configure/buildしてCTest 69/69成功（36.23秒）を確認した。
既存試験が緑でも上記timer競合は残っていたため、独立した回帰を追加した。
既存の標準scriptはpackage/stageを作り直すため、この保全レビューでは補助CMake手順を同じcontainer内で使う。

### retry修正とNode回帰

UIの要求owner世代を内部で管理し、旧返答によるretry予約・map採用・新要求のloading解除を拒否する。
session/disc変更と新しい取得では不要timerを破棄する。同discのstream変更では保持済みmapを消さず、
未取得なら既存timerまたは進行中だった要求の残る2秒間隔を最新streamの取得へ引き継ぐ。
新streamの100 ms時点では要求せず、追加snapshot/手動Refreshなしでも2000 msで再取得する。
PCM選択/STOP/API/JSON ownerのC++実装は保全元とbyte一致のまま変更しない。

最初に追加した4回帰は修正前にすべて失敗し、修正後に成功した。親の再確認で同disc/無操作の2経路が
最初の修正では残ると分かり、1回の限定修正を追加した。既存503 timerあり/旧fetch進行中の両経路で
要求が`[0]`のままになるREDを確認し、最終的に`[0, 2000]`となるGREENを確認した。
scenario RESTのsnapshotを実JSON同様にcloneして、fixtureの変更が保持snapshotへ伝播しないようにした。
元のtest defaultは変更していない。これはhelperの共有参照を直したもので製品API仕様変更ではない。

`node tests/ui_integrity_test.js`は元の1 caseと、新session/disc、明示Refreshを伴う同disc stream、
手動操作なし同disc stream（timerあり/旧fetch進行中）、成功後の冗長retry抑止、遅れた旧503の6回帰、計7 caseが成功した。
`ui_controls_test.js`、`ui_render_test.js`、`ui_boot_test.js`、`ui_status_test.js`も成功した。
fake時間/HTTP/DOMの結果であり、browser/WPE/実CD/音声/CECや長期受入ではない。

親担当も最終testへ保全元Playerを渡して、元の1 caseだけ成功・追加6 caseすべて失敗することを再確認した。
同じtestを修正版Playerで実行すると7 case成功する。前者のexit 1は意図したnegative controlである。

```sh
node tests/ui_integrity_test.js ../audio-first-diagnostics/ui/default/player.js
node tests/ui_integrity_test.js
```

### 修正後sourceのLinux再検証

修正後の`audio-first-review`をread-only mountし、同じnetworkなしimage・tmpfsに新規buildを作った。
WPE OFFのDebugでAPI/metadata ONはbuild/CTest 69/69（37.63秒）、続けてAPI/metadata OFFは
別build/CTest 41/41（10.20秒）で成功した。diagnostic isolation/publisher/UI integrationを含み、
既存build binaryや配備packageの実行結果を代用していない。保全元とbyte一致のC++/header/CMake/testは14ファイル。
通常Debugのfake/loopback試験であり、今回ASan/UBSan/TSan・標準WPE build・実hardwareは再実行していない。
UI 7 caseとこの構成回帰を、Piの音声/TV/CEC・長期受入の成功へ読み替えない。

Custom UI手順の標準map endpointと世代/取得時刻/503説明、message contractの追加snapshot fieldを整合した。
Issue整理文書は別の`docs/issue-cleanup-a1-a2` worktreeに未commitで保持され、このworktreeへ一括統合していない。
commit前には両方の文書差分を照合する必要がある。独立レビュー段階では配備/Pi操作を行わず、
上記配備は後続の明示承認で実施した。commit/push/PR・総合実機受入は未実施。

## 音声優先の診断投影（2026-10-17）

基準masterは`55263c458dda1adabf61d206aa05ec1bcc2e4958`。音声を優先し、integrity JSONは必須だが
生成/配信待ちで音声を止めない方針を利用者と合意してローカル実装した。commit/pushは未実施。
後述の別承認で修正版DebugをPiへ配備したが、音切れ解消の受入は未完了。

修正前のPiでは独立給電・cold boot後にboot IDの変更と`get_throttled=0x0`を確認した。
承認された短時間play/pause/resume中、13:20:32.787 UTCにALSA write underrunを再現した。
直前のapi_serviceは155,485 us、engine tick gapは166,090 us、queued_blocks=50、last_read_us=4,958、
read_inflight_us=0だった。観測contextはgeneration1/PRESENT/INITIALのまま、試験前後と監視中の低電圧フラグは0。
開始以降のkernel journalに追加entryはなく、13:20:33.380 UTCにstop、STOPPED/IDLEを確認した。設定変更は行っていない。
最初の監視helperはjournal no-matchのexit1とAPI snapshot反映待ちを扱えていなかったため、前者は再生前に中断、
後者はplay直後にstopした。監視helperを修正してから上記の有界試験を行い、アプリの異常とhelper都合を混同しない。
この結果は電圧実測・電源全般の解消やUSB異常の原因確定ではない。抜き差し時刻不明の過去bootのUSB/低電圧記録とは分ける。

修正前sourceの履歴providerを合成データで分解計測した。ローカルLinux/aarch64 container、Debug相当、
各60回の中央値は16件1.45 ms/約12 KB、64件4.86 ms/約48 KB、128件9.42 ms/約95 KB。
128件の構造体copy平均約0.005 msに対しJSON構築平均約5.08 msで、JSON copy/破棄とdumpも加わる。
copyをmoveへ置換するだけの試作は同一文字列で6.96 ms。最適化ありの現行経路は1.71 msだった。
これらはPiの処理時間・speedup予測ではなく、履歴件数と負荷の対応、不要copyだけでは同期境界が残る根拠である。
実機api_service全体の内部時間分解はしていない。

候補実装は履歴/map JSONの構築・破棄を専用ownerへ分離し、最大1 active/1 pending・2秒refreshで集約する。
通常mapは履歴を含めず、詳細履歴の契約とeviction/未知値を維持する。初回/世代変更後は503、
完成済み同context snapshotは取得時刻付きで返し、更新中でも音声を待たせない。
HTTPは4096-byte writeable callbackと最大8診断応答で有界にする。通常state/WSや他APIまで非同期化したものではない。

追加回帰は最大128件/各8 attempt detailと256 map regionの投影一致、map-only取得、capture時点、
1000重複要求の集約、世代変更・contextの往復時の旧結果拒否、生成例外/byte上限を扱う。
deadline付きfake出力は100 msの供給遅れをunderrunと判定し、意図的120 ms停止を検出するnegative controlを持つ。
同じthreadで実APIとengineを進め、JSON ownerを350 ms止めてもdeadline missがなく、slow HTTP受信側と
最大履歴JSONの分割送信中も音声が進むことを確認する。既存の無期限に受理するfake sinkだけの試験とは区別する。
UIは軽量endpoint、503時の有界retry、取得時点表示、旧stream返答拒否を検証する。
最終sourceのLinux/aarch64 DebugはAPI/metadata ON・WPE OFFのbuild/CTest 69/69、API/metadata OFF・WPE OFFの
build/CTest 41/41成功。追加publisher回帰はAPI ON/metadata OFFの別buildでもASan/UBSan（leak検出あり）とTSanを各実行し成功した。
deadline付きpublisher回帰は通常Debugで10回反復して成功した。UIは経過秒ではなくUTC capture時刻を表示し、
停止中に画面更新がなくても「数秒前」の表示が固定されて鮮度を誤認しない。
実行は既存`picdplayer-profile-review-build` imageのnetworkなしcontainerで、実device I/Oはない。
対象は`build-audio-check`/`build-audio-minimal`、sanitizerは`build-audio-asan`/`build-audio-tsan`の
`read_diagnostics_publisher`試験。続いて下記の標準WPE Debug packageを作成・配備した。
Piの修正版音切れ解消、聴感、長期再生、実電圧は未確認であり、
#121/#83/#184や#88の受入完了・Issue closeには使わない。

### 修正版Debugの配備確認

利用者の明示的な配備依頼により、同じworktreeから`PICDPLAYER_BUILD_TYPE=Debug`で標準
`scripts/build-container.sh`/`scripts/deploy.sh`を実行した。既存WPE SDKのrecipe一致を確認して再利用し、WebKitを再compileしていない。
標準WPE構成のCTestは70/70成功。配備直前はSTOPPED/IDLE、低電圧フラグ0。
旧packageと有効設定はprivateなtarget側directoryへ保全し、既存の証跡・backupを上書きしていない。

- package SHA256: `1bee80965622e593613791a6fbab0e1bce19cf4e56842a270ef44e462429430f`
- daemon SHA256: `1da554efcf299aa2cf1bfe2e68556f1df97c25fc10b2d9321bcdb3401b182044`
- Player JS SHA256: `9e93ecf51ae79d95ec7281c326d17d2dde106e1fe34e6e3f3efd3f8a9de62fc2`

target package/daemonとHTTP配信Player JSのhash一致、daemon/WPEのDebug identity、両service activeを確認した。
有効設定3ファイルのhash、起動引数、requested policy/artist設定、offset未指定、非参照backup metadataは配備前と一致。
配備後にSTOPPED/IDLEを確認しており、こちらから再生・seek・設定変更は行っていない。
Inspectorで新Playerのconnected、mapのUTC capture時刻表示、観測context1/PRESENTを確認した。
map endpointはhistoryなしの200、history endpointは初回/世代変更後503を経て200、同session/streamと履歴128件を確認した。

13:56:00.954 UTCのCEC playは利用者操作としてログに記録され、こちらから停止・再開していない。
読み取り確認時はPLAYING、queued_blocks=50、get_throttled=0x0。13:56:28 UTCまで取得したdaemonログには
配備後のunderrun記録がなく、baseline以降のkernelログにも追加entryはなかった。
これは短い観測範囲の記録であり、利用者の実機試験完了・長期安定性・聴感の合格とは扱わない。
13:57 UTCに利用者から「無事に再生できている」と報告を受けた。通常再生の利用者確認として記録し、
時間・操作列・背景ON時の負荷等が未指定のため長期試験や全条件の受入完了とはしない。
同時に背景ONの永続化を要望されたが、現行はdaemon session内の設定のみで、保存・復元は#177の残課題として扱う。

<a id="pr256-readonly-20261031"></a>
## PR #256 実機read-only再照合と休止点（2026-10-31追記）

利用者の再開指示に従い、2026-10-24 snapshotを現在状態と推定せず、GitHub/Git/Piを別々に照合した。
PR #256は最終head`ce1b7e0a2eb25938d78e447c187b9bb4e648c777`、2026-10-26のmerge
`a17e6e016d0eafa0598b41e45427d5037ffdebaa`で統合済みだった。後続の文書PR #262も統合済みで、
今回取得したlive masterは`6c871f67cd1b333bdfd84f19862d67b88c45300e`だった。
#238/#239はOpen。過去のDraft/未commit/未統合という記録を現在の実装状態へ読み替えない。

指定の保存worktreeは存在せずGit登録にもなかった。残存feature branch先端`779bfc5`と最終PR headの
差分はbacklog/verificationの2文書だけだったが、消失worktreeの未commit成果やprivate証跡の残存は保証しない。
このread-only観測単位では復元・checkout・merge/rebase・stash/reset、root checkoutや旧候補の変更は行っていない。
Mac更新後のSSH設定解析成功を確認し、利用者がconsole由来として提示したED25519指紋と
取得公開鍵の完全一致後、repository外の専用known-hostsへ保存して厳格照合で接続した。
秘密鍵・設定全文・全Environment/argv・private画像は公開していない。

### Gate Aの観測

2026-10-31 12:49:09〜12:50:23 UTC（日本時間21:49〜21:50）の有限read-only観測で、
daemon/kioskはactive/running・NRestarts0。PIDは51340/51369、実機時計の起動時刻は
10月27日07:07:15/16 UTCだった。WPE WebProcessも存在し、Chromium processは観測しなかった。
Pi上の保存package SHA-256は`c7e1e38b74b8ff9154c746557e6b71c9b3df494d04d3c2281e93e3e89cbb3a3a`、
配置daemonは`e176151f2a3c9dd97dec6b994ff88331548fcb5439eb5460646795254c49634a`、
installed profileはRelease/version0.1.0・package statusはinstalled/arm64だった。
稼働PIDの`/proc/.../exe`経由のhash取得はPermissionErrorで未確認であり、配置fileのhashと区別する。
これらを最終PR headのCI package配備の証明にはしない。

開始/終了の再生状態はSTOPPED/track1/position0・AUDIO_READY/14 track、offset null/NONE。
REPEAT要求/実効一致、region75/matches2/attempts3/time_budget10000ms、pending false。
captureはIDLE/job0・busy false/reader_released true/call_inflight false/waiting_for_access false・受理量0。
最初の標本はmedia RUNNING/media_busy、終了標本はIDLE/offset_unknownで、取得を予約していない。
背景の保存設定はconfigured/saved、source restoredを確認したが、今回背景ON/OFFを操作していない。

Custom UI指定は`/srv/picdplayer-ui-dev-20261020-aria`一組、live argvと設定の追加引数は一致した。
元backupとのprivate比較で、custom-ui一組以外のEXTRA_ARGS保持とoffset引数なしを確認した。
7 fileは通常file・symlinkなし・owner/group udonchan・0644、配置directory0755で、
全7 fileとHTTP応答のSHA-256は最終PR headのsourceと一致した。
HTMLは`22999923d7b0b6c454bfbab352a34cab47785a8101804648efe4cecc88899aad`、
JSは`4874953872f69b13ee47cf15767a404f59d4d87ed06f24c54267c2b71d24bb81`、
CSSは`3d4187843746ee739156c5a79c9af23f5e12c2e166fb75e79bbdd2237e5c6d64`だった。
manifest/statusの4 assetも一致した。初回は誤った`/player/assets/...`へ照会してHTTPエラーになったが、
正しい`/player/...`で200/hash一致を確認した。これは実機file修正ではない。
旧upload元のCSS/JSは配置先と不一致であり、再コピーに使用しない。

### Capabilityと履歴の限界

13:17:30 UTCの既存API読取では、能力6項目はUNKNOWN/source NONE、
`probe_error`は`open /dev/sr0: Permission denied`、node観測はPRESENT、playerはSTOPPEDだった。
probeを新たに発行せず、権限・group・service制限の原因までは確定していない。
未実装の能力評価経路も別に残るため、権限エラー解消だけを全能力完成としない。

利用者は最後の実機操作が10月23日深夜頃だったと記憶しており、10月26〜27日の保存記録と食い違った。
今回のPID/起動時刻/package/daemon/UIは後続記録と一致するが、それだけで過去の全域取得成功や
利用者の操作日を独立に証明しない。元の記録は消さず、今回のlive観測と区別して保持する。

### 休止と再開条件

今回のPOST start/cancel/play/stopは全て0回。deploy、restart、offset変更、直接probe/ioctl、
権限変更、Inspector有効化、TV/物理リモコン/音声試験は行わず、取得jobや一時実機設定を残していない。
終了時も同PID・NRestarts0・STOPPED・capture解放済み・offset未指定で、今回の復元操作は不要だった。
UI配置とHTTP反映は確認済みなので、その確認だけのための再配備/restartは不要。

利用者はCapabilityの全表示項目をv1必須とし、理由のUI説明量は別議論とした。
現在は文書/Issue監査と次の実装へ切り替える安全な休止点とする。
再開時はPi/利用者の利用可否とlive identity/状態を再取得し、UI代表受入とPCM取得を別承認で行う。
#252の物理入力受入を同runで参照し、過去の限定focus確認を全受入へ拡張しない。
PCM取得/取消後再生復帰・音声等の未確認は保持し、offset0・有限job・取消/解放/復元条件を事前合意する。
今回の文書追記は実装完了・Issue close・master統合を意味しない。

## #88 Device lifecycle（2026-10-17 JST）

基準masterは`5a8007703f3745fec4d099f0d786ac125439e3a5`。利用者承認は#88の契約整理・実装・自動検証・
必要なDebug配備/非破壊Pi確認・commit/push/CI/master統合までで、物理抜き差し/reset・provider/C/D・Issue closeを含まない。
[観測と失効](../design/integrity-design.md#device-lifecycle-observation)と[公開契約](../design/message-contract.md#device-lifecycle-observation)を追加し、
既存reader open counterと別の観測contextを使う。node/sysfsをread-onlyでowner threadがsampleし、
旧reader/probe/TOC/capability/diagnosticの失効・admission・再probeを実装する。
metadata観測不能をABSENT/NOにせず、same vendor/model/TOC/pathだけで物理同一と主張しない。

| 検証 | 結果 |
|---|---|
| native ASan/UBSan・TSan | device lifecycle各9ケース・59 checks成功。MediaWorker両sanitizer成功、engine ASan/UBSan成功 |
| Linux/aarch64 Debug API/metadata ON WPE OFF | 最終source再build後68/68成功 |
| 最小API/metadata OFF、WPE OFF | 最終source再build後41/41成功 |
| 標準WPE Debug（paranoia OFF） | 最終source再build後69/69成功 |
| Debug package lifecycle | disposable containerのinstall/upgrade/reinstall/purge成功 |
| Pi | 新Debugの機能assertとDOM確認は成功。ただし再生中のundervoltageを後解析で発見し、電源安定性/総合実機受入は未成立。追加負荷は停止 |

実機のsysfs観測ではblock nodeのmajor/minor/inode、resolved syspath、USB busnum/devnumがread-onlyで得られることを確認した。
daemon userはpicdplayer。旧journalと権限へ依存するreset検出は導入せず、物理同一性を証明する値として公開しない。
新sourceの配備は再build/自動回帰成功後だけに行う。
nativeはworker/observer/engineの各sourceを再compileして`-fsanitize=address,undefined`または別binaryの
`-fsanitize=thread`で実行した。Linuxは既存build imageの`build-device-check`、`build-device-minimal`を
DebugのAPI/metadata ONまたはOFF・WPE OFFでconfigure/buildし、各directoryのCTestを実行した。
標準構成は`PICDPLAYER_BUILD_TYPE=Debug PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build
PICDPLAYER_WPE_VOLUME=picdplayer-wpe221-build ./scripts/build-container.sh`と同imageのCTest、
同Debug/imageで`./scripts/test-package-lifecycle.sh`を実行した。既存SDK identityを照合して再利用し、WebKitは再compileしていない。

fake試験は観測状態/signatureの世代、安定UNKNOWN/ABSENT、遅延sample拒否、node/sysfs parser、
cap失効/新世代bind・C2降格・offset config保持、old in-flight read/queue/event拒否、
旧coordinator待ちfactory/controlの非発行、shutdown closeのowner/coordinator所有を含む。
既存UI fixtureのdrive capabilityでNOT_CHECKEDを使っていた箇所はKnowledgeのUNKNOWNへ修正し、
新contextのfixtureに旧cap/診断を持ち越さない。新公開fieldはserializerに実装してから試験へ追加した。
検出不能なreset/交換、ioctl中断、物理同一性、physical hotplug/reset・異常媒体・別drive・聴感は未確認として残す。
今回はC238/D239/Bのcapture runtime接続や外部照合を追加しない。

### 非破壊Pi確認と電源条件

現行EnvironmentFiles/DropInPathsとargsのSettings fileだけを参照し、設定3ファイルと旧Debug packageをprivate領域へ保全した。
非参照0600 backupは内容を読まずmetadataだけ記録した。中断後にもhash/args/requested policy/背景requested/package一致をread-onlyで確認した。
背景GETのrequested/effectiveとPOSTのenabledは別の契約であり、helperの初回誤参照を修正しただけでアプリは変更していない。
その初回partial backupは上書きせず同hashを確認して再利用した。

標準`PICDPLAYER_BUILD_TYPE=Debug PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build ./scripts/deploy.sh`で配備し、
両service activeとWPE主runtimeを確認した。package SHA-256は
`59701bdda6a95a916d5fd43744270de0bdfa07cd8d8ba9ee3e883e4aad822b35`、実行binaryは
`d8019bc9422b54d31d17c2a60c6f0f8376ed766d7cbdb2b884423c269c704c1e`でstagingと一致した。
旧packageは`4d2011c3342d2d8abdde4a325b0a28bf6148614b4aa3052bac348572a120c80f`を保持している。

現在の通常CDでAPI/WS、play、pause、+2秒seek、resume、stopの機能assertを実行した。
観測generation=1/PRESENT/INITIALのまま変わらず、revalidating/retiring_ioはfalse、disc generation=1、
read evidenceのreader-open counterは初回とresumeとも1だった。履歴23 regionのdisc世代一致とoffset UNKNOWN/nullを確認した。
callerの操作/stream変更をphysical観測counterの変化と混同していない。
Inspectorのread-only DOMでは1920x1080、connection connected、NODE PRESENT / observed context 1 /
physical identity unverifiedを確認した。PNGや実PCM fileは作成していない。

**ただし電源条件を満たす正常実機受入とは扱わない。** 後解析したkernel journalに
2026-10-17 12:26:26 UTCの`Undervoltage detected!`、12:26:32 UTCの`Voltage normalised`があった。
play開始12:26:21.989、pause12:26:28.177、resume12:26:28.569、stop12:26:29.590 UTCであり、警告は再生中だった。
元helperはUSB disconnect/reset/over-current等を監視したがunder-voltageを含めておらず、途中中止できていなかった。
元のresult.json（機能assert passed）とraw journalを保持し、analysis.jsonで電源安定性false/総合受入INCOMPLETEを補足した。
guardをunder-voltage等にも広げたが、電源/USB確認前の追加負荷・再配備・再試験は行っていない。
電圧のnormalisedだけで負荷時の安全を再確認したとは推定しない。USB再接続/resetやread failure/underrunはこのrunでは検出しなかった。
観測generation安定やPRESENTが電源healthの保証でないことも、この結果と区別する。

同runでmain_loop_stallはapi_service 73,644 usと62,529 usを観測した。API/WS/Inspectorを伴う短時間観測で、
原因や新旧比較はしておらず、#121/#83の解消・#184の長期安定性確認には使わない。
試験後はDebug・offset未指定・STOPPED/track1、元args/requested policy/背景requested、設定3hashと非参照backup metadata一致を確認した。
API8080/Inspector9223はloopback待受、両service active。12:26:33 UTC以降のread-only kernel照会に追加eventはなかった。
これは電源問題の原因確定・解消や聴感確認ではない。

証跡はローカルの`/var/folders/np/xlf6yym55bx3_tsbgtl5znhw0000gn/T/kilo/device88-hardware-Rsoah1`、
Piの`/tmp/picdplayer-device88-RzHlrI`へ非公開で保持する。config/旧package・raw result/journal/各JSONとDOM情報を含むためGit追加・公開しない。
`evidence/verify.py`は元run、rootのverify.pyはguard修正版であり、修正版の実負荷成功を主張しない。
追加の負荷/物理hotplug/reset試験には利用者の電源・USB経路の再確認と別承認が必要。#88と関連hardware Issueはcloseしない。

## #237 排他的取得worker（2026-10-17 JST）

基準masterは`5c266debe954efcbab53bb06dbf6ee6e8935b04d`。利用者からBだけの実装・自動検証・
commit/push/PR/master統合を追加承認された。#237本文の作成当時の未承認記録には最新scopeをcommentで追記し、
受入条件やOpen状態は変えていない。C/D/#88実装、providerアクセス、実機操作、Issue closeは対象外である。

[CompletePcmWorker](../../include/complete_pcm_worker.hpp)とgate付きfake試験をtest-only CMake対象として追加した。
raw factory/初期と最終TOC probe、offset 0/repeat、reader closeをowner threadとcoordinatorへ閉じ込め、
raw seek/read境界とconsumer前後にもadmission checkを置いた。
mainはcoreを同時操作せず、取消・条件変更・期限と、実際のI/O/cleanup終了・caller確定を分離する。
内部snapshotは実runtimeの開始条件を自動検出するものではなく、公開JSON・CLI/UIは変更しない。

| 検証 | 結果 |
|---|---|
| macOS clang ASan/UBSan再compileとfake試験 | 57ケース・367 checks成功、exit 0 |
| macOS clang ThreadSanitizer再compileとfake試験 | 57ケース・367 checks成功、exit 0。race reportなし |
| Linux/aarch64 Debug、API/metadata ON、WPE OFF | 再build後67/67成功 |
| Linux/aarch64 Debug、API/metadata OFF、WPE OFF | 最終source再build後40/40成功 |
| Linux/aarch64標準WPE Debug（paranoia OFF） | 最終source再build後68/68成功 |
| Debug package lifecycle | disposable containerのinstall/upgrade/reinstall/purge成功 |

native試験は`clang++ -std=c++20 -Wall -Wextra -Wpedantic -pthread -g -Iinclude`に、
`-fsanitize=address,undefined -fno-omit-frame-pointer`または別binaryの`-fsanitize=thread`を指定し、
worker/input/offset/TOC/integrity/repeat各sourceをcompileしてから実行した。
Linuxは既存build imageへsourceをmountし、`build-worker-check`をAPI/metadata ON・WPE OFFのDebugとして
configure/buildしCTestを実行した。A coreの既存回帰も含め、以前のbuild結果を代用していない。
最小構成は`build-worker-minimal`へ`-DCMAKE_BUILD_TYPE=Debug -DENABLE_API=OFF -DENABLE_METADATA=OFF
-DENABLE_WPE_KIOSK=OFF`でconfigureした。標準構成は`PICDPLAYER_BUILD_TYPE=Debug
PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build PICDPLAYER_WPE_VOLUME=picdplayer-wpe221-build
./scripts/build-container.sh`と同imageでのCTestを実行した。
SDKのruntime-image/RootFS identityをread-onlyで事前照合してcacheを再利用し、WebKit自体は再compileしていない。
同Debug/imageの`./scripts/test-package-lifecycle.sh`も成功した。
最終daemonの`nm -C`にCompletePcmWorker/CompletePcmInput symbolがなく、package inventoryにも
worker/core/test/headerが含まれないことをassertした（exit 0）。main/session/既存worker/verifier/JSON/UI/policy/core sourceの差分もなく、
runtimeや公開schemaを変更していないことを機械確認した。

fakeはfactory・seek・read・initial/final probe・consume/finalize・close/discardをgateで止め、
取消とpollが閉じたgateを待たず返ること、busy/release待ちを保持して新jobを拒否することをassertした。
正常single/repeatで全35 CD frame・track別canonical byteを照合し、thread IDと同時I/O数1、
coordinator待ち取消後のcallback非発行、次のphysical repeat試行の抑止、全identity変更・遅延旧read、
factory/probe/reader/consumer例外、partial/zero/wrong-position、TOC不一致、call/job期限・時計逆行/例外、
取消/confirmの両順序、解放前のdestructor待ち、prepared期限とreadiness失効を検証した。
各gateはtimeoutとscope releaseを持ち、実kernel ioctlの無期限待ちを試験したわけではない。
取消されたreadの後に同じ注入factoryで新jobを始めるcaseも検証した。fakeの初期/最終TOC区別を
globalな最初の1回ではなくreaderの生存状態へ合わせてから、全構成を再実行した。
これはfixtureの意味の修正であり、既存assertを弱めたり現物driveの同一性を確認したものではない。
fake consumerは`test-worker-byte-check`であり、実checksum algorithmやproviderは採用していない。

正規化PCMのcapacity/high-waterはsingle/region15で35,280 bytes、region75で176,400 bytesを確認し、
終了後capacity=0を確認した。core reservationとconsumerの宣言量、reader/library内部、stack、process RSSは別で、
すべてのheapを実測して8 MiB以下と保証する結果ではない。
destructor joinはcallが戻らなければ待つ。callerはI/Oとconsumerがquiescentになる前にmain loopで破棄しない必要がある。
factory/probeのopaque call内部は強制中断せず、Bが制御できる次の境界で止める。
今回のfake成功は物理driveの中断/解放、USB reset/同一path再接続、実CD取得、音声やTVの実機確認ではない。
daemonへのlink/公開登録・配備は行わず、C/D/#88に残るproduction有効性と混同しない。
設計/公開契約/backlogと全diffを照合し、追加ローカルMarkdownリンク/anchor 8件と`git diff --check`も成功した。

## #236 完全取得入力core（2026-10-16）

基準は#160設計PR #235統合後のmaster `1b656834259b0eab9f1fcbbf11fee6418cdb4667`。
利用者承認に従い後続4 Issueを作成し、A=#236/B=#237/C=#238/D=#239を#160のsub-issueへ設定した。
BはA、DはA/B/C/#88にblocked byを設定。今回のsource実装はAだけで、全IssueをOpenのまま保持する。

`CompletePcmInput`、job/profile/consumerの内部C++契約と`complete_pcm_input_test`を追加した。
連続prefixとtrack境界、S16LE、完全read/offset 0、consumer chunk/finalize、取消/世代/期限、
最終TOC・release申告の二段階gateを検証する。CMakeのdaemon targetは変更せず、新coreはtest-onlyでcompile/linkする。
profileはtest-byte-recorderというfakeだけで、実checksum algorithmの採用や外部通信はない。

| 検証 | 結果 |
|---|---|
| macOS Apple clang 15、ASan/UBSan、core再compile | 71ケース、82,707 checks成功、exit 0 |
| Linux/aarch64 Debug、API/metadata ON、WPE OFF | `build-core-check`で最終source再build後66/66成功 |
| Linux/aarch64 Debug、API/metadata OFF、WPE OFF | `build-core-minimal`で再build後39/39成功 |
| Linux/aarch64標準WPE Debug（paranoia OFF） | `build-container`で再build後67/67成功 |
| Debug Debian package lifecycle | disposable containerでinstall/upgrade/reinstall/purge成功 |

nativeの実行は`clang++ -std=c++20 -Wall -Wextra -Wpedantic -fsanitize=address,undefined
-fno-omit-frame-pointer -g -Iinclude tests/complete_pcm_input_test.cpp src/complete_pcm_input.cpp
src/disc_toc.cpp src/integrity_state.cpp src/repeated_read_policy.cpp src/repeated_read_verifier.cpp
-o build-core-native/complete_pcm_input_sanitized`後に同binaryを実行する。
Linuxは既存`picdplayer-profile-review-build`へsourceをmountし、各構成をconfigure/buildしてからCTestを実行する。
最小構成は`-DCMAKE_BUILD_TYPE=Debug -DENABLE_API=OFF -DENABLE_METADATA=OFF -DENABLE_WPE_KIOSK=OFF`、
API有効構成はAPI/metadataをONにした。両構成とも`cmake --build <dir> -j6`と
`ctest --test-dir <dir> --output-on-failure`が成功した。
標準構成は`PICDPLAYER_BUILD_TYPE=Debug PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build
PICDPLAYER_WPE_VOLUME=picdplayer-wpe221-build ./scripts/build-container.sh`と同imageでのCTestを実行した。
既存SDKのruntime-image identityをread-onlyで事前照合し、cacheを再利用してWebKit自体を再compileしていない。
packageは同Debug/imageで`./scripts/test-package-lifecycle.sh`を実行し成功した。
生成した`cdplayerd`を`nm -C`で照合してCompletePcmInput symbolがなく、
`dpkg-deb --contents`にもcore/test/headerが含まれないことをassertした（exit 0）。
既存main/session/worker/verifier/JSON/UI/policyのsource差分もexit 0で不変を確認した。

試験は全sample/channelとbyte order、track跨ぎ、非1始まりTOC、128件を超える小block列、
gap/duplicate/順序/partial/zero/error/過剰block、明示0/未知/正負offset、宣言容量、
consumer例外/結果超過/再入、単一予算・期限/時計逆行、全identity変更、final TOC/解放申告、
cancel/finalize競合を含む。fake PCMに実repeat wrapperを接続して、guard/overlapを二重計上しないことも検証した。
read予算のms丸めは切捨てとし、残り1ms未満を上向きに増やさない条件を追加してから全構成を再検証した。

開発中、実repeat wrapperをfake PCMへ接続した試験が先頭overlapの扱いで失敗した。
実契約はSTREAM_BOUNDARYでもrequested=15/compared=0であり、短い先行blockでは比較量が小さくなる。
core検証と新fakeをその意味へ合わせた。既存verifierや既存試験の意味・既定値は変更していない。
SINGLEにrepeat証拠が混在する場合も、固定したpolicyと異なるものとして拒否する。

memoryは宣言上限・reservation・core scratch/result境界を検証するもので、任意consumerの隠れたheapや
process RSS全体を8 MiBに抑える実測/保証ではない。manifest serializer、global slot、read watchdog、
reader解放の実測、production capture、provider adapterはB/C/Dに残る。
本作業では配備・実CD・試聴・TV・物理操作を実行しておらず、過去の#10実機結果を流用しない。
全diffと設計/公開契約/backlogを照合し、今回の実行結果を記録した。
追加したローカルMarkdownリンク/anchor 6件のassert検査と`git diff --check`も成功した。

## #160 完全取得PCMの設計整理（2026-10-16）

基準はmaster `2e821685eb9ed0fc2c9ba84586d1e2e5dae64573`。#160はOpen、Hard dependencyなしの設計Issueとして
最新本文と#13を照合した。#13はClosedだが、PR #161は文書2ファイルだけでprovider/capture実装を含まず、
PR本文も#13をcloseしないと記す。timelineは直後のclose操作を示すだけで、機能実装を証明しない。
Closedと残条件の不一致を明示し、再openやprovider実装は行っていない。

最初の承認範囲は、[完全取得入力](../design/integrity-design.md#complete-pcm-input)の設計整理である。
PCM非保存incremental方式、明示開始・通常再生と同時取得なし、全audio coverage、canonical byte列、
offset端不足の開始拒否、取消と物理releaseの区別、memory/deadline上限、再起動時の破棄、4実装単位を記録した。
新しい公開field/enumを追加せず、[現行公開契約との境界](../design/message-contract.md#complete-pcm-input)を補足した。
8 MiB/3時間等は未実装の初期設計上限であり、現在のdaemonの動作・実測値ではない。

| 照合した根拠 | 設計との対応 |
|---|---|
| `include/pcm_worker.hpp`、`src/pcm_worker.cpp` | bounded再生queue/128件履歴とSTREAM世代は全disc PCMの保持・完全性証明ではない |
| `include/drive_access.hpp`、詳細設計のreader/engine | mutexは公平性・ioctl中断を提供しない。main ownerによる発行抑止とreader解放確認が別途必要 |
| `include/disc_toc.hpp`、`src/disc_toc.cpp` | audio-only、非負で増加するLBA、非1始まりtrackを許す。全域は最初のaudio開始からleadoutまで |
| `include/read_offset.hpp`、offset設計/試験 | 非0 offsetの不能/整列除外を全域取得へ昇格しない。未指定を0と推定しない |
| `src/cdda_probe.cpp`、`src/pcm_file.cpp` | 既存診断は最大750 CD frameの部分PCM保存。full-discのjob/世代/consumer契約ではない。S16_LEの明示byte化はcanonical検討の根拠 |
| #156の採用判断・read policy | 未解決時はSTOP、代替PCMなし。完全readのUNCERTAIN分類と、不完全/非採用を区別 |

変更はintegrity-design、message-contract、backlog、本記録の4文書だけ。source・tests・fixture・
CLI/API・build設定・実機設定は変更していない。Node.jsのassertでcanonical byte順、連続prefix、
gap/重複/順序/partial/zero/範囲外の拒否例、offset可否、byte数、memory/deadline換算を19ケース検算して成功した。
これは設計のworksheetであり、capture/consumer実装の単体試験ではない。
追加ローカルMarkdownリンク/anchor 6件、全diff、`git diff --check`を検査し成功（exit 0）。
`git diff --exit-code -- src include tests ui scripts CMakeLists.txt`もexit 0で実装不変を確認した。
今回はbuild/CTest、capture/checksum実行、通常起動のfull-disc read、ripping、配備・物理操作は未実施。
既存の#10/#156の検証を、完全取得機能や外部照合の検証に流用しない。
設計整理後、文書のcommit/push/master統合、後続A/B/C/DのIssue作成と、Aだけの実装・自動検証・統合を
利用者から承認された。実Issueは文書統合後に作成し、B/C/D実装、providerアクセス、実機操作、Issue closeは行わない。

## #156 unresolved PCMの設計判断（2026-10-16）

基準はmaster `554055d0a02d7ab77d26d211e42c7a74a8ec81aa`。#156はOpen、Hard dependencyなしの
設計Issueとして本文・コメント・関連#11とPR #157を照合した。#11のコメントはPR #154でSTOPを確定し、
残る採否判断を#156へ移管したことを示す。PR #157自体はbacklogだけの変更であり、追加fallbackの実装ではない。

利用者はSTOPを継続し、BEST_AVAILABLE/SILENCE/WAIT_WITH_BUDGETを現時点では採用しない方針を承認した。
[設計判断](../design/integrity-design.md#unresolved-pcm-policy)に候補比較、由来、有限予算・最終STOP、
見送り理由、再検討条件、将来の独立実装と試験条件を記録した。
[message contract](../design/message-contract.md#unresolved-pcm-policy)は既存公開fieldの意味を補足するだけで、
新field・enum・event・選択肢を追加していない。候補を採用しないため実装Issueの作成も行っていない。

照合した実装・試験は次のとおり。これは今回これらの試験を実行したという意味ではない。

| 根拠 | 確認した境界 |
|---|---|
| `include/read_policy.hpp`、`src/daemon_snapshot_json.cpp` | 現行policyのfieldにunresolved_data_policy/sample_originがない。single/repeatと既存予算を維持 |
| `src/pcm_worker.cpp`、`tests/integrity_fixture_test.cpp` | 完全readのqueue採用、失敗時のqueue破棄、旧stream PCM/STREAM観測の拒否を確認。同discのDISC観測は別scope |
| `src/repeated_read_verifier.cpp`、`tests/repeated_read_verifier_test.cpp` | 一致・overlap条件を満たさない候補を呼び手bufferへコピーせず、試行・時間予算で打ち切る |
| `src/integrity_state.cpp` | 完全性と根拠分類は別。UNCERTAINとSTOPを同義にせず、実際の採用条件を変えない |
| `src/playback_engine.cpp`、`tests/playback_engine_test.cpp` | STOP・read stall・ALSA underrun復旧・正常drainの境界を分離。kernel ioctlの強制中断は保証しない |

変更は設計・message contract・backlog・本記録の4文書だけで、source・test・fixture・build設定・
公開schema・実機設定は変更していない。今回のbuild/CTest・Pi配備・再生・物理操作は未実施。
既存の#10等の試験結果を、未採用候補の自動試験・実機確認へ流用しない。
文書と実装/既存試験を照合し、全diffを確認した。追加したローカルMarkdownリンク/anchorの8件を
Node.jsのassertで検査して成功（exit 0）、`git diff --check`も成功した。
`git diff --exit-code -- src include tests ui scripts CMakeLists.txt`はexit 0で、実装・試験・toolingの不変を確認した。
将来の採用判断には別承認と実装・自動/実機試験が必要。文書整理後、利用者からcommit・push・master統合を
別途承認された。#156のIssue状態変更や追加の実機操作は行わない。

## #10 read offsetの実装と限定実機確認（2026-10-16）

基準はmaster `86d1054a31178ef0967f3a32a9fee52f2fa86fb9`。専用branch `docs/10-offset-contract`の
作業差分として実装した。#162はPR #161による入力契約の文書化でClosedとなったが、補正コードは含まない。
今回、仕様整理、Dと追加wire契約の採用、実装・自動検証・Debug配備・限定実機試験を順に承認して進めた。
[採用仕様](../design/integrity-design.md#10の採用仕様と実装)と[公開契約](../design/message-contract.md#read-offset)を参照する。
Issue #10の状態は変更していない。過去のprofile試験を今回のsourceの検証には代用していない。

### 実装と自動検証

`--player`限定の起動時明示値、signed stereo sample frameで`logical p <- physical p + o`となるreader、
完全CD frameに内側整列したwindow、不能区間と整列除外、source/detailと物理/論理read根拠、
既存2列構図の最小表示を実装した。未指定は従来経路とUNKNOWN/nullを維持し、0も明示値として扱う。
失敗・不完全PCMの代替出力、外部database、外部照合、full-disc capture、hotplug検出は追加していない。

| 構成/検証 | 実行場所 | 結果 |
|---|---|---|
| Debug、API/metadata ON、WPE OFF | `build-offset-check`、Linux/aarch64 Docker | 65/65 |
| Debug、API/metadata OFF、WPE OFF | `build-offset-minimal`、同Docker | 38/38 |
| 標準Debug、API/metadata/WPE ON | `build-container`、同Docker | 66/66 |
| Debug package lifecycle | disposable同Docker | install/upgrade/reinstall/purge成功 |
| offset専用試験 | 上記CTest内の`read_offset` | 正負1/587/588/589/5000と0、開始LBA 0/150、全sample/channel比較、短いdisc、partial/wrong-start非採用、repeat overlap、終端drain |
| 世代と公開/操作 | integrity_fixture/controller/model/JSON/CLI/player_daemon/UI試験 | 遅延旧read・再open・disc世代、window失効、操作拒否、32-bit境界/不正値、source、8 attempt/128 history容量、DOM差分更新を検証 |

標準buildは`PICDPLAYER_BUILD_TYPE=Debug PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build
PICDPLAYER_WPE_VOLUME=picdplayer-wpe221-build ./scripts/build-container.sh`で実施した。
既存SDKのimage identityを照合して再利用し、WebKit自体を再buildしていない。
CTestは同imageへsourceをmountし`ctest --test-dir build-container --output-on-failure`を実行した。
最小構成は`cmake -S . -B build-offset-minimal -DCMAKE_BUILD_TYPE=Debug -DENABLE_API=OFF
-DENABLE_METADATA=OFF -DENABLE_WPE_KIOSK=OFF`、`cmake --build ... -j6`、同directoryのCTestである。
packageは`PICDPLAYER_BUILD_TYPE=Debug PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build
./scripts/test-package-lifecycle.sh`で確認した。各最終commandはexit 0。

標準構成の初回CTestでは既存`metadata_worker`が単発失敗した。同じbinaryで
`ctest --test-dir build-container -R '^metadata_worker$' --repeat until-fail:10 --output-on-failure`は10回成功し、
その後の全件再実行は66/66成功した。原因は未解明であり、今回修正済みとは扱わない。
初期buildの型不整合とUIの不要DOM更新は修正後に再build・回帰した。
仕様式のNode assert検算211,779ケースも成功したが、これは実装reader試験とは別の算術検算である。

### Piの限定試験と復帰

標準`PICDPLAYER_BUILD_TYPE=Debug ./scripts/deploy.sh`で配備し、daemon/kiosk activeとWPE実processを確認した。
対象は`/dev/sr0`、drive reportはASUS SDRW-08D2S-U / F601、direct reader、既存通常CD（14 track、
LBA範囲`[0,242334)`）、既存HDMI ALSA device `plughw:CARD=vc4hdmi,DEV=0`である。
vendor/modelは今回の観測対象の記述であり、物理同一性や校正値の証明ではない。

serviceを一時停止し、SSH利用者の診断daemonへoffsetを起動引数で渡した。
CECとmetadata外部取得は無効、設定保存先は指定せず、既存EnvironmentFile/drop-in/Settingsは変更していない。
各条件でRESTとWSのwindow一致、通常短時間read/current根拠、pause・両端seek、旧current根拠の失効、
終端151 CD frameの連続coverage、2秒強の出力時間とSTOPPED復帰を検査した。
終端streamのplay後はcontrollerへ明示stopせず、既存drain経路で停止したことを状態・時間・エラー不在と照合した。
drain成功専用ログは存在しないため、そのようなログを成功根拠には使っていない。

| 起動値 / mode | 再生window（LBA半開区間） | 終端read region数 | 終端再生からSTOPPEDまで |
|---|---|---|---|
| 未指定 / single | `[0,242334)`、offset根拠UNKNOWN/null | 11 | 2.629 s |
| 0 / single | `[0,242334)`、除外なし | 11 | 2.869 s |
| +1 / single | `[0,242333)`、末尾不能1 + 整列除外587 sample frame | 11 | 2.794 s |
| -1 / single | `[1,242334)`、先頭不能1 + 整列除外587 sample frame | 11 | 2.804 s |
| +1 / repeat | `[0,242333)`、上と同じ除外 | 3 | 3.022 s |
| -1 / repeat | `[1,242334)`、上と同じ除外 | 3 | 2.805 s |

この表は最初のpackage（SHA-256 `406a2acc643e01e8dd6036ea92730494cc1c35f14c99c94264b732a7d23fc4ae`）の結果であり、
6条件とも成功した。明示値の採用evidenceはCORRECTED/USER_CONFIGURED、singleの実物理readは論理要求より
1 sector多い（±1の場合）、repeatは試行の物理/論理座標・2回以上の一致を確認した。
各終端streamの採用read数の和は151 CD frame、最後のreadがwindow終端で終わり、除外区間を採用していない。
read failure、underrun、stall、出力write/reset/drain例外は当該診断ログに認めなかった。
この結果は音声の試聴やbit一致の実測ではない。未指定と0で読み取り根拠が区別できることも確認した。

最終確認で、空windowの1-track discに対するNextも拒否するようcontrollerと単体試験を修正した。
その後sourceを3構成で再buildし、65/65・38/38・66/66とpackage lifecycleがすべて成功した。
この最終packageを再配備し、同じ実機条件を最初から再試験したが、結果は上の成功表と区別する。

| 最終packageの試験（利用者確認前の停止履歴） | 結果 |
|---|---|
| 既定開始buffer 45、未指定 / single | 成功。終端11 region、3.711 sでSTOPPED |
| 既定開始buffer 45、0 / single | 成功。終端11 region、3.108 sでSTOPPED |
| 既定開始buffer 45、+1 / single | 通常再生・pause/seek後、端でALSA underrun復旧1回。stream変更により151 frameの同stream完走assertが停止 |
| 診断開始buffer 150、未指定 / single | 端でread error（errno 5）、出力提出前にfail-closed STOP。その後USB消失と再列挙を観測 |
| 最終packageの-1 / single、±1 / repeat | このrunは上記異常で中断。後述の再確認結果と区別する |

既定45の+1試験では、2026-10-16 07:55:50 UTCにALSA delayのunderrunが発生し、
提出済み70560 stereo sample frameからLBA 242302で復旧した。read blockの実時間が供給余裕を超えた可能性はあるが、
原因を確定せず、既定bufferでの安定性を確認済みとは扱わない。
余裕を分離する比較として、設定を書き換えず診断daemonへ`--startup-buffer-frames 150`を追加した。
この比較の最初の未指定offset試験で、07:58:08 UTCにCDDA read errorが発生した。
kernel journalには07:58:07 UTCの`usb 1-1-port2: over-current change #5`、
07:58:08 UTCの光学drive側`usb 1-1.5: USB disconnect`、07:58:12〜13 UTCの再接続と
`Power-on or device reset occurred`が記録された。過電流イベントとdrive消失の直接の因果関係は断定しない。
電源・USB経路の確認なしで再読出しを続けず、以降の試験を停止した。
この観測はoffset未指定でも発生しており、offset補正の欠陥とも実機安定性の問題解消とも断定しない。
#88の物理hotplug/reset検出や#146の異常条件評価を、この試験で実装・完了したとは扱わない。

### 利用者確認後の最終package再試験

利用者が電源・USB経路を確認済みと回答し、限定試験の再開を承認した後、同じ最終Debug packageと
同じ通常CDで6条件を最初から実行した。binary/package hashは上記最終buildから変更していない。
開始bufferは診断比較の150ではなく、通常serviceの既定値と同じ45 CD frameを明示した。
CEC/metadata外部取得は無効、設定保存先なし、その他の試験条件・採用判定は前回と同じである。

各waitではdaemonログのread error/underrunを監視し、kernelは開始cursor以降を約1秒間隔で観測した。
新規over-current/USB disconnect/reset、read errorの検出でrunを中止しfinally復帰する。
今回はその中止条件に達せず、全6条件のPCM根拠・REST/WS・pause/seek・終端coverage・STOPPEDを確認した。
各終端streamは151 CD frameを連続採用し、singleは11 region、repeatは3 regionだった。

| 起動値 / mode、開始buffer 45 | 終端read region数 | 終端再生からSTOPPEDまで | 結果 |
|---|---|---|---|
| 未指定 / single | 11 | 2.619 s | 成功、offset UNKNOWN/null |
| 0 / single | 11 | 2.813 s | 成功、USER_CONFIGURED/CORRECTED、除外なし |
| +1 / single | 11 | 2.942 s | 成功、末尾不能1 + 整列除外587 sample frame |
| -1 / single | 11 | 2.927 s | 成功、先頭不能1 + 整列除外587 sample frame |
| +1 / repeat | 3 | 2.881 s | 成功、物理/論理試行範囲・一致根拠を確認 |
| -1 / repeat | 3 | 2.870 s | 成功、物理/論理試行範囲・一致根拠を確認 |

`kernel-during-test.json`は空配列で、新しいkernelイベントを観測しなかった。
復帰後まで同じ開始cursorから照会し、`kernel-post-restore.json`にも新規USB/電源異常がないことを確認した。
daemonログにもread failure、underrun、stall、出力例外を認めなかった。
再試験前後でアプリsourceと配備binaryは変更せず、再試験後のrepository更新は検証文書だけである。
これは短時間の再確認であり、以前の過電流・切断・underrunの原因を特定・解消したという保証ではない。
利用者による具体的な物理変更の内容は記録しておらず、今回の成功から推測しない。

試験helperの初期試行はbody付きPOST、read-historyのroot shape、非同期snapshot、track相対位置、
存在しない成功ログへの期待で停止した。いずれもhelperを現行契約へ修正し、アプリ側の受入条件は緩めず、
最後の6条件を最初から通して実行した。失敗時もfinallyでserviceを再開し、設定hashとoffset未指定を確認した。

保全preflightの当初globは非参照の0600 backupまで含めたため停止した。systemdのEnvironmentFilesと
DropInPathsを読み取り専用で照合し、現行3ファイルだけを保全した。参照されない
`/etc/default/picdplayer.fanart-poc-backup-20261014T124854Z`は内容を読まず、metadata不変を確認した。
権限変更・sudo読み取り・設定書換えは行っていない。
旧packageと設定・証跡は新規の保護領域に保持した。ローカルは
`/var/folders/np/xlf6yym55bx3_tsbgtl5znhw0000gn/T/kilo/offset10-hardware-JJvSzU`、
Pi側は`/tmp/picdplayer-offset10-DxVDQc`。非公開設定を含むためGit追加・公開・添付しない。
`verify.py`と、初回6条件成功の`evidence/`、最終packageの失敗を含む`evidence-final-default45/`、
buffer比較を含む`evidence-final-prefill150/`のJSON・daemonログを保全した。
同directoryの`kernel-observation.json`には上記UTC区間のkernel journalを保持した（非公開）。
利用者確認後の再試験はPiの`/tmp/picdplayer-offset10-recheck-GKJeZV`と、ローカルの
`evidence-recheck-power-confirmed45/`へ独立して保全した。baseline.jsonは既存保全記録のコピーであり、
既存の失敗証跡・旧package・設定backupを上書きしていない。
各directoryのresults.jsonはそのrunで最後まで成功した条件だけであり、残った別条件の旧ファイルを
そのrunの成功証拠にしない。最終buffer比較runのresults.jsonは空である。

最終配備packageのSHA-256は`4d2011c3342d2d8abdde4a325b0a28bf6148614b4aa3052bac348572a120c80f`で
Pi側test-build.jsonと一致。実行binaryのSHA-256は`6f5997b0fc7e202d576cb7aba43ae5afe2ac8ad930a5b64e96ae363e74f98b03`。
旧Debug packageは`f6f29bb5c3d49b8972cf1160a03cb0638a3e70d1d0cb3c3120f09b2684dbae72`で保全した。
最終状態は新Debug、offset未指定、STOPPED/track 1、両service active、WPE主runtime、
API 127.0.0.1:8080とInspector 127.0.0.1:9223待受を最初の復帰時に確認した。
失敗後と利用者確認後の成功runの両方でfinallyによりserviceを再開し、元のdaemon引数、
現行設定3ファイルのhash、非参照backupのmetadata不変、Debug・offset未指定・STOPPEDを確認した。
開始bufferは通常serviceの既定45に戻っている。成功run後もAPI8080/Inspector9223のloopback待受を確認した。

過電流/消失が再発した場合は再生を止め、利用者の物理系確認なしに自動retryや抜き差しを行わない。
未確認: 物理校正値の正しさ、原盤/外部PCMとの一致、音声試聴、TVの実表示・可読性、物理CEC、
disc入替/自動再生、別drive/異常媒体、paranoia実機、全disc完走・cold boot・長期再生。
利用者の試聴では通常CDの先頭・seek後・末尾の音切れ/異音とTV上の端除外表示を確認する必要がある。
物理校正にはdrive/firmwareに対応した独立の根拠が必要で、試験値±1を保存値として採用しない。
これらを残したまま#10や#146/#184/#164をcloseしない。
実装・検証完了後、利用者からcommit・push・master統合を別途承認された。Issueの状態変更は行わない。

## Debug/Release profile分離（2026-10-15）

開始commitは`3194660f007e9df8da6027b8d7fe74bec47e910e`。作業branchでapplicationの
build type、Inspector既定、daemon log filterと運用NOTICE、package識別・deploy事前検査を実装した。
対応表とoverrideは[build](../manual/build.md#application-profile)・[操作](../manual/operations.md#診断profileとoverride)参照。
WPE SDKのRelease recipe/cacheは変更せず、既存`picdplayer-wpe221-build`のstageとimage identity一致を確認して
標準export scriptで再利用した。WebKitのconfigure/build/stageは再実行していない。

初回実装は標準Linux/aarch64 containerでDebug/Release各65/65とpackage lifecycleを記録した。
その後の単独レビューで、生成wrapper試験が未定義CMake変数によりtemplate試験へfallbackしていた点、
multi-config試験が実generatorを使わず前の不正cacheを保持していた点、archive identity検査不足を修正した。
以下は修正後にsourceを再buildして取得した結果であり、初回記録を代用していない。

元workspaceのstage/package/candidateを保持するため、sourceと既存SDKを新規一時directoryへコピーした。
検証場所は`/var/folders/np/xlf6yym55bx3_tsbgtl5znhw0000gn/T/kilo/profile-review-8c8bQy`。
Ninja追加後のrepository Dockerfileから`docker build -t picdplayer-profile-review-build .`（exit 0）で
Linux/aarch64 imageを作成した。image IDは`sha256:e8373978b57232d61c7d3bb56ffc77a8a5a039908c94a7f13a298644472cb2a2`。
このimage名を`PICDPLAYER_BUILD_IMAGE`へ指定し、同じtreeをRelease → Debug → Release →
RelWithDebInfo → MinSizeRel → Releaseの順に切り替えた。各回の実commandは以下で、全てexit 0。

```sh
PICDPLAYER_BUILD_TYPE="$profile" bash scripts/build-container.sh
docker run --rm --network none -e PYTHONDONTWRITEBYTECODE=1 -v "$PWD:/src" -w /src \
  picdplayer-profile-review-build ctest --test-dir build-container --output-on-failure
PICDPLAYER_BUILD_TYPE="$profile" bash scripts/test-package-lifecycle.sh
```

`review-matrix.sh`に実行順と最小構成のconfigure/build/test command、`review-results.txt`にexit code、
`review-{1..6}-*-{build,tests,package}.log`に各実行結果を保持する。buildは既存SDKを再利用し、WebKitは再compileしていない。

| 対象 | 観測結果 |
|---|---|
| Debug | build/stage/CPack成功、CTest 65/65、package install・再install・purge成功 |
| Release | 同じbuild treeのDebugから切替、build/stage/CPack成功、CTest 65/65、package lifecycle成功 |
| RelWithDebInfo / MinSizeRel | application全体build/stage/CPack成功、各CTest 65/65、package lifecycle成功。診断profileはRelease |
| profile mapping | fresh tree未指定・空をReleaseへ確定、4 build typeと双方向切替、不正値（`OFF`・`0`含む）は独立fixtureで検査。実Ninja Multi-Configの拒否理由を確認 |
| log | 実global loggerのprofile既定・override、invalid/empty値の非転載、NOTICE/WARN/ERROR保持、無効levelのformatting抑制、drop除外、優先eviction、既存92件drop・sink障害を試験 |
| Inspector | 実launcherの`--diagnostics`で既定・on/off・既存HTTP override・空値・非loopback・不正port・別protocol拒否を試験。実portのlisten確認ではない |
| kiosk wrapper | CMake生成wrapperの既定と4 build typeのChromium on/off、既存WPE/fallback/restart marker、TCP待受・timeout試験成功 |
| package/deploy | marker全文/versionとdaemon・WPE・wrapperを共通validatorで照合。旧形式・期待値違い・validator失敗の転送前拒否、install後不整合、失敗時の旧package無効化をmockで試験。実機deployではない |
| Release candidate | 隔離コピーにGit metadataがないため、networkなしcontainerで`python3 scripts/release-artifact.py --commit 3194660f007e9df8da6027b8d7fe74bec47e910e --ref refs/heads/feature/debug-release-profiles --dirty`を実行（exit 0）。stage/payload/mode/markerと実binaryが一致。manifestは`Release`、`source_dirty=true`、`publication=not-approved`、`hardware_verified=false` |

INFO出力で故障注入・readinessを確認する既存試験はCTestから明示的に`PICDPLAYER_LOG_LEVEL=info`を指定し、
元の試験経路を維持した。Release既定の抑制は別のdaemon/global logger試験で確認する。
同じSDKを使用するRelease/Debugのbuild/test/package lifecycleをCIにも追加した。
PR #232の初回CI（run `38693489296`）はRelease検証後、Debug切替前のCPack作業directory削除で
permission deniedとなった。Linux host上でcontainerのroot所有出力をrunnerが削除できないことが原因で、
macOS Dockerのbind mountでの成功では検出できなかった。build/install/CPackを呼出元UID/GIDで実行する
修正と引数の回帰assertを追加した。sudoや既存成果物の強制削除は行っていない。
修正commit `8a8480374f8d4d4f11074eb110b3a16bcbe222d1`の
[CI run 38694667757](https://github.com/udonchan/picdplayer/actions/runs/38694667757)は
2026-10-15 06:55 UTCに6分27秒で成功した。Release/Debug各65/65、両package lifecycle、
Release candidate検査・保存の成功と両buildのSDK再利用を実logで確認した。
これはLinux runner上のprofile切替の検証であり、Piの実機確認ではない。
修正後も隔離コピーの`bash review-uid.sh`でDebug/Releaseを再buildし、各CTest 65/65とpackage lifecycleは
exit 0（`review-uid-{Debug,Release}-{build,tests,package}.log`）。先行するinline検証driverは変数展開により
Docker image引数が空になって`invalid reference format`で停止したため、script化して再実行した。
これはapplication試験の失敗ではなく、再実行前の試験成功を主張しない。
Release最小構成（metadata/API/systemd/WPEをOFF）もconfigure/build成功、CTest 37/37（exit 0）。
初回の36/37失敗は`player_daemon`がAPI無効でも`--api-port`を渡すfixtureの問題だった。
`git show 3194660:tests/player_daemon.py`で同じ経路を確認し、今回の回帰ではないと判定した。
API flagを試験へ渡してAPI専用部分だけを条件化し、daemonのstdin EOF無視・SIGTERM終了・interactive終了は
API無効でも検証する。試験全体のskip/削除は行っていない。
package単体試験26件は実deb内のshell fixtureを使い、正常4 profile・marker欠損/矛盾/重複・
stageと一致する古いbinary/wrapperの拒否を確認した。shell fixtureと実application binaryの照合は別の証拠である。
実binaryの混在は別treeのNinjaでDebugをbuildし、Releaseへconfigureだけ切り替えてCPackすると再現した。
共通validatorはこの実archiveをexit 1で拒否した（`review-mixed-ninja.log`、検証driverはexit 0）。
さらに同じ実archiveをread-only mountしたnetworkなしcontainerからvalidatorを呼び、例外理由が
`package binary build profile mismatch`であることをassertしてexit 0を確認した。単なる起動失敗を成功扱いにしていない。
先にUnix Makefilesで同じ操作を試した際はCPackのpreinstallが再buildし混在せず、validatorが正常受理した。
この初回再現driverは「拒否される」という仮定と異なりexit 1だった（`review-mixed.log`）。
標準build scriptの正常経路で混在したという意味ではなく、レビュー時の発生条件をgenerator別に訂正する。
従来sourceのmissing-field-initializers warningは残り、今回対象外の修正は行っていない。

上記ローカル/CI検証時点では実機操作は行っていなかった。後続の承認済み実機確認は次節に分離する。

### 実機DebugからReleaseへの切替（2026-10-15 UTC）

利用者が`picdplayer-pi`へのDebug→Release deploy、再生中断、稼働中daemon/kioskの再起動を明示承認した。
sourceは`git archive 3aa4a48c7cffda8f00a6a6b1104a3d9a6c945e97`から新規隔離directoryへ展開し、
元workspaceのstage/package/candidateは保持した。検証場所は
`/var/folders/np/xlf6yym55bx3_tsbgtl5znhw0000gn/T/kilo/profile-hardware-y1rFx4`。
`PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build`で標準build script、CTest、package lifecycleを
Debug/Release各回に実行し、両build/stage/CPack・各65/65・lifecycleは全てexit 0。
実commandは同directoryの`build-verify.sh`、logは`hardware-{Debug,Release}-{build,tests,package}.log`。

接続先はhostname `raspberrypi`、aarch64。開始時両serviceはactive、kiosk MainPIDはWPE、
playerはdisc track 1のSTOPPEDで、背景はsession ONだった。既存remote packageはMacの隔離directoryへ
`predeploy-package.deb`として保全してから、標準deploy scriptの固定remote packageを更新した。
各profileの実deploy commandは以下で、どちらもexit 0。両serviceはactive→active、
実install marker全文・両binary identity検査とMainPIDのWPE確認に成功した。

```sh
PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build PICDPLAYER_BUILD_TYPE=Debug \
  PICDPLAYER_TARGET=picdplayer-pi bash scripts/deploy.sh
PICDPLAYER_BUILD_IMAGE=picdplayer-profile-review-build PICDPLAYER_BUILD_TYPE=Release \
  PICDPLAYER_TARGET=picdplayer-pi bash scripts/deploy.sh
```

| 実機観測 | 結果 |
|---|---|
| Debug | marker/binaryがDebug。`ss -ltnH`で`127.0.0.1:9223`のみInspector待受。daemon current invocationのjournalはNOTICE 4・INFO 16件 |
| Debug Inspector | 専用loopback SSH転送を通じて`inspect-wpe.py --port 39223 --target-url http://127.0.0.1:8080/player --screenshot PATH`がexit 0。title PiCDPlayer、ready complete、connection connected、1920×1080 PNG 362512 bytes取得 |
| Release | marker/binaryがRelease。APIは`127.0.0.1:8080`待受、9222/9223待受なし。稼働中WPEはMainPID 110101、daemonは110076 |
| Release Inspector拒否 | 同じ専用転送経由のInspector toolはexit 1（connection reset）、SSH側はremote connection refused。直接のport一覧でも待受なしを確認 |
| Release log | current invocationのdaemon journalはNOTICE 4件、DEBUG/INFO 0件。起動profile、API待受、CEC opened・existing registration reusedを固定event抽出で確認。WARN/ERRORの新規障害注入は行っていない |
| API/WS | Debug/ReleaseともGET state成功、STOPPED・track 1。Releaseの`/api/events`は101 handshake/accept検証と5104 bytesのJSON snapshot受信成功、read objectあり。metadataの新規lookup成功を意味しない |
| 背景 | restart後は既存仕様どおりsession既定OFF。requested/effective=false、available=trueを両profileで確認。keyは取得・表示せず、背景ONへの変更は行わなかった |

PNGは隔離directoryの`debug-viewport.png`（0600）だけに保持し、Git/PR/配布物へ追加していない。
snapshotはWebKitのrendered pageであり、TV/DRM scanout・物理表示を確認した証拠ではない。
確認可能な既存EnvironmentFile/drop-inからInspector/log levelだけを抽出し、明示overrideは検出しなかった。
`/proc/PID/environ`は権限不足で読めず昇格していない。全環境・key・raw journalを成果物へ保存していない。
kiosk invocationで読めたjournalは1件だけでprofile行を取得できなかったため、実port/Inspectorとinstalled identityを
観測根拠とし、kiosk logの全量確認済みとは扱わない。
最初のSSH転送は既存ControlMasterへ追加されてprocessが終了したため専用ControlPathなしの転送へ切り替え、
最後に今回追加した29223転送だけを`ssh -O cancel`で解除し、専用39223のtracked processも停止した。
既存SSH masterや設定は終了・削除していない。既存drop-in/EnvironmentFileは一切変更していない。

このprofile比較を終えた時点ではReleaseで両service active、WPE主runtime、Inspector待受なしだった。
その後、利用者指示でPR #232 headのDebugを再配備している。さらに上記#10試験後は新Debug・offset未指定に復帰した。
以下はprofile比較そのものの未検証範囲であり、後続試験と区別する。音声試聴、実CD読出し・再生、
物理CEC入力、metadata再取得、背景ONの実表示、cold boot、長期再生は今回未検証であり、#219はOpenを維持する。

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
このスクリーンショットCLI変更時点ではDebug/Release profile分離を延期し、
build profile・logger既定・Inspectorの標準起動設定は変更していなかった。後続実装は上節を参照する。
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
| #226 | 当時Open | 当時は待機契約不一致が未修正で実機遅延再現も未実施。後続の修正・自動試験・実機45.840秒待機は冒頭の2026-10-18記録を参照 |

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
