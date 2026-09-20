# Custom UI

Now Playingとtechnical statusのHTML/CSS/JSのソースは`ui/default/`にある。CMakeがこの同じ
ファイルからdefault UIを実行ファイルへ埋め込むため、install先のファイルが壊れてもdefaultは残る。
API有効buildでは編集用コピーを`/usr/local/share/picdplayer/ui/default/`へinstallする。
標準画面の変更には再buildが必要だが、Custom UIの編集にはdaemonの再起動だけでよい。

## 導入

例としてユーザーのホーム配下にコピーする。

```sh
mkdir -p "$HOME/PiCDPlayer"
cp -R ui/default "$HOME/PiCDPlayer/ui"
./build-metadata/cdplayerd --player /dev/sr0 --cdda-reader direct --api-port 8080 \
  --custom-ui "$HOME/PiCDPlayer/ui"
```

常駐serviceが動作中の場合は先に停止する。systemdでは`/etc/default/picdplayer`の
`PICDPLAYER_EXTRA_ARGS`へ`--custom-ui /srv/picdplayer/ui`などを追加する。ディレクトリを作成・
コピーし、service userが親directoryを通過でき、UIファイルを読み取れる権限にしておく。
daemonには書き込み権限が不要である。パスに空白がある場合はsystemdの引数展開にも注意する。

Samba、SFTPなどによる共有、アクセス権限の設定はOS側の責務である。daemonは共有サービスを
起動・設定しない。`config/`や`logs/`を将来隣接させても、UI配信のrootは`ui/`だけにする。
この段階ではconfig/logファイルをその場所へ自動生成しない。

## manifestと配信契約（version 1）

```json
{
  "picdplayer_ui": 1,
  "name": "My PiCDPlayer UI",
  "requires_api": 1,
  "entry": "player.html"
}
```

両versionは整数1。nameは空でないUTF-8文字列で最大128 bytes。
entryはUI root相対の空でないHTMLファイルで、拡張子は`.html`。
`manifest.json`、entry、`player.css`、`player.js`が必須である。既存URLを保持するため
CSS/JSの名前は現行UIに合わせた。追加fieldは無視する。

| URL | 内容 |
|---|---|
| `/player`、`/player/` | 選択中のentry HTML |
| `/player.css`、`/player.js` | 選択中のCSS/JS |
| `/player/assets/example.png`等 | Custom UI root相対の読み込み済みファイル |
| `/builtin/player` | Custom UIに関係なくdefault画面 |
| `/builtin/player.css`、`/builtin/player.js` | default専用asset |
| `/debug/status` | 常にbuilt-in technical status |

Custom HTML内の参照は`/player.css`、`/player.js`、`/player/assets/...`のような絶対URLを使う。
`/player`には末尾slashがないので相対URLの解釈に注意する。API/WebSocketの契約は従来どおり
`/api/state`と`/api/events`である。API version 1はこのUI互換性の契約であり、URLにversionは付けない。
将来、破壊的API変更時にはrequires_apiとの対応を更新する。

## 検証とfallback

指定がなければdefaultを使用する。指定directoryが無い、読めない、manifestが不正、version非対応、
必須ファイル欠損などの場合もAPIは起動し、defaultを配信する。この場合ログの`ui: custom_disabled`
に原因が入り、`/player`に`CUSTOM UI DISABLED`を表示する。再生・CECのエラーにはしない。

起動時に全ファイルを上限付きで読み込み、検証完了した一組だけを採用する。HTTP処理はfilesystemを
読まない。変更反映にはdaemon再起動とbrowser再読み込み（kioskならservice再起動）を行う。
HTTP応答は`Cache-Control: no-store`。共有フォルダーで複数ファイルを更新中の起動は避ける。
同時編集の原子的なsnapshotまでは保証しない。

- ファイル名はASCII英数字、`-`、`_`、`.`とdirectory区切り`/`。`.`/`..`成分、空成分、
  絶対asset path、percent encoding、backslash等を拒否する。相対asset path上限240 bytes。
- rootの親を含むsymlinkを拒否し、directory FDに対する`openat`と`O_NOFOLLOW`で読む。
  regular fileとdirectory以外を拒否する。
- 1ファイル256 KiB、合計2 MiB、ファイルとdirectory計64件、directory深さ8まで。
  manifestのJSON深さ16、text assetのUTF-8を検査する。
- 拡張子はhtml/css/js/json/png/jpg/jpeg/webp/woff2のみ。SVG、任意の実行ファイルは対象外。

HTMLの全参照解決、JS構文・実行結果、ネットワーク画像の成功は検証しない。これらが壊れていても
静的検証は通り得る。default専用URLへアクセスするか、`--custom-ui`を外して再起動して戻す。
CSPは既存の制約を保持し、inline script/styleや外部scriptを許可しない。

Custom UIは信頼するユーザーが編集するコードである。同一originで動くJSはAPIへアクセスでき、
kioskのloopback接続では操作POSTも可能。静的検証はJavaScript sandboxや権限制限ではない。
外部UIによる大量requestやbrowserのCPU/memory消費まで、この段階で隔離・保証はしない。
壊れたファイルによるloaderエラーをdaemon起動失敗にしないことと、悪意あるコードの隔離は別である。

将来の設定基盤は[ユーザー設定の拡張案](../development/user-configuration.md)を参照する。
