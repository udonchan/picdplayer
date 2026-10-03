# 残課題とIssue一覧

2026-09-26の一覧を基に、2026-09-28にIntegrity・drive・CI検証範囲を再照合した作業一覧。
実装の契約は[設計書](../README.md)、実測・確認範囲は[検証状況](verification.md)、
着手・進捗・完了条件は各GitHub Issueで管理する。Issueを閉じる際は仕様と検証記録も更新する。
この一覧の順番は優先順位や実装順ではない。実装済みと実機確認済み、
測定した条件と一般的な保証を区別する。

親Issueは複数の独立した成果を追跡する入口であり、子Issueの完了条件と混同しない。
方式が未決定の項目を採用済みの仕様として扱わない。
実機への.debデプロイは`scripts/deploy.sh`を使用し、設定済みのsudoersの範囲でSSH越しのサービス停止・起動・再起動を自動実行できる。
再生・停止等のdaemon操作は、SSH経由でPiのloopback HTTP APIへ送信できる。
ディスクの挿入・取り出しなどの物理操作、電源断を伴うcold boot、TVの実表示確認・試聴は手順を提示してユーザーに依頼する。
ビルドと自動試験はDockerで行い、遠隔操作の成功と実表示・音声の確認は区別する。

## 実機評価・改善

