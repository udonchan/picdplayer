# ユーザー設定基盤の拡張案（未実装）

第一段階のCustom UIは[運用契約](../manual/custom-ui.md)を参照する。ここは将来案であり、
設定ファイル、Settings UI、設定API、hot reloadはまだ存在しない。

設定の優先順はbuilt-in defaults → system/device configuration → user configurationを候補とする。
現行CLIと`/etc/default/picdplayer`は維持し、将来のCLI上書き順位も導入時に決める。
TOMLは候補で、parserや追加依存は未採用。`secure`等の未実装modeを受け付ける予定仕様にはしない。

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
