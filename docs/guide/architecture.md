# 状態を持つdaemon

CDは正常に再生している一方で、MusicBrainzへの通信だけ失敗していることがある。
また、トレイが準備中でも直前のdisc情報をすぐに捨てるべきとは限らない。
そのためPiCDPlayerは再生、media、metadataを別々の状態として持つ。

PlayerControllerは操作の意味と再生位置を管理する。MediaStateTrackerはdriveの観測を
media状態へ変換し、MetadataSessionは曲名の候補と取得状態を管理する。
これらを更新するのはmain threadである。CECやHTTP、将来のUIは操作を渡し、
公開された状態を読む。

CDのioctlやネットワークは、呼び出してもすぐに戻るとは限らない。そこでPCM取得、media操作、
metadata通信をworkerへ分ける。workerは結果を返し、mainが現在の状態へ適用する。
mainが読み取り完了を待たないことが、音声出力の進行確認やリモコン応答を続ける条件になる。

遅い処理が戻るまでに利用者が別の曲へ移ることもある。世代番号は、その結果がまだ必要かを
判断するために使う。古いPCMや、取り出したdiscのmetadataを現在の再生へ適用しない。
処理を強制中断できるかどうかと、戻ってきた結果を採用してよいかは別の問題である。

Coreはdrive/media/disc/TOC、playback/read/integrity、CEC、audio outputだけを扱う。外部
MusicBrainz/CAA、HTTP、cache、画像検証は任意のEnrichmentに閉じ込める。`EnrichmentService`は
TOCを入力に非同期結果を返すだけで、PlayerControllerやPlaybackEngineを変更しない。

UIはprovider固有の内部結果ではなく、Core factsと任意Enrichmentから組み立てるversionedな
Presentation Modelを読む。通信が戻ったら現在のPresentation Modelから表示を復元でき、
過去のeventをすべて受け取ったことを前提にしない。provider ID、外部URL、cache pathはUI契約に含めない。

正確な所有権とライフサイクルは[基本設計](../design/basic-design.md)、
世代と処理順序は[詳細設計](../design/detailed-design.md)を参照する。
成立の経緯は[controller](../history/player-controller.md)と
[snapshot](../history/daemon-state.md)の記録にある。

前：[装置と接続](hardware.md) / 次：[CD-DAが音になるまで](playback.md)