| Issue | 主な範囲 |
|---|---|
| [#3 Investigate and reduce kiosk startup latency](https://github.com/udonchan/picdplayer/issues/3) | cold boot後のTV表示とtelemetryは確認済みだが、起動短縮は未解決。2026-09-25の一回の測定ではservice→wrapper約7.8秒、Cage exec→UI script受信約22.3秒、ui_ready受信はkernel起動後47.411秒だった。支配要因とTVのfirst pixelは未確定。 |
| [#4 Complete appliance runtime and endurance validation](https://github.com/udonchan/picdplayer/issues/4) | 通常再生とcold boot後のSTOPPED表示は確認済み。2026-09-28に通常CDでAPIのplay/pause/seek/resume/stopとdaemon restart後のAPI復帰を確認し、[実機記録](reports/2026-09-28-normal-runtime-api/README.md)を保存した。診断配信を復元したPR #82後の負荷・配信頻度は#83で測定し、本Issueでは結果を参照する。kiosk長期運転、Custom UIの再生と並行した表示、TV実表示・試聴、CEC、eject等には未確認項目が残る。 |
| [#27 Investigate and reduce kiosk CPU and thermal load](https://github.com/udonchan/picdplayer/issues/27) | クローズ済みの親Issue。[#52](https://github.com/udonchan/picdplayer/issues/52)の基線測定・完了判定の根拠は[測定記録](reports/2026-09-25-kiosk-baseline/README.md)へ反映済み。[#53](https://github.com/udonchan/picdplayer/issues/53)の描画改善、[#61](https://github.com/udonchan/picdplayer/issues/61)の残余負荷測定、[#62](https://github.com/udonchan/picdplayer/issues/62)のCustom UI向け注意事項は完了済み。最新構成の追加測定は[#83](https://github.com/udonchan/picdplayer/issues/83)、長期runtimeは[#4](https://github.com/udonchan/picdplayer/issues/4)へ移管した。条件と限界は[検証状況](verification.md)を参照する。 |
| [#83 Complete outstanding kiosk performance measurements](https://github.com/udonchan/picdplayer/issues/83) | #122修正候補を導入したPiで、通常CDのCDP未接続PLAYINGを5分測定した。system CPU平均13.27%、最高温度66.6°C、現在のthrottling 0で、[rawを含む記録](reports/2026-09-28-post-websocket-dedup-playing/README.md)を保存した。revision・条件が異なるため比較値ではない。STOPPED/PLAYING、修正版CDP、Cage単独、計測器負荷と条件をそろえた反復比較が残る。長期耐久・実表示・試聴は#4。 |
| [#121 Investigate player main-loop stalls across stages](https://github.com/udonchan/picdplayer/issues/121) | 過去に`control`、`api_service`、`cec_update`のstage warningを観測した。通常CDのAPI操作列、daemon restart後の30秒、CDP未接続PLAYING 5分では再現せず、CEC条件を含む再現条件と原因は未確定。5分記録は[#83の報告](reports/2026-09-28-post-websocket-dedup-playing/README.md)を参照する。 |
| [#122 Prevent duplicate WebSocket state delivery](https://github.com/udonchan/picdplayer/issues/122) | STOPPED中に同一snapshotを繰り返し送るAPI serverの不具合を#83のCDP測定で検出した。PR #123で接続ごとの送信済みgeneration追跡を追加し、当時のDocker 38/38とPiのSTOPPED 0 frame / PLAYING 40 frame確認を記録した。 |
| [#134 Eliminate timing flake in diagnostic isolation test](https://github.com/udonchan/picdplayer/issues/134) | クローズ済み。client切断後のPCM progressを固定20 tickではなくdeadline内で待つ試験へ修正し、Dockerで10回反復と全CTestを確認した。productionの再生・diagnosticsは変更していない。 |
| [#5 Evaluate read stalls and bound playback recovery](https://github.com/udonchan/picdplayer/issues/5) | 親Issue。物理媒体・別driveの実機評価は [#146](https://github.com/udonchan/picdplayer/issues/146)、有界な再生復旧は [#34](https://github.com/udonchan/picdplayer/issues/34) に分ける。同一streamのALSA underrun復帰は最大3回としてDocker自動試験済み。read errorの復旧方針、buffering表示、実機異常系は未完了であり、ALSA underrun復旧とread integrityを混同しない。 |
| [#6 Benchmark CD-DA backends and read policies](https://github.com/udonchan/picdplayer/issues/6) | `PICDPLAYER_ENABLE_PARANOIA=ON`で同じDocker build scriptからparanoia有効packageを生成でき、CTest 39件を確認した。direct/paranoiaの保存PCM正常再生とdirect single/repeatの限定的な比較はあるが、drive回転・cache条件をそろえたPi上のbackend性能比較は未完了。現行運用はdirect。 |

## integrity仕様の実装

| Issue | 主な範囲 |
|---|---|
| [#7 Extend drive capabilities and validate C2 evidence](https://github.com/udonchan/picdplayer/issues/7) | Close済み。起動時のread-only capability probe、`YES / NO / UNKNOWN`とprobe failureの区別、C2 pointer observationの`c2_status`伝搬、通常CDでのdirect C2 pointer確認を完了した。capability/backendに応じたstrategy選択は[#155](https://github.com/udonchan/picdplayer/issues/155)、傷disc・別drive・C2 command failureの物理検証は[#146](https://github.com/udonchan/picdplayer/issues/146)、hotplug lifecycleは#88へ移管した。 |
| [#8 Add bounded drive speed control with fallback](https://github.com/udonchan/picdplayer/issues/8) | `--drive-speed-x 1..255`で、KERNEL_REPORTED/YESのdriveへ停止/一時停止中に一度だけ速度要求する経路、失敗記録、API/UI投影を実装・Docker確認済み。Piでは1x要求のioctl受理と20秒API再生まで確認済み。要求受理は実測速度・回転・騒音低下を意味しない。騒音・CEC・長時間再生・throughput・失敗状態の既定速度との比較が残る。 |
| [#144 Stop optical drive spin after a bounded stopped idle period](https://github.com/udonchan/picdplayer/issues/144) | STOPPED/PAUSED中の15秒ごとの`CDROMSTART`回転維持要求を廃止し、TOC取得後にSTOPPEDが5分続いた時だけ`CDROMSTOP`を一回要求する。Docker回帰とPiでのrequest受理、TVでの自然停止、API再生復帰を確認済み。当時はeject・再挿入後のAUDIO_READY/STOPPED復帰も確認したが、現行の自動再生仕様は[#172](https://github.com/udonchan/picdplayer/issues/172)を参照。物理回転は観測しない。通常経路の完了によりCloseし、傷・劣化媒体、pause、unsupported/error、別USB drive/bridgeは [#146](https://github.com/udonchan/picdplayer/issues/146) へ集約する。 |
| [#9 Add overlap verification and cache independence evidence](https://github.com/udonchan/picdplayer/issues/9) | repeat verifierは直前に採用した15 CD frameの先行overlapを再読してPCM連続性を確認し、overlapを出力から除いて重複再生を防ぐ。不一致はfail-closed。反復・overlap一致でもdrive cacheを排除できないため`CACHE_POSSIBLE`を公開する。Docker自動試験と通常CDのPi repeat再生を確認し、[実機記録](reports/2026-09-28-repeat-overlap-normal-cd/README.md)を保存した。cache軽減手順、物理再読込の保証、傷discを含む異常媒体での検証は本Issueの保証に含めず、#146で追跡する。 |
| [#10 Track and apply CD read offsets with explicit coverage](https://github.com/udonchan/picdplayer/issues/10) | 現在のread offsetはUNKNOWN/nullで補正しない。offset不明を0とみなさず、符号・単位・根拠・端区間の扱いを決める。まず[#162](https://github.com/udonchan/picdplayer/issues/162)で明示入力・校正根拠の契約を確定し、その後reader補正とfake PCM試験を実装する。 |
| [#162 Define explicit CD read-offset calibration inputs](https://github.com/udonchan/picdplayer/issues/162) | MMC probeはoffset値を取得しない。vendor/model推定や無許可の外部databaseを使わず、stereo sample frameの符号規約、既存CLI/`/etc/default/picdplayer`からの明示入力、根拠公開、将来校正の境界を定義する。#41の永続設定実装はHard dependencyにしない。 |
| [#11 Implement capability-aware read modes and fallback policies](https://github.com/udonchan/picdplayer/issues/11) | Close済み。single/repeatの未解決PCMはqueueへ入れずSTOPする初期方針を確定した。capability-based strategy選択・降格理由の公開は[#155](https://github.com/udonchan/picdplayer/issues/155)、BEST_AVAILABLE/SILENCE/WAIT_WITH_BUDGETの採否と仕様化は[#156](https://github.com/udonchan/picdplayer/issues/156)へ移管した。read stall復旧は#34、物理異常試験は#146。 |
| [#155 Expose capability-based read strategy selection and downgrade reasons](https://github.com/udonchan/picdplayer/issues/155) | direct C2 pointer opt-inについて、requested/effective strategy、pending、machine-readable downgrade reasonをsnapshotへ公開する。probe前、capability UNKNOWN/NO、PLAYING/PAUSED中のreader再生成待ちを区別する。追加mode、未解決PCMの代替、read stall復旧、傷disc・別driveの実機検証は含めない。 |
| [#34 Bound playback recovery after read stalls](https://github.com/udonchan/picdplayer/issues/34) | effective ReadPolicyの`time_budget_ms`を一回のreader callのmain-loop監視上限にも適用する。超過時はioctlを強制中断せず、streamをSTOPPEDにしてworker threadで後からreaderを破棄する。inflight/timeoutはdiagnostic APIへ公開し、ALSA underrun復旧・RECOVERED・代替PCMと混同しない。傷disc・別driveでの評価は#146。 |
| [#109 Provide deterministic Integrity scenarios for Player UI review](https://github.com/udonchan/picdplayer/issues/109) | クローズ済みの親Issue。#110のtest-only `ScriptedCddaReader`と#111のlocal harnessにより、clean、retry、RECOVERED、UNCERTAIN、mixed map、read-ahead、状態遷移を通常のworker/API/Player asset経路で確認できる。production daemon/CLI/packageと実drive挙動は変更せず、実機異常mediaは#146で追跡する。 |
| [#12 Add bounded provenance and diagnostic event recovery](https://github.com/udonchan/picdplayer/issues/12) | #35/#36の実装と#89/#90の追加検証をPR #86/#87/#94/#95で整備。Docker36/36成功。全子Issue完了によりClose済み。実機異常系は#146、長期評価は#4/#83へ分離する。 |
| [#13 Add optional external PCM verification](https://github.com/udonchan/picdplayer/issues/13) | MusicBrainz metadataはPCM照合ではない。AccurateRipは第三者アクセスの利用根拠がなく既定providerにしない。CTDBは全disc ripを前提とするため、現行の部分再生PCMを照合入力にしない。providerの採用可否・protocolと結果契約を追跡する。 |
| [#160 Define complete-disc PCM capture for external verification](https://github.com/udonchan/picdplayer/issues/160) | #13で全域照合を実装する前提となる、完全取得PCMのcoverage・generation・保存/破棄・再生とのI/O調停を定義する。通常起動・通常再生中の自動full-disc readは対象外。 |
| [#92 Document the current playback and diagnostic message contracts](https://github.com/udonchan/picdplayer/issues/92) | 現行state/WS/詳細履歴のfield・型・単位・世代・順序・欠落・互換性を[メッセージ契約](../design/message-contract.md)へ整理済み。#89/#90は完了。#24正式化時に公開実装と再照合済み。 |
| [#146 Validate physical optical-media faults and USB drive compatibility](https://github.com/udonchan/picdplayer/issues/146) | 傷・劣化媒体、read stall、別USB optical drive/bridgeの実機評価を集約する。音声/操作影響とAPI・警告・復元の整合を同じrun記録で確認する。physical hotplugは#88、特殊TOCは#39/#40が担当する。 |
| [#97 Correct diagnostic buffer capacity and missing counter displays](https://github.com/udonchan/picdplayer/issues/97) | 診断画面の端数付きblock容量と欠損counterの0表示を修正。PR #95で回帰試験を追加しDockerで検証（Pi未再確認）。 |
| [#24 Integrate CD read integrity into the player UI](https://github.com/udonchan/picdplayer/issues/24) | #98/#99と監査修正#103〜#106はマージ済み。Phase 1ではscope付きsummary・有界disc read map・全read/drive値を常時表示し、実使用で情報量を評価する。primary playbackを優先し、daemon接続をIntegrity headerへ統合する。PR #108をマージしPhase 1はClose済み。通常Pi CDP確認済み。deterministicな異常scenarioの開発確認は#109で完了し、実機異常系は#146、長期/負荷は#4/#83で追跡する。 |
| [#150 Reorganize Integrity Monitor information hierarchy and responsive layout](https://github.com/udonchan/picdplayer/issues/150) | PR #163をmergeしClose済み。Playerの幅を維持してRead observationを左、cardlessなDisc mapとDrive capabilityを右に配置した。通常CDのPi CDP表示とmixed fixtureは[比較記録](reports/2026-09-30-disc-read-map/README.md)に残した。policy/strategy選択表示と#25導入後の状態別UI評価は[#164](https://github.com/udonchan/picdplayer/issues/164)へ移管した。#83と#124の#150依存は充足し、#25は引き続き未完了。 |
| [#164 Finish Integrity Monitor state presentation after artist backgrounds](https://github.com/udonchan/picdplayer/issues/164) | #150の残課題を引き継ぎ、`SINGLE`/`REPEAT`の選択状態、strategyのrequested/effective/pending/downgrade表示、No Disc・pending・unknown・長文等の状態別確認を#25のArtist Background導入後に実施する。GitHub上で#25にblocked byを設定済み。既存2列構図の再設計やbackend推測値の追加は含めない。 |

## 障害対応・機能改善

| Issue | 主な範囲 |
|---|---|
| [#14 Recover CEC device loss and bound address claiming](https://github.com/udonchan/picdplayer/issues/14) | 通常のCEC登録・操作・ARC復帰は実機確認済み。device消失後の再open、claim timeout、専有制御は未実装/検討中。 |
| [#15 Add explicit metadata release selection](https://github.com/udonchan/picdplayer/issues/15) | PR #170をmergeしてClose済み。同じDisc IDの複数候補をAMBIGUOUSで保持し、候補公開、世代付き選択API、選択後の非同期画像取得を実装した。Piの『The Slip』で実候補の公開と選択後metadata/cover URL反映も確認した。標準UIのCEC候補pickerは[#166](https://github.com/udonchan/picdplayer/issues/166)としてPR #171をmerge済み。現在discを越える選択の永続化は対象外。 |
| [#166 Select ambiguous metadata candidates from the Player with CEC](https://github.com/udonchan/picdplayer/issues/166) | #15の候補契約と#55のsemantic CEC入力を用いた標準UIの候補pickerをPR #171で実装した。曖昧候補は初回に自動表示し、back後は`Choose album`から開き直せる。Docker自動試験、Mac Chromeの合成fixture表示、Piの『The Slip』実候補選択・TV表示・初回自動表示とBack後の非再表示を確認した。2026-10-03にユーザーは候補選択後のcover画像をTVで確認した。異常系の実機確認は未実施。 |
| [#172 Start playback when an audio CD is inserted](https://github.com/udonchan/picdplayer/issues/172) | 候補picker #166とは別責務。daemon稼働中に明示的なdisc不在/eject後のAudio CDをTOC受理したら自動再生し、metadata候補選択を待たない。daemon起動時に既に挿入済みのCDやSTOP後のpollでは自動再生しない。Docker 48/48とPiで同一PIDのeject→再挿入→PLAYING、TV音声を再確認し、APIのSTOP後5秒もSTOPPEDを維持した。異常媒体や別driveは未検証。 |
| [#16 Harden metadata caching and HTTP input handling](https://github.com/udonchan/picdplayer/issues/16) | クローズ済み親Issue。#37でTTL・容量・atomic更新・破損cache無効化/再取得・offline fallback、#38でJSON入力上限・有界Retry-After・CAA redirect先host/IP制限を実装した。外部依存のHTTP統合試験とPi network確認は[#135](https://github.com/udonchan/picdplayer/issues/135)へ分離した。 |
| [#135 Validate metadata HTTP failure handling with deterministic mocks](https://github.com/udonchan/picdplayer/issues/135) | #38の実装後検証。test-only `MetadataOptions::http_get`で429/Retry-After、503再試行、CAA routing、cancel、注入したconnection failureから`MetadataWorker`のERROR結果への変換、古いgenerationのERROR結果を`MetadataSession`が適用しないことをDockerで確認済み。空けたloopback portへの実`HttpClient`接続失敗もDockerで確認済み。実HTTPS responseを使うtimeout/redirect header、Piでの通常metadata/CAA lookup記録が残る。 |
| [#17 Define metadata mapping for unusual TOCs and disc identity](https://github.com/udonchan/picdplayer/issues/17) | 親Issue。[#39 先頭trackが1でないTOC](https://github.com/udonchan/picdplayer/issues/39)ではMusicBrainz medium positionをTOC順の物理track番号へ対応付け、曲数不一致をERRORへする実装とDocker試験を追加した。実機の非1始まりTOC確認は未実施。[#40 同一TOCの識別限界](https://github.com/udonchan/picdplayer/issues/40)が残る。 |
| [#25 Add artist backgrounds as progressive player enrichment](https://github.com/udonchan/picdplayer/issues/25) | 親Issue。[#48 artist識別](https://github.com/udonchan/picdplayer/issues/48)、[#49 provider/cache](https://github.com/udonchan/picdplayer/issues/49)、[#50 段階的配信](https://github.com/udonchan/picdplayer/issues/50)、[#51 Player表示](https://github.com/udonchan/picdplayer/issues/51)に分割済み。 |
| [#177 Persist user settings for read policy and artist backgrounds](https://github.com/udonchan/picdplayer/issues/177) | #41の保存・優先順位契約を入力に、Read PolicyとArtist Backgroundの明示ON/OFFをdaemonが保存・公開する。PR #179をmergeし、`--settings-file`指定時のRead Policy保存・復元を部分実装した。実daemon APIの再起動試験はDockerとPiの隔離processで確認した。Piのservice user/systemd統合は未確認。背景の明示ON/OFFと利用可否APIは未実装。#49/#51のruntime経路と権利条件が整うまでは利用可能と表示しない。 |
| [#50 Support progressive delivery of visual enrichment](https://github.com/udonchan/picdplayer/issues/50) | PR #175をmergeし、MusicBrainz metadataをCAAより先に公開する経路、後続artworkの世代照合、独立した失敗statusをDockerで検証済み。Artist Background自体の配信とPiでの更新順序確認は残り、IssueはOpenを維持する。 |
| [#48 Expose selected artist identity for enrichment](https://github.com/udonchan/picdplayer/issues/48) | PR #174をmergeしてClose済み。選択済みreleaseのartist-creditから、単一の有効なArtist MBIDだけをEnrichment内部へ渡す。複数IDはAMBIGUOUS、欠損・不正・Various ArtistsはUNAVAILABLE。外部背景provider、Viewへの画像契約は#49/#51へ分離する。 |
| [#28 Hide the cursor in the Cage kiosk session](https://github.com/udonchan/picdplayer/issues/28) | kiosk上のcursorを非表示にする方法を調査・検証する。 |
| [#124 Investigate unintended kiosk scrollbar display](https://github.com/udonchan/picdplayer/issues/124) | TVで意図しないscrollbarを目視した。CDPのDOM probeでは1920×1080 viewportにdocument/root/bodyのoverflowや画面外elementを検出できず、Chromium/Cage/Waylandを含む原因は未確定。UIの見た目変更とは切り分けて調査する。 |
| [#31 Add CEC-driven controls to the Player view](https://github.com/udonchan/picdplayer/issues/31) | Close済みの親Issue。[#54 loopback操作契約](https://github.com/udonchan/picdplayer/issues/54)、[#55 CEC navigation配信](https://github.com/udonchan/picdplayer/issues/55)、[#56 Player操作UI](https://github.com/udonchan/picdplayer/issues/56)に分割済み。#54はPR #168、#55はPR #167、#56はPR #169をmergeしてClose済み。Piで上下左右・決定・Back、再生/音声・一時停止・停止、右方向キー約2秒長押し後の選択停止を確認した。CECとChromium keydownの二重入力を#56で抑制した。別TV・入力切替時の挙動は未確認。metadata候補pickerは[#166](https://github.com/udonchan/picdplayer/issues/166)で別途扱う。 |
| [#178 Add a CEC-accessible Player settings screen](https://github.com/udonchan/picdplayer/issues/178) | Draft PR #180でRead PolicyのSINGLE/REPEAT選択と詳細値表示を実装中。#177の任意保存はRead Policyのみ部分実装済み。Artist Backgroundの明示ON/OFFは#49/#51と利用条件が揃ってから扱う。PiのChromium/CDPで1920×1080表示とREPEAT→SINGLE操作を確認。TV実機でBackボタンへCEC方向キーで移れない問題を確認して修正し、Node回帰試験は通過、Piへ再デプロイ済み。修正版のCEC実操作と実serviceでの保存・復元は未確認。 |

## 設計を先に確定する作業

| Issue | 主な範囲 |
|---|---|
| [#18 Specify persistent settings and custom UI updates](https://github.com/udonchan/picdplayer/issues/18) | 親Issue。Custom UIの起動時静的検証とfallbackは実装済み。[#41 永続設定の契約](https://github.com/udonchan/picdplayer/issues/41)と[#42 Custom UI更新・復旧](https://github.com/udonchan/picdplayer/issues/42)を追跡する。契約後の実装は[#177](https://github.com/udonchan/picdplayer/issues/177)、標準画面は[#178](https://github.com/udonchan/picdplayer/issues/178)。 |
| [#21 Plan reproducible releases and appliance images](https://github.com/udonchan/picdplayer/issues/21) | 親Issue。PR向けDocker/aarch64 CIと[#43 開発用Debian package](https://github.com/udonchan/picdplayer/issues/43)は完了済み。[#44 更新・削除](https://github.com/udonchan/picdplayer/issues/44)では使い捨てcontainerでinstall/upgrade/reinstall/purgeをCI化し、Piのactive serviceで同版reinstallと不正archive拒否後の正常artifact復旧を確認した。通常lifecycleの完了により#44をCloseし、異version upgradeと展開後/maintainer script中断からの破壊的復旧確認は [#147](https://github.com/udonchan/picdplayer/issues/147) へ移管する。[#45 版付きrelease artifact](https://github.com/udonchan/picdplayer/issues/45)、[#46 image要件](https://github.com/udonchan/picdplayer/issues/46)、[#47 bootable image](https://github.com/udonchan/picdplayer/issues/47)を追跡する。最終imageに開発用`.deb`を使うかは未決定。 |

## 未実装の製品機能

| Issue | 主な範囲 |
|---|---|
| [#19 Add quiet boot with a diagnosable failure path](https://github.com/udonchan/picdplayer/issues/19) | Chromium/CageのTV表示は動作しているが、kernel/systemd画面の非表示とsplashは未実装。起動時間短縮とは別の製品上の課題。 |
| [#20 Design and validate a read-only root deployment](https://github.com/udonchan/picdplayer/issues/20) | read-only rootは未実装。cache・Chromium profile・journal・設定・Custom UIなどの書込先を分ける必要がある。 |

## OSSライセンスと配布物の追跡

最終目標はソース公開だけでなく、実際に配布するbootable imageのOSS構成を継続的に追跡すること。
現在は`stage/`と開発用`.deb`をCMake install規則から生成し、Piへdpkgで導入する。
専用bootable imageと公開release workflowは未実装である。

| Issue | 段階と残る作業 |
|---|---|
| [#66 Establish project licensing and audit direct dependencies](https://github.com/udonchan/picdplayer/issues/66) | Phase 1。本体のApache-2.0案、直接依存、optionalなlibcdio-paranoiaの配布条件を監査する。対応containerでのlibcdio-paranoiaはGPL-3-or-laterと確認し、`ENABLE_PARANOIA=ON` configure時に再配布前の確認を促す警告を追加した。本体ライセンスと依存監査文書は未確定。 |
| [#67 Track Debian package contents and distribution metadata](https://github.com/udonchan/picdplayer/issues/67) | Phase 2。#66を入力に、`.deb`の内容・runtime依存・copyrightを追跡可能にする。 |
| [#68 Generate compliance artifacts from bootable release images](https://github.com/udonchan/picdplayer/issues/68) | Phase 3。#67と検査可能な#47のimageを入力に、最終image実体のinventory・SBOM・notice等を生成する。 |
| [#69 Integrate compliance metadata with an embedded build system](https://github.com/udonchan/picdplayer/issues/69) | Phase 4。Buildroot/Yocto等への移行が決まった場合のみ着手する将来候補。 |

## 採用条件が整うまで保留する候補

次は現在の不具合や実装必須項目ではない。必要性と対象が決まった時点で独立Issueを作る。

| 候補 | 保留理由・着手条件 |
|---|---|
| S/PDIF・外部I²S・複数出力profile | [出力拡張案](digital-audio-output.md)。対象hardwareと利用目的が未決定。現行HDMIのbit-perfectも保証しない |
| AsyncLoggerのlibrary置換 | 現状の要件を満たす。追加sink・runtime level・rotation等が必要になった時にサイズ・依存・queue/flushを比較 |
| mixed-mode・負LBA・隠しtrack・CD-TEXT | 現行は音声のみのCDが対象。対応discと用途が決まった時にTOC契約・試験を追加 |
| 外部公開APIの認証・TLS | 現行はloopback操作と信頼する開発LANの診断用途。公開範囲を拡張する要件が決まった時に設計 |

ジャケット画像のbinary cacheとsame-origin配信は[#23](https://github.com/udonchan/picdplayer/issues/23)で
実装済み。破損・容量・offline運用等の未検証事項は[検証状況](verification.md)を参照し、
未実装候補として重複掲載しない。

## 以前の整合性確認（2026-09-25）

README、Guide、Design、Manual、Developmentと、History/旧パスの案内・リンクを確認した。
履歴の当時の数値・判断は現行仕様に合わせて書き換えない。

- integrity拡張案を[設計仕様](../design/integrity-design.md)へ統合し、現行型・未実装要求・暫定値を区別した。
- 通常の開発をMac + Docker、Piをruntime検証に統一し、Linux単体ビルドは補助手順として残した。
- CMakeの試験登録条件とCIを照合し、26件だった過去の結果とPython追加後の29件を区別した。
- main loopのengine tick頻度、LOADING後のmetadata再要求、能力probeの待ちと再取得の限界をコードに合わせた。
- UI telemetryとCustom UIのAPI記述、cold boot・日本語表示・journald・underrun復旧の確認範囲を更新した。

この整理で新たな実機試験を行ったことにはしない。未確認事項は各Issueの完了条件として残す。

## Buildrootの適合性調査

[#74](https://github.com/udonchan/picdplayer/issues/74)を親として、
[#75 公式資料](https://github.com/udonchan/picdplayer/issues/75)、
[#76 OSS事例比較](https://github.com/udonchan/picdplayer/issues/76)、
[#77 現行runtime要件](https://github.com/udonchan/picdplayer/issues/77)を並行調査し、
[#78](https://github.com/udonchan/picdplayer/issues/78)で適合性評価と必要な後続Issue作成を行う。
Buildroot採用と最終imageへの.deb利用は未決定である。

## 監査で確認した不整合・回帰

[#79 文書整合](https://github.com/udonchan/picdplayer/issues/79)、
[#80 CDP接続・trace集計](https://github.com/udonchan/picdplayer/issues/80)、
[#81 診断API・status表示](https://github.com/udonchan/picdplayer/issues/81)を追跡する。
修正の検証範囲は[検証状況](verification.md)を参照する。

## 物理drive hotplug（保留）

[#88](https://github.com/udonchan/picdplayer/issues/88)で物理交換/reset検出・能力失効・再取得を追跡する。
#7の物理lifecycle部分を分離したPending項目。#35のreader-open観測世代と混同せず、#24の追加Hard dependencyにはしない。

### 全ディスクread mapの前提

- [#98](https://github.com/udonchan/picdplayer/issues/98): TOC座標とdisc観測世代のdisc.layout公開はPR #101でマージ済み。Docker36/36、通常PiのTOC/REST/WS照合済み。
- [#99](https://github.com/udonchan/picdplayer/issues/99): 最大256区間のDISC集計を実装。PR #102でマージ済み。Docker36/36、Piで140 read超・stop/resume保持・daemon restart resetを確認。

#98/#99は完了済みの前提。#24の標準Player consumerはPR #108で完了済み。相互はRelatedで、#12の完了を取り消さず追加機能として管理する。

## #24着手前の監査修正（PR #102）

- [#103](https://github.com/udonchan/picdplayer/issues/103): stream終了時の旧再生根拠・欠落counter破棄。
- [#104](https://github.com/udonchan/picdplayer/issues/104): disc/session変更時の同URL artwork再取得。
- [#105](https://github.com/udonchan/picdplayer/issues/105): 最終throttled sample欠損時の集計。
- [#106](https://github.com/udonchan/picdplayer/issues/106): 終端underrunの再seek防止。

いずれもPR #102でマージ済み。Docker37/37成功。
実機では修正前の終端試験が失敗したため、その後の通常再生だけを確認した。修正版で13秒間の
通常再生・API stop・エラーなしと、TVの表示・音声再生は確認済みだが、終端drainは未確認。
[監査記録](reports/2026-09-26-pre-integrity-audit/README.md)を参照する。

## 2026-09-27変更の監査

- [#115](https://github.com/udonchan/picdplayer/issues/115): 未比較overlapの不一致誤表示、拒否候補の採用識別子をPR #117で修正・Docker確認済み（マージ待ち）。#9の実機検証は別途継続。
- [#116](https://github.com/udonchan/picdplayer/issues/116): #24完了、速度要求、世代・履歴公開、READMEとCI検証範囲の文書反映漏れをPR #117で修正（マージ待ち）。
