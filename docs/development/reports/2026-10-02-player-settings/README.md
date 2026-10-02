# Settings画面のローカル表示確認（2026-10-02）

Draft PR #180の標準Player Settings第一段階をMacのheadless Chromeで確認した。
`/api/state`と`/api/read-policy`はローカルmock応答で、実CD・Pi・CECハードウェアは使用していない。
画面内のThe Slip、Read Policy、`persistence_configured=true`は表示確認用のfixture値であり、
実機の設定保存が成功した証拠ではない。

| viewport | capture | 確認結果 |
|---|---|---|
| 1920×1080 | [Settings](settings-1920x1080-mock.png) | Settings surface 620×660 px。document 1920×1080、surface内に縦scrollなし |
| 720×720 | [Settings](settings-720x720-mock.png) | Settings surface 620×631 px。panelはviewport内に収まり、surface内に縦scrollなし。背景のPlayer文書は1929 pxだがmodalはfixed表示 |

API/CEC/TV実表示、Chromium kiosk上のpaint・CPU・保存後のPi再起動はこの記録では未確認。
現行CEC操作コードの自動試験とDocker aarch64のCTestは[検証状況](../../verification.md)に記録した。
