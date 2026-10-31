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
JPEG/PNG/WebPの外形とMIMEを検査し、PNG/WebPは以下の本体構造/寸法検査も行う。実decodeはViewが確認する。
provider寸法を画像本体の検証結果とは扱わない。

<a id="image-admission-49"></a>
### #49の限定修正契約（2026-10-31、PNG/WebP先行・JPEG未完了）

画像本体の寸法を公開前に検査する。圧縮上限だけではdecode時の資源を制限できないため、
JPEG/PNG/WebPの単一静止画について各辺1〜4096、総画素8,388,608以下を受入上限とする。
3840×2160を含むが、4 bytes/pixel換算では1面最大32 MiB相当であり、decoder一時領域、
browser cache、複数画像・crossfade・GPUのcopyを含むprocess全体のmemory上限ではない。
runtimeのencoded上限8 MiB/枚・最大3枚/合計16 MiBは維持する。
この共通目標のうち、現行実装はPNG/WebPに適用する。JPEGの外形確認を同等の寸法admissionとみなさない。

provider metadataではなく実byte列を判定の正とする。寸法を取得できない、0/上限超過、
切断・不正な長さ、重複/矛盾する寸法、未対応の画像構造は拒否し、decode結果を推定して通さない。
JPEGは8-bitのbaseline/extended sequential/progressive、PNGは8-bit以下、WebPは静止画を対象とする。
APNG/WebP animationとJPEGの途中寸法変更等は、frame数や展開予算を保証できないため今回は拒否する。
これは独自の画像decoder追加ではなく構造/寸法のadmissionであり、完全decodeと表示失敗の確認はViewに残す。
切断・不正長の拒否は構造/headerから検出可能な範囲であり、圧縮payload全体の正当性を保証するものではない。
対応形式の安全な構造解析が確定できない場合は、先に契約を見直してから実装する。

network応答、fresh cache、stale/offline fallbackの全経路に同じ検査を適用する。
不適合cacheを破棄し、古いcacheという理由で再公開しない。拒否は写真候補の失敗として扱い、
既存の次候補・cover/default fallbackを使う。自動retry、追加provider、disk cacheのruntime採用は追加しない。

受入試験は正常な静止画、各辺/総画素の境界と超過、申告と本体の不一致、0/切断/不正長、
重複・矛盾、animation、fresh/stale cache、取消、MIME/URL/encoded上限の既存回帰を含める。
実際に対応形式として有効なfixtureを使い、失敗条件以外を壊して成功したことにしない。
CIのLinux/aarch64 build/CTestと、実写真・TV・音声の#51受入は分離し、この修正だけで#49全体をcloseしない。

<a id="png-admission-49"></a>
### PNG限定単位の実装と方針

本体を最後までchunk単位で走査し、名前・長さ・CRC、先頭で一意の13-byte IHDR、legalなcolor/depth、
compression/filter/interlace、PLTEの要否/長さ/順序、連続したIDAT、空で最後のIENDを確認する。
0-length IDATは有効dataと連続している場合に許容し、全IDATの結合が空の場合は拒否する。
未知critical chunkは拒否し、ancillaryは境界/名前/CRCを検査するがpayloadの意味や展開は検査しない。
`acTL`/`fcTL`/`fdAT`をchunk typeとして検出した場合は、one-frameや孤立chunkも含めて拒否する。
同じ文字列が通常のancillary payloadに含まれるだけではanimationと判定しない。

CRC不一致・IEND後のbytes・16-bit・animationの拒否は、この製品の保守的なadmission方針である。
全てを一般PNG規格の不正とは呼ばず、APNGのstatic fallbackも本製品では採用しない。
PNG規格全体への準拠判定やfull decodeではなく、`iCCP`/`zTXt`/`iTXt`等の展開資源はpixel数上限では保証しない。
根拠は[PNG Third Edition](https://www.w3.org/TR/2025/REC-png-3-20250624/)の
§5（chunk構造/CRC/順序）、§11.2（critical）、§11.3.6（animation）、§13（decoder/security）。
実decodeした境界fixture、回帰・sanitizer・mutation確認は[検証記録](verification.md#png-admission-20261031)を参照する。

<a id="webp-admission-49"></a>
### WebP限定単位の実装と方針

RIFFの宣言全長との完全一致、chunk境界、odd-length payload後の0 paddingを要求する。
VP8 key frameまたはVP8Lを1つだけ受け入れ、実bitstreamの寸法を評価する。VP8Xがあれば先頭・一意の
10-byte headerとcanvasを確認し、本体frameと一致を要求する。VP8Lの`alpha_is_used`はhintとして可否に使わない。
ALPHはVP8より前で一意、VP8Lと併用しない。headerの圧縮/前処理範囲と、raw形式のplane長を確認する。
ICCP/EXIF/XMPは対応flagと存在の一致を要求し、重複を拒否する。ICCPはimage/ALPHより前に限る。
unknown chunkは境界を確認してskipするが、VP8Xのない画像の先頭に置く未対応layoutは拒否する。

animation flag・ANIM/ANMF、VP8X/ALPH reserved値、VP8の非表示frame・version4以降・scale指定を拒否する。
これらや末尾data・重複metadataの拒否は対応profileを絞る製品方針で、一般readerのignore許容規定と混同しない。
pixel/ALPH/ICC/EXIF等のpayload全体はdecodeせず、full decodeや全process資源の保証をしない。
根拠は[WebP container](https://developers.google.com/speed/webp/docs/riff_container)、
[VP8L bitstream](https://developers.google.com/speed/webp/docs/webp_lossless_bitstream_specification)、
[VP8 frame header](https://www.rfc-editor.org/rfc/rfc6386.html#section-9.1)を参照する。
fixtureは`tests/fixtures/artist-background-webp/generate.cpp`から既存libwebp/muxで生成した合成画像で、
generatorは通常build/CTestの依存にしない。再生成・native decode・Linux試験の条件は
[検証記録](verification.md#webp-admission-20261031)に記載する。

### 次のJPEG単位の受入profile（実装前）

SOF0/1/2の8-bit Huffman DCTを対象とし、quantization tableも8-bit版だけに限定する。
sample precisionとquantizer precisionは別の値であり、16-bit DQTを12-bit画像と誤分類しない。
低品質のlibjpeg出力がnative decoderで読めても、T.81(09/92) B.2.4.1との区別をせず規格適合の証明にしない。
広いquantizerの互換受入は今回採用せず、SOF1正例はHuffman table 2と8-bit DQTを使って生成・decode確認する。
後続SOF/DNL/hierarchyと非8-bit processを拒否し、progressiveの複数SOS・DHT再定義・entropy stuffing/RSTを
構造試験へ含める。この段階ではJPEGの製品検査はまだ実装していない。

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
