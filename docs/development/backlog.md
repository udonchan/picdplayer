# 残課題とIssue一覧

2026-09-26時点のrepositoryとGitHub Issueを照合した作業一覧。
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
| [#4 Complete appliance runtime and endurance validation](https://github.com/udonchan/picdplayer/issues/4) | 通常再生とcold boot後のSTOPPED表示は確認済み。診断配信を復元したPR #82後の負荷・配信頻度は#83で測定し、本Issueでは結果を参照する。最新service構成での一連の操作、kiosk長期運転、Custom UIの再生と並行した表示には未確認項目が残る。 |
| [#27 Investigate and reduce kiosk CPU and thermal load](https://github.com/udonchan/picdplayer/issues/27) | 親Issue。[#52](https://github.com/udonchan/picdplayer/issues/52)の基線測定・完了判定の根拠は[測定記録](reports/2026-09-25-kiosk-baseline/README.md)へ反映済み。Cage単独の省略理由とtrace件数の制約を明示し、クローズ対象として整理した。[#53](https://github.com/udonchan/picdplayer/issues/53)の描画改善、[#61](https://github.com/udonchan/picdplayer/issues/61)の残余負荷測定、[#62](https://github.com/udonchan/picdplayer/issues/62)のCustom UI向け注意事項は完了済み。条件と限界は[検証状況](verification.md)を参照する。 |
| [#83 Complete outstanding kiosk performance measurements](https://github.com/udonchan/picdplayer/issues/83) | 最新masterのSTOPPED/PLAYING、修正版CDP、Cage単独、計測器負荷と条件をそろえた反復比較。#52の未完了測定を引き継ぐ。長期耐久・実表示・試聴は#4。 |
| [#5 Evaluate read stalls and bound playback recovery](https://github.com/udonchan/picdplayer/issues/5) | 親Issue。[#33 read stallの影響測定](https://github.com/udonchan/picdplayer/issues/33)と[#34 有界な再生復旧](https://github.com/udonchan/picdplayer/issues/34)に分割済み。ALSA underrun復旧とread integrityを混同しない。 |
| [#6 Benchmark CD-DA backends and read policies](https://github.com/udonchan/picdplayer/issues/6) | direct/paranoiaの保存PCM正常再生とdirect single/repeatの限定的な比較はあるが、drive回転・cache条件をそろえたbackend性能比較は未完了。現行運用はdirect。 |

## integrity仕様の実装

| Issue | 主な範囲 |
|---|---|
| [#7 Extend drive capabilities and validate C2 evidence](https://github.com/udonchan/picdplayer/issues/7) | 現行probeは起動時一回のsysfs identityとCDROM_GET_CAPABILITY。speed control以外の能力はUNKNOWNで、C2取得・trust評価・hotplug/reset時の能力失効は未実装。 |
| [#8 Add bounded drive speed control with fallback](https://github.com/udonchan/picdplayer/issues/8) | Phase 2のbuffer設定・drive I/O直列化は実装済みだが速度設定は未実装。KERNEL_REPORTED/YESだけでは実際の速度制御や効果を確認したことにならない。 |
| [#9 Add overlap verification and cache independence evidence](https://github.com/udonchan/picdplayer/issues/9) | 現行repeatは同一区間のPCM全体の反復一致だけを調べる。overlap整列とcache対策はなく、2-of-3一致でも独立した物理再読込を保証しない。 |
| [#10 Track and apply CD read offsets with explicit coverage](https://github.com/udonchan/picdplayer/issues/10) | 現在のread offsetはUNKNOWN/nullで補正しない。offset不明を0とみなさず、符号・単位・根拠・端区間の扱いを決める必要がある。 |
| [#11 Implement capability-aware read modes and fallback policies](https://github.com/udonchan/picdplayer/issues/11) | 現行ReadPolicyはsingle/repeatと停止境界のruntime切替。QUIET/BALANCED/SECUREや未解決時の追加fallbackは未実装であり、backend名をsecure保証にしない。 |
| [#12 Add bounded provenance and diagnostic event recovery](https://github.com/udonchan/picdplayer/issues/12) | 親Issue。#35/#36の根拠・coverage・有界履歴・復元契約はPR #86/#87でマージ済み。未完了検証を[#89](https://github.com/udonchan/picdplayer/issues/89)（容量・drop・再生中の根拠保持）と[#90](https://github.com/udonchan/picdplayer/issues/90)（異常系復元・slow client/ログ障害時のaudio非干渉。追加試験36/36成功、再生ログ経路との組合せ・メモリ有界性等は継続中）へ移管。親はOpenを維持する。 |
| [#13 Add optional external PCM verification](https://github.com/udonchan/picdplayer/issues/13) | MusicBrainz metadataはPCM照合ではない。外部checksum照合は未実装で、利用するサービス・protocol・依存は未決定。 |
| [#92 Document the current playback and diagnostic message contracts](https://github.com/udonchan/picdplayer/issues/92) | 現行state/WS/詳細履歴のfield・型・単位・世代・順序・欠落・互換性を[メッセージ契約](../design/message-contract.md)へ整理済み。#89/#90の検証と並行可能。#24の表示契約との整合も確認する。 |
| [#24 Integrate CD read integrity into the player UI](https://github.com/udonchan/picdplayer/issues/24) | Draftを維持。#35/#36の実装済み契約を基盤とし、#90の復元検証と公開契約・文書の整合確認後に着手可否を判断する。未観測値をUNKNOWN/UNSUPPORTEDと区別する。メッセージ仕様を変更するたび、関連文書と#24の現状・データソース・完了条件を同時に更新する。 |

## 障害対応・機能改善

| Issue | 主な範囲 |
|---|---|
| [#14 Recover CEC device loss and bound address claiming](https://github.com/udonchan/picdplayer/issues/14) | 通常のCEC登録・操作・ARC復帰は実機確認済み。device消失後の再open、claim timeout、専有制御は未実装/検討中。 |
| [#15 Add explicit metadata release selection](https://github.com/udonchan/picdplayer/issues/15) | 同じDisc IDに複数候補があるとAMBIGUOUSを保持する。明示的な候補選択API/UIと選択の保存は未実装。 |
| [#16 Harden metadata caching and HTTP input handling](https://github.com/udonchan/picdplayer/issues/16) | 親Issue。[#37 cache lifecycle](https://github.com/udonchan/picdplayer/issues/37)と[#38 HTTP応答・解析の上限](https://github.com/udonchan/picdplayer/issues/38)に分割済み。 |
| [#17 Define metadata mapping for unusual TOCs and disc identity](https://github.com/udonchan/picdplayer/issues/17) | 親Issue。[#39 先頭trackが1でないTOC](https://github.com/udonchan/picdplayer/issues/39)と[#40 同一TOCの識別限界](https://github.com/udonchan/picdplayer/issues/40)に分割済み。 |
| [#25 Add artist backgrounds as progressive player enrichment](https://github.com/udonchan/picdplayer/issues/25) | 親Issue。[#48 artist識別](https://github.com/udonchan/picdplayer/issues/48)、[#49 provider/cache](https://github.com/udonchan/picdplayer/issues/49)、[#50 段階的配信](https://github.com/udonchan/picdplayer/issues/50)、[#51 Player表示](https://github.com/udonchan/picdplayer/issues/51)に分割済み。 |
| [#28 Hide the cursor in the Cage kiosk session](https://github.com/udonchan/picdplayer/issues/28) | kiosk上のcursorを非表示にする方法を調査・検証する。 |
| [#31 Add CEC-driven controls to the Player view](https://github.com/udonchan/picdplayer/issues/31) | 親Issue。[#54 loopback操作契約](https://github.com/udonchan/picdplayer/issues/54)、[#55 CEC navigation配信](https://github.com/udonchan/picdplayer/issues/55)、[#56 Player操作UI](https://github.com/udonchan/picdplayer/issues/56)に分割済み。 |

## 設計を先に確定する作業

| Issue | 主な範囲 |
|---|---|
| [#18 Specify persistent settings and custom UI updates](https://github.com/udonchan/picdplayer/issues/18) | 親Issue。Custom UIの起動時静的検証とfallbackは実装済み。[#41 永続設定](https://github.com/udonchan/picdplayer/issues/41)と[#42 Custom UI更新・復旧](https://github.com/udonchan/picdplayer/issues/42)を追跡する。 |
| [#21 Plan reproducible releases and appliance images](https://github.com/udonchan/picdplayer/issues/21) | 親Issue。PR向けDocker/aarch64 CIと[#43 開発用Debian package](https://github.com/udonchan/picdplayer/issues/43)は完了済み。残る[#44 更新・削除](https://github.com/udonchan/picdplayer/issues/44)、[#45 版付きrelease artifact](https://github.com/udonchan/picdplayer/issues/45)、[#46 image要件](https://github.com/udonchan/picdplayer/issues/46)、[#47 bootable image](https://github.com/udonchan/picdplayer/issues/47)を追跡する。最終imageに開発用`.deb`を使うかは未決定。 |

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
| [#66 Establish project licensing and audit direct dependencies](https://github.com/udonchan/picdplayer/issues/66) | Phase 1。本体のApache-2.0案、直接依存、optionalなlibcdio-paranoiaの配布条件を監査する。 |
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
