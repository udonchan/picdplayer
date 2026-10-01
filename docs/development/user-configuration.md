# ユーザー設定基盤の拡張案（未実装）

第一段階のCustom UIは[運用契約](../manual/custom-ui.md)を参照する。ここは将来案であり、
設定ファイル、Settings UI、設定API、hot reloadはまだ存在しない。

## 現行の設定項目と画面候補（2026-10-01）

現行実装を基準とした棚卸しであり、この表だけでSettings UIや永続化を実装済みとは扱わない。
`/etc/default/picdplayer`の`PICDPLAYER_EXTRA_ARGS`はdaemon起動引数を渡す運用者向け経路である。

| 項目 | 現行経路 | 設定画面での扱い | 適用・制約 |
|---|---|---|---|
| Read Policy（SINGLE/REPEAT、region、matches、attempts、budget） | `--read-verification`は起動時、5項目は`GET/POST /api/read-policy` | 最初の画面候補。基本modeと詳細値を分け、requested/effective/pendingを表示 | 再生中の変更は停止境界までpending。API変更は再起動後に残らない |
| Artist Backgroundの有効/無効 | 未実装。開発中のprovider key fileは起動オプション | 明示的な任意有効化候補。既定OFF、利用可能になるまで操作不能と理由を示す | #49/#51のruntime配信・表示と権利/利用条件確認が前提。選択だけで写真利用許諾が生じるわけではない |
| metadata照会 | `--metadata off\|musicbrainz` | 後続候補 | network利用と再起動境界を要設計。Artist Backgroundはartist MBIDがなければ利用不可 |
| drive速度要求 | `--drive-speed-x 1..255` | 後続の詳細設定候補 | 停止/一時停止中に要求。受理は実測速度や騒音低下を保証しない |
| direct C2 pointer要求 | `--direct-c2-pointers` | 後続の詳細設定候補 | direct backend限定。能力・downgradeを表示し、C2の有無を読取保証とみなさない |
| buffer容量/startup量 | `--read-buffer-frames`、`--startup-buffer-frames` | 後続候補 | 15 frame刻み、15..2250、startup≦capacity。現状は起動時設定 |
| CDDA reader | `--cdda-reader direct\|paranoia` | 初期画面から除外 | build optionとライセンス・reader再生成に関わる運用者設定 |
| audio/CEC/CD device、audio latency、API listen/port、Custom UI path | 起動引数とsystemd environment | 初期画面から除外 | 誤設定で操作不能になり得る機器/運用者設定。rollbackと再起動境界が必要 |
| metadata cache path、Artist Background API key file | 起動引数または開発中の起動引数 | 初期画面から除外 | cacheの保存先とsecretの管理はUI表示設定から分離し、keyをAPIへ返さない |
| diagnostic/probe/fixture flag | 起動引数 | 除外 | 通常の利用者設定ではない |

画面へ載せる最初の実用範囲は、既存APIのRead Policyと、#49/#51が利用可能になった後の
Artist Backgroundの明示的なON/OFFである。画面はdaemonが返す状態だけを表示し、
保存・検証・適用の唯一のownerはdaemonとする。背景がOFFのときは外部写真の取得・配信・
表示を行わず、Album Artworkまたは既定背景へのfallbackを維持する。外部写真を配布物へ同梱しない。
家庭内での利用を想定しても、providerのAPI条件や画像ごとの権利・表示条件は別に確認する。

設定の優先順はbuilt-in defaults → system/device configuration → user configurationを候補とする。
現行CLIと`/etc/default/picdplayer`は維持し、将来のCLI上書き順位も導入時に決める。
TOMLは候補で、parserや追加依存は未採用。`secure`等の未実装modeを受け付ける予定仕様にはしない。

## #41で確定すべき契約

画面実装に先立って#41で以下を決め、機能設計へ移す。上記の優先順はまだ採用済みの仕様ではない。

- 利用者設定で上書きできる項目と、運用者のCLI/systemd設定が優先する項目を個別に決める。
  特に現行`--read-verification`と保存済みRead Policyの競合を未定義のまま実装しない。
- 保存先は再生成可能なcacheと分離し、service userだけが書ける領域とする。`StateDirectory`の
  利用を候補とし、read-only rootを将来採用しても状態領域を分離できるようにする。
- schema version、未知field、旧版、破損、不完全なwrite、容量上限、権限不足の動作を決める。
  受理済みの設定が保存に失敗したら「保存成功」と応答せず、現行effective値を維持する。
- 書き込みは同じfilesystem内のtemporary file、sync、atomic rename等で不完全な本体を見せない。
  異常停止後の復元可能性と、失敗時の安全な既定値を試験する。
- Read Policyは現行のrequested/effective/pendingと停止境界を維持する。Artist Backgroundの
  ON/OFFは画像取得を始める前に反映する。起動時にprovider keyがあっても利用者の明示ONがなければOFF。
- 表示不能・取得不能・未確認の権利条件ではONを成功として返さない。metadataやproviderの失敗が
  再生を止めないこと、Custom UIが設定機能を実装しなくても動くことを確認する。

設定の読み込み、型・範囲・組合せvalidation、適用を分離する。CEC/audio/CD device/networkは
操作不能につながり得るので、起動時fallback、last-known-good保存、適用確認とrollbackを項目別に設計する。
現行ReadPolicyのrequested/effective/pendingと停止境界の契約を再利用できるか検討する。

Settings UI → configuration API → validated configuration → 永続化 → 適用結果公開とする。
daemonが唯一の設定ownerであり、UIはTOMLを直接書き換えない。書き込み権限、原子的保存、
再起動が必要な項目、失敗時の通知をAPI追加時に決める。再生設定と表示だけの設定を同一の
即時適用として扱わない。

ユーザー領域は`/srv/picdplayer/{config,ui,logs}`等を候補とし、read-only root構成では別の書き込み
領域をmountする。cacheは再生成可能な`/var/cache/picdplayer`と区別する。Samba管理、共有ユーザー、
SFTP、USB importはOS/installerの責務でありloaderの依存にしない。

将来のruntime smoke testはHTML/JSロード、runtime error、API/WebSocket、NO_DISC描画を候補とする。
hot reloadを入れるなら、変更検知 → 一組として検証 → 採用 → browser更新とし、不完全な保存途中の
UIを配信しない。現在は起動時ロードだけであり、watcher、package installer、marketplaceは対象外。
