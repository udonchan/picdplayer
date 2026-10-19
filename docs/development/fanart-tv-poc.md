# fanart.tvローカル開発PoC

## 目的と境界

[#229](https://github.com/udonchan/picdplayer/issues/229)は、開発者が端末へ設定したProject API Keyを使い、
MusicBrainz Artist MBIDから取得した写真を標準Playerへ表示するPoCである。
fanart.tvの正式採用、公開バイナリ・bootable imageへのkey同梱、認証基盤の完成を意味しない。
中央proxy、Personal API Key、追加provider、key入力UIは実装しない。外部写真も配布物へ同梱しない。

2026-10-17のIssue整理で#229と旧親#25のtrackingをNot planned/supersededとして終了した。
PoCの全条件を合格にしたのではない。実写真切替・時間条件・TV可読性・取得切替中の音声/代表CECは
[#51](https://github.com/udonchan/picdplayer/issues/51)、通常継続運転は#184、定量性能は#83、
decoded資源安全性は#49、背景保存/次回復元は#177、Settings画面受入は#178へ一本化する。
以下の過去の検証・未確認・権利条件は保持し、基本再生報告を全条件の検証へ読み替えない。

WPEを主系とし、最新masterで採用済みのWPE 2.54専用WPEPlatform/DRM launcherを使う。
これはCog経由ではない。Cogへの移行作業を追加せず、HTML/CSS/JavaScriptのWeb標準機能と
既存HTTP/WebSocketを使用する。Chromiumは既存の復旧・バックアップ経路だけとする。

## ローカルkey設定

起動時に一度だけ次の順序で読む。設定変更にはdaemon再起動が必要である。

1. `--artist-background-key-file PATH`を明示した場合はそのfileだけを読む。不正・読込不能でも環境変数へ切り替えない。
2. file未指定の場合だけ`FANART_TV_API_KEY`を読む。未設定・空ならfanart.tvは利用不能となる。
3. keyは1〜256 bytesの空白・制御文字を含まないASCIIとする。不正な設定は秘密情報をlogへ出さず無効化する。

環境変数はローカル開発用経路であり、端末所有者・同一user・rootから完全に秘匿するものではない。
shell history、`env`、`set -x`、process dump、診断bundleへkeyを含めない。browserには環境変数を渡さない。
例としてPiのbashで、既存serviceを停止してから非rootの開発用daemonへ渡す場合は次のようにする。
device・ALSA引数は[systemd手順](../manual/systemd.md)で確認した端末の値を使う。

```sh
sudo systemctl stop picdplayer.service
read -r -s -p 'fanart.tv Project API Key: ' FANART_TV_API_KEY
printf '\n'
export FANART_TV_API_KEY
/usr/local/bin/cdplayerd --player /dev/sr0 --cdda-reader direct --metadata musicbrainz --api-port 8080
unset FANART_TV_API_KEY
sudo systemctl start picdplayer.service
```

key値をコマンド引数へ書かない。上記daemonを終了した後は必ず環境変数をunsetする。
WPE kioskは別serviceとして起動し、そのunitや`/etc/default/picdplayer-kiosk`へkeyを設定しない。
ローカル設定fileを使う場合、repository直下の`.fanart-tv.env`はGit ignore対象だが、任意の名前のfileまで
保護されるわけではない。mode `0600`とし、`git check-ignore .fanart-tv.env`とstage対象を確認する。
ignoreは既に追跡済みのfileや強制addを保護しない。key値をbuild環境・CI・releaseへ渡す必要はない。

常駐serviceでは既存のkey file方式を推奨する。`/etc/picdplayer/fanart-api-key`など、repository外の
root所有・group `picdplayer`・mode `0640`の通常fileを使用し、`PICDPLAYER_EXTRA_ARGS`へ
`--artist-background-key-file /etc/picdplayer/fanart-api-key`だけを追加する。
symlink、group書込、otherの全権限を拒否する。file末尾の改行1文字は許可するが環境変数の改行は拒否する。
service用EnvironmentFileにkey値を記載しない。

有効なkeyがあるだけでは取得しない。標準PlayerのSettingsからArtist Backgroundを明示ONにする。
保存先なしでは設定はsession限りで起動時はOFF。#177の開発差分では`--settings-file`指定時だけ
利用者の背景要求値も保存・復元する。保存ONでも起動時key不在等では要求だけ保持し実効OFFとなる。
新規ON操作はkey不在・不正・metadata無効では受理しない。保存・OFF未保存の契約は
[機能設計](../design/functional-design.md#設定の所有保存適用境界)を参照する。
ON受理は認証成功、写真の到着、画像の利用許諾を保証しない。

## 取得と表示

Enrichmentが選択済みreleaseの一意なArtist MBIDを使う。曖昧なartist、Various Artists、ID欠落は
写真を取得しない。Player coreのTOC/PCM/ALSA経路やbrowser JavaScriptへ認証処理を追加しない。
ネイティブHTTPが`https://webservice.fanart.tv/v3.2/music/{Artist MBID}`へ`api-key` headerを送る。
keyをURLへ含めず、認証付きredirectを拒否し、asset CDNへkeyを転送しない。
Artist MBID一致と`artistbackground`型はAPIのJSONで検証し、画像URLのdirectory名から推測しない。
2026-10-14の実APIは全13件が`https://assets.fanart.tv/fanart/`直下のflat画像を返した。
公式文書の例にあるmusic階層と、この確認済みflat形式のみを許可する。HTTPS・正確なCDN host・
2048 bytes以下・安全なpath segmentを要求し、HTTP、別host、query/fragment、空segment、traversal、
percent escapeは拒否する。keyは画像取得へ渡さず、redirect/public peer/MIME/bytesの検査も維持する。

候補JSONと画像は別workerで処理し、取得済み画像を段階的に公開する。現行runtimeはmemory-onlyで、
既存cache部品へ空pathを渡す。新たなディスクcacheの採否・保持期間・削除方針は決めない。
候補JSONは512 KiB、最大32候補。重複画像を除き、画像は1枚8 MiB以下、保持は最大3枚・合計16 MiB。
これは圧縮bytesの上限であり、完全decode後のmemoryやbrowser/GPU memory上限ではない。
JPEG/PNG/WebPの外形とMIMEを検査し、実decodeはViewが確認する。provider寸法を画像本体の検証結果とは扱わない。

公開modelは同一originの不透明URLだけを渡す。API契約は[メッセージ契約](../design/message-contract.md)を正とする。
標準UIは1枚なら静止、複数ならshuffleで45秒滞在・4秒crossfadeする。
追加画像の到着・同じsnapshotでは現在画像やtimerを最初からやり直さない。
`prefers-reduced-motion`ではfadeを即時にする。画像失敗時は使用可能な写真を維持し、全て失敗なら
ジャケットのブラー背景、さらに既定背景へfallbackする。再生位置やIntegrity測定とは同期しない。
OFF、disc/artist変更、候補辞退で旧参照・timer・遅延結果を失効させる。Custom UIへ描画を注入しない。

401、404、429、timeout、不正JSON、画像0枚、画像取得/decode失敗は再生エラーにしない。
自動retryは行わない。認証付きfanart要求と画像CDN要求は429後、別々のprocess内待機期限で
`Retry-After`以上の間隔を守り、ON/OFF・artist変更・次画像候補で迂回しない。
APIの待機は画像CDN・CAA・MusicBrainzを停止しない。欠落・不正・非正数の待機値にはPoC側の保守的な60秒を使う。
daemon再起動を跨ぐ期限保存は未実装であり、再起動して制限を回避する運用はしない。

## 公式仕様と利用条件

参照元は[API仕様](https://api.fanart.tv/)、[key取得](https://fanart.tv/get-an-api-key/)、
[Personal API](https://wiki.fanart.tv/General/personal%20api/)、[利用規約](https://fanart.tv/terms-and-conditions/)。
2026-10-14の直接取得はCloudflare 403だったため、API契約は利用者が同日に提供した公式OpenAPI本文に基づく。
v3.2、MusicBrainz Artist ID、`api-key`、`artistbackground`/`ImageV32`、429と`Retry-After`秒数を確認した。
Project tierの新規画像遅延は7日。固定の数値quotaは規定されず、keyによって制限がある場合がある。
Personal keyの`client-key`は今回使用しない。仕様説明はv3.2が寸法を返すとする一方、schemaは寸法を
`required`に含めていない。現行parserは安全上、欠落・不正な寸法を持つ候補を採用せずfallbackする。
API文書のCC BY 3.0表記を写真本体のライセンスと読み替えない。

key案内と規約の公開本文は公式WordPress RESTの
`https://fanart.tv/wp-json/wp/v2/pages?slug=get-an-api-key`および
`https://fanart.tv/wp-json/wp/v2/pages?slug=terms-and-conditions`から確認した。
不要な要求・bulk取得を避け、commercial useには書面同意が必要で、第三者向けAPIへの組込みは禁止とされる。
公開programでは利用者へfanart.tvと使用画像を案内し、artworkへの貢献を呼びかける規定がある。
標準Settingsにはfanart.tvの出典・websiteと貢献の呼びかけを記載する。個別画像のcreditや許諾を保証しない。
規約には投稿者へのattribution/royaltyなしの利用文言もあるが、各権利者の著作権留保と許可確認を同時に記載する。
投稿者への支払不要を、写真の権利者からの無条件の再掲載許諾とは扱わない。
写真の出典はfanart.tvであり、画像の著作権は各権利者に残る。利用者は用途に応じて権利条件を確認する。
API応答から個々の写真の権利者・包括的許諾を推定しない。privateなローカルPoCを明示的に禁止する条項は
取得できた本文では確認されていないが、全条件について無条件の許可を確認したものではない。

## WPE実機検証

標準の[Mac + Docker build/deploy](../manual/mac-docker-development.md)を使用する。
このPoCの検証でkeyをpackageへ注入しない。実API試験は開発者のkeyを設定した端末だけで行う。
本作業での自動試験と実機確認は[検証状況](verification.md)を参照する。
同日の実機試験でProject keyによるHTTP 200、JPEG 3枚の同一origin配信、OFFでの全参照404、
再ONでの新参照3件を確認した。同じPiのWPE rendered-page snapshotで1920×1080の写真背景描画も確認した。
これを物理TV目視・聴感、厳密な切替時間測定や長期負荷の確認とは区別する。snapshotはtmpだけに保存し、
Git/PR/配布物へ含めない。Inspectorはloopbackで一時利用し、撮影後に無効化した。
利用者から基本再生に問題が見られないとの実機報告も得た。後続CDのartist「大森靖子」は状態APIで採用1枚を
確認し、静止表示は仕様どおりだった。登録総数や他候補の除外理由をこのAPIから推定しない。

1. 既存設定を記録し、標準packageを通常のdeploy手順で導入する。daemonとkioskを別に二重起動しない。
2. `journalctl -u picdplayer-kiosk.service -b -o cat`の`kiosk_runtime`で`effective=wpe`を確認する。
   Chromiumへfallbackしている場合はWPE合格としない。専用launcher/WPE versionも既存手順で確認する。
3. key未設定でCD再生、metadata、cover、CEC操作を確認する。背景ON不可とジャケットfallbackを確認する。
4. keyを安全に設定してdaemonを再起動し、同じCDの選択済みartistでSettingsの背景をONにする。
   有効key・対象artist・ネットワーク・tier遅延が揃って初めて写真取得を確認できる。
5. `/api/state`の`artwork.background.urls`と画像GETの成功、TVの写真表示をそれぞれ確認する。
   複数画像のartistでは45秒以上観測し、緩やかな切替、前景の可読性、音声・CEC継続を確認する。
6. OFF、disc交換、metadata候補切替で旧写真が消え、旧URLが404になることを確認する。
   network切断・画像0枚・不正key時も再生とcover/default fallbackが継続することを確認する。
7. keyを含まない範囲でdaemon log、HTML/JS、REST/WS、browserの同一origin画像通信を確認する。
   browserからfanart.tvへの認証要求がないことを確認し、秘密情報入りのcaptureは保存しない。
8. 試験後は背景OFF、不要なkey設定の削除とservice設定の復元を行う。実写真・key・端末cacheをGitへaddしない。

429などの失敗注入は秘密を含まないfixtureを使用し、実サービスへ意図的に大量要求を送らない。
Linux/aarch64 buildや合成画像のHTTP/WS試験成功を、実provider認証・実WPE表示・実CD音声の確認と同一視しない。

## Future Work

- Project API Keyを公開バイナリ・bootable imageへ同梱できるか、fanart.tvへ確認する。
- 許可を得た場合のbuild時注入方式と、公開source・CI log・artifactへの混入防止を設計する。
- keyの失効・ローテーション・再起動を跨ぐrate-limit期限への対応を検討する。
- 他の画像providerとの品質、利用条件、帯域、可用性を比較する。
- 画像のライセンス、公開版でのfanart.tv案内・貢献の呼びかけ・credit表示とCustom UIへの適用を整理する。
- runtimeディスクcacheの採否、保持/削除、decoded-memory上限、WPE長期負荷を別途検証する。

実API取得・WPEでの写真表示・再生継続が未確認なら、実装PRが完成してもPoC全体の完了とは扱わない。
