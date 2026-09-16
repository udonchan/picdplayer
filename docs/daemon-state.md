# Daemon state snapshot

## 目的

将来のREST/WebSocketとUIが同じ状態を読み、独自のplayer状態を所有しないための値境界を置く。
`DaemonSnapshot`はauthoritative ownerではなく、main threadが各ownerから作る読み取り専用の投影である。

- `PlayerController`: playback、track、絶対LBAを所有する。
- `MediaStateTracker`: drive/media lifecycleを所有する。
- `MetadataSession`: metadata状態、候補、選択結果を所有する。
- `DaemonSnapshot`: 上記の同一時点のコピーをAPIへ渡す。

snapshot生成はhardware/networkへアクセスしない。PlayerStateとTOCが矛盾する場合は受け入れず、
APIへ部分的に誤った状態を公開しない。位置と長さは内部の正規単位であるCD frameを維持する。
UI向けには絶対LBAに加え、現在トラック内の`position_in_track_frames`と
`current_track_length_frames`を計算する。millisecondへの丸めは表示層で行う。

`revision`は将来main threadが状態更新ごとに増やし、WebSocket clientが更新順序を判定するための値。
公開内容が変化した時だけ増やし、同じsnapshotの定期配信では増やさない。

`ENABLE_API=ON`では`GET /api/state`用のJSON serializerもbuildする。JSONはrevision、player、
media、TOC、metadata候補、選択release、Cover Art状態を含む。optional値は欠落させず`null`にし、
UIがmedia状態によって型を推測する必要をなくす。serializer自体はsocketを持たず、fixture testで
schemaとCD frame単位を確認する。

## HTTP読み取りPoC

libwebsockets 4.3.5をoptional dependencyとして追加した。`--api-port PORT`を明示したplayerだけが
`127.0.0.1`へlistenする。既定ではsocketを作らない。headerは2 KiB、同時header poolは4、
service bufferは4 KiB、state responseは1 MiBを上限とする。未知pathは404、GET以外は405。

デバッグ時は`--api-listen 0.0.0.0`またはPiの数値LANアドレスを明示して外部から照会できる。
hostnameは受け付けず、意図しない名前解決を行わない。認証・TLS・操作APIはまだないため、
信頼できる開発用LANだけで使用し、port forwardingやインターネット公開は行わない。

libwebsocketsの追加threadは作らず、player loopが`lws_service(context, 0)`を呼ぶ。JSONはmain threadで
250 msごとに状態変化を確認し、HTTP callbackは完成済み文字列を返すだけにする。
`WS /api/events`は接続直後と公開内容の変化時に同じstate JSONをtext messageで送る。
送信頻度は最大4 Hzとし、より細かい位置更新でclientを圧迫しない。clientからのmessageは受け付けず、
操作APIとauthoritative stateの境界を混在させない。

loopbackではbodyなしの`POST /api/play|pause|stop|next|previous`を受け付ける。HTTP callbackもmain
thread上で動くため、handlerはPlayerControllerへ直接commandを適用し、変更時にPlaybackEngineを
synchronizeする。discがなければ409、成功は204を返す。外部debug listenではcommand handler自体を
渡さず403とし、認証がない状態でLAN上から操作できないようにする。seek、track選択、ejectはrequest
bodyとmedia worker連携の仕様を決めてから追加する。

routeのmethod/path/size上限をhardwareなしで試験し、実loopback socketへHTTP/1.1 GETを送って
200とJSON bodyを確認した。sandboxではsocket作成が制限されるため、この統合テストはloopbackを
許可した環境で実行する必要がある。

2026-09-16にMacからPiのLAN address `192.168.1.2:8080`へ問い合わせ、disc、player、metadataを
含むJSON responseを取得できることを実機確認した。外部listenは引き続き明示optionの場合だけ有効。

## HTTP server候補

実機のRaspberry Pi OSでは2026-09-16時点でHTTP server開発libraryは未導入。
aptには`libcpp-httplib-dev`、`libmicrohttpd-dev`、`libwebsockets-dev`がある。

- cpp-httplib: C++から小さくHTTPを始めやすいが、WebSocketは別経路が必要。
- libmicrohttpd: 成熟したHTTP C libraryだが、WebSocketは別経路が必要。
- libwebsockets: HTTPとWebSocketを同じevent loopで扱える。C APIとlifecycle管理の実装量は増える。
- Boost.Beast: HTTP/WebSocketを扱えるが、PiCDPlayerの現状にはBoost依存が大きい。

Buildroot本流にはlibwebsockets packageがあり、Raspberry Pi OSのapt候補は4.3.5系だった。
HTTPとWebSocketを一つのoptional dependencyで扱え、Buildroot移植時に独自package追加が不要なため、
API PoCではlibwebsocketsを第一候補とする。
[Buildroot package](https://github.com/buildroot/buildroot/tree/master/package/libwebsockets)、
[upstream minimal examples](https://github.com/warmcat/libwebsockets/tree/main/minimal-examples)

main threadから短いnon-blocking service処理を呼び、CEC/playbackと同じauthoritative threadで
requestをcontrollerへ適用する案を先に検証する。別threadからPlayerControllerを操作しない。
listenは開発中も既定をloopbackとし、LAN公開は明示optionにする。request bodyと接続数へ小さな
上限を設ける。外部libraryを選ぶ前に自前HTTP parserは実装しない。
