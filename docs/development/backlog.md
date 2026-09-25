# 残課題とIssue一覧

2026-09-25のドキュメント整合性確認とintegrity仕様統合を基準にした作業一覧。
実装の契約は[設計書](../README.md)、実測・確認範囲は[検証状況](verification.md)、
着手・進捗・完了条件は各GitHub Issueで管理する。Issueを閉じる際は仕様と検証記録も更新する。
この一覧の順番は確約された実装順ではない。まず実機回帰確認と起動時間の要因分析を進め、
integrity拡張は必要な能力・根拠モデルの成立を確認して段階的に実装する。

「設計を先に確定する作業」は方式を選び実装へ分割するためのIssueであり、候補方式の採用決定ではない。
実機操作・再起動・試聴は手順を提示してユーザーに依頼し、ビルドと自動試験はDockerで行う。

## 実機評価・改善

| Issue | 主な範囲 |
|---|---|
| [#3 Investigate and reduce kiosk startup latency](https://github.com/udonchan/picdplayer/issues/3) | cold boot後のTV表示とtelemetryは確認済みだが、起動短縮は未解決。2026-09-25の一回の測定ではservice→wrapper約7.8秒、Cage exec→UI script受信約22.3秒、ui_ready受信はkernel起動後47.411秒だった。支配要因とTVのfirst pixelは未確定。 |
| [#4 Complete appliance runtime and endurance validation](https://github.com/udonchan/picdplayer/issues/4) | 通常再生とcold boot後のSTOPPED表示は確認済み。最新service構成での一連の操作、kiosk長期運転、Custom UIの再生と並行した表示には未確認項目が残る。 |
| [#5 Evaluate read stalls and bound playback recovery](https://github.com/udonchan/picdplayer/issues/5) | ALSA underrunは自動復旧するが回数上限はない。API snapshot遅延によるunderrunと復旧は一度観測済み。傷disc・USB reset・長いread stallによる音の欠落/重複、操作遅延は未評価。pause復帰の先読み待ちも継続評価する。 |
| [#6 Benchmark CD-DA backends and read policies](https://github.com/udonchan/picdplayer/issues/6) | direct/paranoiaの保存PCM正常再生とdirect single/repeatの限定的な比較はあるが、drive回転・cache条件をそろえたbackend性能比較は未完了。現行運用はdirect。 |

## integrity仕様の実装

| Issue | 主な範囲 |
|---|---|
| [#7 Extend drive capabilities and validate C2 evidence](https://github.com/udonchan/picdplayer/issues/7) | 現行probeは起動時一回のsysfs identityとCDROM_GET_CAPABILITY。speed control以外の能力はUNKNOWNで、C2取得・trust評価・hotplug/reset時の能力失効は未実装。 |
| [#8 Add bounded drive speed control with fallback](https://github.com/udonchan/picdplayer/issues/8) | Phase 2のbuffer設定・drive I/O直列化は実装済みだが速度設定は未実装。KERNEL_REPORTED/YESだけでは実際の速度制御や効果を確認したことにならない。 |
| [#9 Add overlap verification and cache independence evidence](https://github.com/udonchan/picdplayer/issues/9) | 現行repeatは同一区間のPCM全体の反復一致だけを調べる。overlap整列とcache対策はなく、2-of-3一致でも独立した物理再読込を保証しない。 |
| [#10 Track and apply CD read offsets with explicit coverage](https://github.com/udonchan/picdplayer/issues/10) | 現在のread offsetはUNKNOWN/nullで補正しない。offset不明を0とみなさず、符号・単位・根拠・端区間の扱いを決める必要がある。 |
| [#11 Implement capability-aware read modes and fallback policies](https://github.com/udonchan/picdplayer/issues/11) | 現行ReadPolicyはsingle/repeatと停止境界のruntime切替。QUIET/BALANCED/SECUREや未解決時の追加fallbackは未実装であり、backend名をsecure保証にしない。 |
| [#12 Add bounded provenance and diagnostic event recovery](https://github.com/udonchan/picdplayer/issues/12) | 現在はReadEvidence、stream集計、直近64件のsnapshot eventとworker queue上限256件がある。詳細attempt履歴、unique coverage、session/gap/replay、active warning復元は未実装。 |
| [#13 Add optional external PCM verification](https://github.com/udonchan/picdplayer/issues/13) | MusicBrainz metadataはPCM照合ではない。外部checksum照合は未実装で、利用するサービス・protocol・依存は未決定。 |

## 障害対応・機能改善

| Issue | 主な範囲 |
|---|---|
| [#14 Recover CEC device loss and bound address claiming](https://github.com/udonchan/picdplayer/issues/14) | 通常のCEC登録・操作・ARC復帰は実機確認済み。device消失後の再open、claim timeout、専有制御は未実装/検討中。 |
| [#15 Add explicit metadata release selection](https://github.com/udonchan/picdplayer/issues/15) | 同じDisc IDに複数候補があるとAMBIGUOUSを保持する。明示的な候補選択API/UIと選択の保存は未実装。 |
| [#16 Harden metadata caching and HTTP input handling](https://github.com/udonchan/picdplayer/issues/16) | 現行cacheはraw JSONをparse前に保存する。期限・総容量・破損時再取得、Retry-After、JSONの深さ/全field長の制限等が未実装。 |
| [#17 Define metadata mapping for unusual TOCs and disc identity](https://github.com/udonchan/picdplayer/issues/17) | DiscTocは先頭track番号を1に固定しないがmetadata track positionとの対応は未実装。交換を観測できない同一TOCの別discは識別できない。一方、LOADING後の同一TOCへのmetadata再要求は修正・確認済み。 |

## 設計を先に確定する作業

| Issue | 主な範囲 |
|---|---|
| [#18 Specify persistent settings and custom UI updates](https://github.com/udonchan/picdplayer/issues/18) | Custom UIは起動時の静的検証とfallbackを実装済み。永続設定、設定API/画面、runtime JS検査、hot reload、共通bootstrap/SDKは未実装。 |
| [#21 Plan reproducible releases and appliance images](https://github.com/udonchan/picdplayer/issues/21) | PR向けDocker/aarch64 CIと、CMake install規則から生成する開発用Debian package deployを実装。release artifact、package version運用、Pi OS/Buildroot imageは未実装。 |

## 未実装の製品機能

| Issue | 主な範囲 |
|---|---|
| [#19 Add quiet boot with a diagnosable failure path](https://github.com/udonchan/picdplayer/issues/19) | Chromium/CageのTV表示は動作しているが、kernel/systemd画面の非表示とsplashは未実装。起動時間短縮とは別の製品上の課題。 |
| [#20 Design and validate a read-only root deployment](https://github.com/udonchan/picdplayer/issues/20) | read-only rootは未実装。cache・Chromium profile・journal・設定・Custom UIなどの書込先を分ける必要がある。 |

## 採用条件が整うまで保留する候補

次は現在の不具合や実装必須項目ではない。必要性と対象が決まった時点で独立Issueを作る。

| 候補 | 保留理由・着手条件 |
|---|---|
| S/PDIF・外部I²S・複数出力profile | [出力拡張案](digital-audio-output.md)。対象hardwareと利用目的が未決定。現行HDMIのbit-perfectも保証しない |
| daemon側のジャケット画像binary cache | 現行はbrowserがHTTPS画像を表示する。offline表示等の要件、容量・検証・失敗時挙動が決まった時に検討 |
| AsyncLoggerのlibrary置換 | 現状の要件を満たす。追加sink・runtime level・rotation等が必要になった時にサイズ・依存・queue/flushを比較 |
| mixed-mode・負LBA・隠しtrack・CD-TEXT | 現行は音声のみのCDが対象。対応discと用途が決まった時にTOC契約・試験を追加 |
| 外部公開APIの認証・TLS | 現行はloopback操作と信頼する開発LANの診断用途。公開範囲を拡張する要件が決まった時に設計 |

## 今回の整合性確認

README、Guide、Design、Manual、Developmentと、History/旧パスの案内・リンクを確認した。
履歴の当時の数値・判断は現行仕様に合わせて書き換えない。

- integrity拡張案を[設計仕様](../design/integrity-design.md)へ統合し、現行型・未実装要求・暫定値を区別した。
- 通常の開発をMac + Docker、Piをruntime検証に統一し、Linux単体ビルドは補助手順として残した。
- CMakeの試験登録条件とCIを照合し、26件だった過去の結果とPython追加後の29件を区別した。
- main loopのengine tick頻度、LOADING後のmetadata再要求、能力probeの待ちと再取得の限界をコードに合わせた。
- UI telemetryとCustom UIのAPI記述、cold boot・日本語表示・journald・underrun復旧の確認範囲を更新した。

この整理で新たな実機試験を行ったことにはしない。未確認事項は各Issueの完了条件として残す。
