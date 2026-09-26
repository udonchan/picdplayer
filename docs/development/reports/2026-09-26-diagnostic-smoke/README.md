# PR #94 / #95 統合版の通常系確認

対象commit: d8aaa21。scripts/build-container.shでLinux/aarch64 build/package生成、
CTest36/36成功後、scripts/deploy.shでPiへ導入した。Pi上ではcompileしていない。

導入前はdaemon/kioskともinactive。既存sudoersはサービス単位のstart/stop/restartだけを許可しているため、
複数サービスを一つのsystemctl呼出しへ渡す初回試行は拒否された。設定変更はせず、個別呼出しで実行した。

SSH port forwarding経由のCDPとloopback APIを使い、以下を確認した。

- /debug/statusのLive表示、session IDの取得。
- Page.reload後のLive復帰と同じsession ID。
- 既存の通常CDでPOST /api/play、診断画面のPLAYING表示。
- CDP screenshotで1920×1080の診断画面、CLEAN/current PCM、recent observationsを目視確認。
- POST /api/stop後のAPI STOPPED。
- daemon service restart後の新session IDへの自動追従とLive復帰。
- /playerへ戻し、両サービスを停止。最終状態はinactive/inactive。

assertion結果は[results.json](results.json)。画像はローカル一時ファイルだけに置き、repositoryには保存しない。
これは短い通常系smoke testであり、TV実表示・試聴・冷却/温度測定・cold boot・異常disc・長期負荷は未確認。
異常系実機評価は#96が所有し、#33等の試験記録と共有する。
