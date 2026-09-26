# Custom UI

Now Playingとtechnical statusのHTML/CSS/JSのソースは`ui/default/`にある。CMakeがこの同じ
ファイルからdefault UIを実行ファイルへ埋め込むため、install先のファイルが壊れてもdefaultは残る。
API有効buildでは編集用コピーを`/usr/local/share/picdplayer/ui/default/`へinstallする。
標準画面の変更には再buildが必要だが、Custom UIの編集にはdaemonの再起動だけでよい。

## 導入

deploy済みのPiで、例としてユーザーのホーム配下にインストール済みUIをコピーする。
手動起動前に常駐serviceを停止し、同じdeviceを重複使用しない。

```sh
mkdir -p "$HOME/PiCDPlayer"
cp -R /usr/local/share/picdplayer/ui/default "$HOME/PiCDPlayer/ui"
/usr/local/bin/cdplayerd --player /dev/sr0 --cdda-reader direct --api-port 8080 \
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
`/api/state`と`/api/events`はprovider非依存のPresentation Modelを返す。曲名・artist・track長・
enrichment status・same-origin artwork referenceだけを表示契約とし、MusicBrainz ID、CAA URL、
候補index、cache pathには依存してはならない。coverがある場合の`artwork.cover.url`は
`/api/presentation/artwork/cover`である。CSPは外部script/style/image/provider接続を許可しない。
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

起動telemetryは任意であり、Custom UIが送信しなくても動作する。
標準UIの実装を参考にする場合は[計測の定義と限界](systemd.md#起動時間の計測cage--chromium--標準ui)を参照する。

## 標準Playerの更新方法

標準Playerはsnapshotを受け取るたび表示値を計算するが、同じ文字列・progress表示値・cover URLを
DOMへ繰り返し設定しない。これは標準UIの実装上の最適化であり、Custom UIに新しい契約を要求しない。
API/WebSocketの内容や配信頻度、再接続、任意の起動telemetryは従来どおりである。
CSS transitionやbrowserの合成処理は別に発生し得るため、DOM write削減をpaintやCPUの削減量と同一視しない。

## Custom UIの描画負荷

Pi 3の標準Playerでは、再生位置に合わせて約250 msごとに変わる進行バー幅へ
`transition: width 0.2s linear`を付けた条件で、再生中の全4 core CPU平均70.92%（54.6秒）を観測した。
同値DOM writeの削減とこのtransitionの削除を含む版では、5分間の平均が7.10%、最高温度62.3°C、
現在のthrottlingなしだった。測定時間・順序は同一でなく、2変更の寄与率も分離していない。
[変更前の条件とraw](../development/reports/2026-09-25-kiosk-baseline/README.md)と
[変更後の条件とraw](../development/reports/2026-09-26-kiosk-render-cost/README.md)を参照する。
この数値はCustom UIの性能保証や、全てのCSS transitionが遅いという意味ではない。

snapshotを受けるたびに全要素を書き換える前に、表示文字列・進行幅・画像URLなどが実際に
変わったか比較する。特に高頻度で変化する要素へlayoutを伴うtransition、全画面filter、
常時pan/zoomなどを重ねる場合は、Pi実機でCPU・温度・描画を測る。`transform`など別のCSS手法も
compositeやGPU負荷を増やし得るため、計測なしに高速と決めない。cover画像はURLが同じなら
再読込せず、変更時には古いload/error callbackが新しい画像状態を上書きしないようにする。

#61のPi 3比較では、進行バーを`width`から`transform`へ変更するとCDPでのlayoutが
39件から10件/10秒、paintが48件から12件/6秒になった。一方、CDP未接続の
全4 core CPU平均は7.23%から7.13%（各5分）で、CPU・熱の明確な改善は確認できなかった。
TVは電源ONでもPi画面を表示していない条件で、CDPのスクリーンショットにより
Player描画を確認した結果である。描画経路の改善と機器全体の負荷改善は分けて判断する。

daemonが再生状態の唯一の所有者である。表示を軽くするために再生位置、読み取り状態、Integrityの
値を推定・生成しない。更新を一つの描画機会にまとめる場合も、最新snapshotを反映し、停止・
disc交換・警告・接続断/再接続を落とさない。画面上の秒表示と進行バーは異なる表示粒度を
選べるが、APIの値そのものを黙って間引いたり、実際に観測していない値を表示したりしない。

静的なCustom UI validationはJS実行、paint、CPU使用率を検査しない。Mac/Dockerの試験に加え、
[実機負荷の手順](../development/verification.md#kiosk定常負荷の計測手順issue-52)に従い、
PiでSTOPPED/PLAYINGを分け、CDP未接続のCPU・温度・現在のthrottlingを基線として測る。
CDPのlayout/paint traceは短時間の別条件で取得し、接続による負荷を無接続値に混ぜない。
`vcgencmd get_throttled`の現在bitとboot以降の履歴bitも区別する。高温や現在の電源・thermal制限を
検出したら測定を中断する。標準Playerの結果が良くても、独自UIには同じ結果を仮定しない。

## 実機確認記録

2026-09-20、`ui/default/`を`/tmp/picdplayer-custom-ui`へコピーし、`--custom-ui`
を付けてPi上で起動した。ログに`ui: source=custom`が出力され、Audio CDの認識、TOC取得、
MusicBrainz metadataのcache hit、CEC初期化が継続することを確認した。

次に`manifest.json`を不正な文字列へ置き換えて起動したところ、
`ui: custom_disabled reason=malformed manifest, field types or text encoding`となり、
default UIへフォールバックした。この状態でもdaemon、CEC、再生系、metadata取得は停止しなかった。
manifestを`ui/default/manifest.json`から復元して再起動すると、再び`ui: source=custom`となった。

なお、カスタムUIのディレクトリに拡張子のない一時ファイルなどが残っている場合は、
manifestが正しくても`unsupported asset extension`でfallbackする。編集途中のファイルを置かず、
起動前にUIディレクトリを完成した一組にしておく。

将来の設定基盤は[ユーザー設定の拡張案](../development/user-configuration.md)を参照する。

### repeat試行根拠の追加（#35）

`read.latest/current_playback.verification`に有界な試行詳細と採用候補を追加する。
field・単位・null・互換性の定義は[機能設計](../design/functional-design.md#有界なrepeat試行根拠35の初期実装)を参照。
旧payloadの欠損は未取得として扱い、試行やcandidateを生成しない。wrapperのPCM一致は
物理再読込・cache独立性の証明ではない。stream単位の有界coverageとstream/policy識別子を追加した。詳細は機能設計の
「stream coverageと根拠の世代」を参照。disc/device世代・履歴は未完了で、#24は引き続きDraft / Blocked。
