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
現段階では値型と純粋な生成処理だけを実装し、共有mutex、HTTP thread、event queueは導入しない。

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
