# Disc Read Map layout comparison (PR #163)

Pi kiosk ChromiumのCDPから取得した1920×1080の実画面。画像はUIの比較用であり、HDMI scanoutや読み取り品質の証拠ではない。

- [変更前](before-1920x1080.png): STOPPED。以前のread historyが残る状態。円盤96px、Mapは枠付きカード。
- [変更後・再生中](after-playing-1920x1080.png): 通常CDのPLAYING。円盤160px、Mapの枠をなくし右列上段のvisual layerへ配置。点はlatest observed readで、PCM提出位置や物理ヘッド位置ではない。
- [mixed fixture](mixed-fixture-1920x1080.png): test-only `integrity_scenario_harness`のmixed状態をMac Chromeで描画。clean/retry/recovered/anomalyの複数観測が同じ円盤上にあるときも配置が崩れないことを確認した。実CDや傷の物理再現ではない。

画像の再生状態とread historyは異なるため、色の量やread品質を前後比較する資料ではない。比較対象は配置と可読性である。CDPの実寸では1920×1080時にRead Observationが826×453px、Map領域が606×193px、Drive Capabilityが606×238pxで、documentのスクロール寸法は1920×1080。1280×720では横スクロールなし（縦スクロールあり）、700×900では1列に縮退し横スクロールなし。

初回の実画面確認で、点のLBA角度は12時起点だが、観測色のCSS角度が9時起点という不一致を発見した。両方を12時起点へ合わせ、再デプロイ後の画像で点と観測色の方向一致を確認した。実機で確認したのは通常CDのみで、mixed異常状態はtest-only fixtureの自動試験とMac Chrome描画で確認した。

Docker Desktopでharnessがcontainer loopbackだけにbindするため、文書の`docker run -p`だけではMac Chromeから到達しなかった。この撮影ではcontainer内に一時的なPython TCP relayを立て、host loopbackの公開portをharness loopbackへ転送した。relayはrepository・package・Piには含めていない。既存のmanual起動例は別途修正が必要である。
