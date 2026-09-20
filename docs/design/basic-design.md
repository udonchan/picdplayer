# 基本設計

## 目的と対象

物理Audio CDを入れ、TVリモコンで操作できる据え置きプレイヤーを実現する。
開発環境はRaspberry Pi OS Lite、C++20/CMake。将来Buildrootへ移植可能な小さなnative daemonを目指す。
一般的な音楽ライブラリ管理、ripping、desktop操作を主用途にはしない。

追加の製品目標として、再生PCMについて「観測した事実、採用理由、未確認事項」を説明できるようにする。
音質や完全性を観測以上に主張しない。read成功、反復一致、原盤PCMとの一致、
出力経路のbit一致を分けて扱う。能力UNKNOWNをNOへ置き換えない。
優先順位は正確性、事実に忠実な説明、根拠の追跡、driveへの依存の抑制、能力不足時の明示的な扱い、
再生の連続性、静音性、拡張性、UIの単純さとする。
未実装のtarget構成は[読み取り信頼性の拡張案](../development/integrity-design.md)に置く。

| 要求 | 現在の状態 |
|---|---|
| CD挿入・取り出しを認識しTOCに従って再生 | 実装済み。音声のみのCDが対象 |
| HDMIへ2ch PCM出力、CECで操作 | 実装・通常動作確認済み |
| metadataなし・ネットワーク障害時にも再生可能 | metadataを独立した任意機能として実装 |
| 一回のeject操作を保持し、待機後に実行 | HTTP経由で実装。失敗はEJECT_ERRORとして公開 |
| 非rootで常駐、signalで正常終了 | systemd・signalfdを利用 |
| TVに曲名・ジャケット・位置表示 | 状態APIとブラウザ用Now Playingを実装。任意導入のChromium/Cage kiosk serviceを実装し、TV表示を確認済み、boot・継続運転は確認待ち |
| Linux起動画面を見せない家電起動 | kiosk serviceは実装。quiet boot・splash・専用imageは未実装 |
| PCMの読み取り根拠・不確実性を説明する | 観測・反復一致・snapshotを実装。確認範囲は検証状況を参照 |

## ブロック図

```mermaid
flowchart TD
    CEC[TVリモコン / Linux CEC] --> Session[main thread / player_session]
    API[HTTP操作 / 対話CLI] --> Session
    Media[MediaWorker / Linux CD-ROM ioctl] --> Session
    Session --> Controller[PlayerController / PlayerState]
    Controller --> Engine[PlaybackEngine]
    Reader[PcmWorker / CddaReader / optional反復検証] --> Queue[採用PCM + ReadEvidence]
    Queue --> Engine
    Reader --> Evidence[main: read診断 / bounded event]
    Engine --> Evidence
    Engine --> ALSA[ALSA / HDMI PCM]
    Session --> Meta[MetadataWorker / MusicBrainz / CAA / cache]
    Meta --> MS[MetadataSession]
    Controller --> Snapshot[DaemonSnapshot]
    MS --> Snapshot
    Session --> Snapshot
    Evidence --> Snapshot
    Snapshot --> Events[GET state / WebSocket events]
    Events --> Technical[読み取り専用technical status]
    Events --> NowPlaying[読み取り専用Now Playing]
    NowPlaying --> Kiosk[任意: Cage / Chromium kiosk]
```

単一process。main threadが再生・media・metadataの正規状態を所有する。
PlayerController、MediaStateTracker、MetadataSessionは別々の責務を持ち、
DaemonSnapshotはそのコピーを公開する。CEC、API、将来のUIが独自の再生状態を所有しない。

| 実行場所 | 責務 |
|---|---|
| main thread | CEC、操作適用、状態遷移、ALSA nonblocking出力、API、終了signal |
| PcmWorker 1本 | reader・任意の反復検証器の所有、blocking CD-DA read、PCMと根拠の先読み |
| MediaWorker 1本 | media観測、TOC、ejectの逐次実行 |
| MetadataWorker 任意1本 | HTTP、JSON変換、cache。PlayerStateは変更しない |

library内部の補助threadを除いた構成であり、thread poolは使わない。
O_NONBLOCKで開いたCD deviceでもioctlは待ち得るためworkerに隔離する。

UIの正本は`ui/default/`で、ビルド時にdefaultを埋め込む。ユーザー編集版は起動時に検証・
読み込みし、失敗時はdefaultへ戻す。UI選択はPlayerStateから独立し、Samba等の共有管理はOS側に置く。
Custom JSの実行結果を静的検証で保証せず、default専用URLを復旧経路として維持する。

採用PCMは区間とstream generationに対応する根拠を持つ。最新readとALSA再生headの推定を分け、
未来の先読み結果を現在の可聴区間の保証として表示しない。APIや診断画面は値の投影だけを扱う。
ReadPolicyはmainが要求値と適用値を所有し、read中の検証条件を変えず停止境界で更新する。
runtime切替は自動試験と通常CDでの状態遷移確認を済ませた。正確な契約は機能・詳細設計を参照する。

## 正規モデルと単位

DiscTocは連続した番号のTrackとleadout_lbaを持つ。開始位置はint32 LBA、長さはint64 CD frame。
1 CD frameは1/75秒、2352 bytes、588 stereo sample frames（1176個のint16 sample）である。
内部PCMは44.1 kHz・stereo・signed 16-bit host endian。
MusicBrainz向け150 frame加算はmetadata境界だけで行い、DiscTocを変更しない。

## ライフサイクル

1. 起動時はPlayerState=NO_DISC。CECの準備を状態確認しながら待つ。
2. media観測でAudio CDを検出するとTOCを取得・検証し、先頭trackのSTOPPEDになる。
3. 任意のmetadata lookupを非同期に開始する。再生操作はmetadata完了を待たない。
4. PlayでPCM先読み後にALSA出力。Pause/Seek/Track変更では古いPCMを無効化する。
5. 取り出しを観測するとTOC・metadataを無効化しNO_DISCへ戻る。
6. API ejectはEJECTINGとして要求を保持し、reader解放とmedia workerの空きを待って実行する。
7. SIGINT/SIGTERMでは音声を止め、workerをjoinして終了する。

再生状態はNO_DISC / STOPPED / PLAYING / PAUSED。
media状態はNO_DISC / LOADING / AUDIO_READY / UNSUPPORTED / EJECTING / EJECT_ERROR。
metadata状態はNOT_REQUESTED / LOADING / AVAILABLE / NOT_FOUND / AMBIGUOUS / ERROR。
これらは同じenumへ統合しない。PLAYINGとmetadata ERRORは同時に成立する。

## 終了・障害境界

CD読み取り失敗は再生を停止しreaderを破棄する。ALSA underrunだけは再bufferして復旧する。
通常のmetadata通信・解析失敗はmetadata ERRORとなり、再生を止めない。
進行中kernel ioctlの強制中断は実装しないため、終了やejectの完了時間に上限を保証しない。
systemdは停止期限30秒を持つが、割り込み不能なkernel待ちを即時解消する保証はない。

将来課題は[検証状況と残課題](../development/verification.md)を参照する。
