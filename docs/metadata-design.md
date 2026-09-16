# Metadata subsystem設計案

状態: Phase 1〜6の最小実装を完了。実CDのDisc IDでMusicBrainzとCover Art Archiveの
live lookup、14曲の内部model変換、raw JSON cache hitを確認済み。
2026-09-16にrepositoryと公式資料を調査。Phase 2以降の型・上限・dependencyは提案。

## 現状と維持する境界

基準commitは30e72b6。追跡ファイルに変更はなく、旧PoC文書
`docs/cdda-streaming-poc.md`だけが未追跡だった。

- `CdDevice`: Linux ioctlによるmedia/disc状態とTOC取得。
- `DiscToc`: hardware非依存。track番号は1固定でなく、位置はint32 LBA、長さはint64 CD frame。
- `MediaStateTracker`: 観測からmedia状態への変換。PlayerStateを所有しない。
- `MediaWorker`: status/TOCのblocking処理を専用threadで直列実行。
- `player_session.cpp`: media結果を受け、TOC検証後にPlayerControllerへdiscを設定。
- `PlayerController`: main threadが所有する唯一の再生状態。
- `PcmWorker` / `PlaybackEngine`: CD-DA取得、先読み、ALSA出力とunderrun復旧。
- CEC・signal・ALSA進捗はmain loopで処理する。

現在HTTP client/JSON libraryはない。ネットワークをMediaWorkerやPcmWorkerへ追加しない。
TOC取得と再生可能化を先に完了し、metadata処理の成否で再生状態を変更しない。
systemd起動にnetwork-onlineを必須条件として追加しない。

## 構成案

```text
CdDevice -> DiscToc -> PlayerController -> PlaybackEngine（既存）
                 |
                 +-> MetadataSession（main threadが状態を所有）
                       |
                       +-> MetadataWorker（1 thread、bounded queue）
                             +-> TOC変換 / libdiscid
                             +-> MusicBrainzClient -> HttpClient(libcurl)
                             +-> JSON変換 -> release candidates
                             +-> 選択済みreleaseのCoverArtClient
```

`MetadataSession`はNOT_REQUESTED / LOADING / AVAILABLE / NOT_FOUND /
AMBIGUOUS / ERROR、候補、選択結果、診断情報を保持する小さなclassとする。
PlayerStateへmetadata errorを追加しない。将来APIでは両snapshotを組み合わせる。
workerは値を返すだけで、PlayerControllerとMetadataSessionを書き換えない。
Disc ID生成、curl初期化、worker生成・request投入・結果変換の例外もmetadata境界で扱う。
通常のmetadata失敗をmainの終了経路へ漏らさず、再生可能化の後にERRORとして報告する。

## Disc IDとTOC

MusicBrainzのDisc ID計算はfirst/last trackを各2桁、lead-outと99 track offsetsを
各8桁の大文字16進ASCIIにし、804 bytesをSHA-1へ入力する。存在しないtrackのslotは0。
20 bytesのhashをBase64化し、通常の`+ / =`を`. _ -`へ置き換える。結果は28文字。
実track番号でslotを埋める。lead-outを含めLBAに150を足し、内部DiscToc自体は変えない。
加算は64bitで行い、libraryのint範囲と入力制限を検証してから渡す。[Disc ID仕様](https://musicbrainz.org/doc/Disc_ID_Calculation)

公式6曲example:

```text
first=1 last=6
start_lba=[0,15213,32164,46442,63264,80339]
leadout_lba=95312
TOC="1 6 95462 150 15363 32314 46592 63414 80489"
Disc ID="49HHV7Eb8UKF3aQiNmu1GR8vKTY-"
```

Disc ID計算とHTTPのfuzzy TOC queryは区別する。libdiscidのTOC文字列はfirst/lastを持つが、
Web API説明のfuzzy TOCはfirst=1、track countを前提にしている。
first!=1でもDisc IDは本来の番号で計算し、初期版ではfuzzy queryを送らない。
first=1の通常CDでは上のTOC文字列をURL encodeできる。
mixed-mode、負LBA、複数sessionは現行player同様に対象外。

方式比較:

|方式|利点|負担|
|---|---|---|
|自前TOC整形+既存SHA-1 library|小さな変換処理、DiscTocに合わせやすい|SHA-1/base64依存または保守が必要。libcurlのTLS backendを暗黙に直接利用できない|
|libdiscidのput API（推奨）|公式実装、TOCだけから計算、SHA-1を自前保守しない|追加shared libraryとLGPL、Buildroot外部package定義が必要|

`discid_new -> discid_put(first,last,offsets[100]) -> discid_get_id -> discid_free`をRAIIで包む。
必要なら`discid_get_toc_string`も使用。`discid_read`は呼ばない。
元TOCの連続番号・長さ等はmetadata境界でも再検証する。
libdiscidは現行DiscTocより厳しいdisc長制限を持つため、計算不可はmetadata ERRORに留める。
公式libraryはLGPL-2.1-or-later、pkg-config名は`libdiscid`、headerは`discid/discid.h`、
リンクは`-ldiscid`。[API](https://jonnyjd.github.io/libdiscid/discid_8h.html)、[library](https://musicbrainz.org/doc/libdiscid)、[pkg-config定義](https://github.com/metabrainz/libdiscid/blob/master/libdiscid.pc.in)

## MusicBrainz requestと候補

初期版はexact Disc ID lookupを行う。

```text
GET https://musicbrainz.org/ws/2/discid/{id}
    ?fmt=json&cdstubs=no
    &inc=recordings+artist-credits+release-groups+discids+labels
```

`toc`を付けるとDisc ID未登録時にfuzzy lookupへ移るため、初期版では送らず診断表示だけにする。
CD stubは無効化。将来fuzzyを追加する場合は明示的な別requestとし、match_kindを保持する。
通常の公開metadata GETに認証は不要。Disc ID lookupは複数releaseを返し、paging非対応。
サイズ上限超過を候補の一部だけで成功扱いにしない。[MusicBrainz API](https://musicbrainz.org/doc/MusicBrainz_API)

responseのrelease全体ではなく、各`media`から要求Disc IDを持つmediumを探す。
候補identityは`(release_id, medium_position)`。同一release内の複数mediumも区別する。
track-count、tracksのposition、discsのoffsetsがあれば実TOCとの整合を確認する。
曲のnumberは文字列であり、CDの整数track番号へ無条件にparseしない。
配列順でもなくmedium内positionとTOC順序を検証して対応付ける。

- 0候補: NOT_FOUND。HTTP障害・不正JSON・schema不一致とは分ける。
- 1候補: 完全な結果で対応付けを検証できればAVAILABLE、選択理由をsingle_exactとして記録。
- 複数候補: AMBIGUOUS。先頭を選ばず、Track番号表示を継続。
- 候補が壊れている場合: 黙って除外して残り1件を確定しない。初期版はERRORとする。

1候補も物理盤の同定保証ではなく「現時点で得られた唯一の対応候補」である。
将来は国・発売日・label/catalog・medium位置・title/artistを候補選択画面へ表示する。
曲数は整合条件、track lengthは比較材料だが、metadataのms長でTOCを書き換えない。
country/date/cover有無だけで盤を確定しない。
artist-creditは表示名とjoinphraseを順番に連結する。
track title/artistを優先し、必要時のみrecording情報へfallbackする。
[JSON例（複数medium、track/recording/artist-credit）](https://musicbrainz.org/doc/MusicBrainz_API/Examples)

## 内部model案

|型|主なfield|
|---|---|
|TrackMetadata|実CD track_number、optional title/artist、optional recording_id、比較用source_length_ms|
|DiscMetadata|album_title/album_artist、tracks、source="musicbrainz"、release_id、optional release_group_id、medium_position/title、country/date、label/catalog、cover availability hint|
|ReleaseCandidate|DiscMetadata、match_kind、TOC対応付けの検証情報|
|MetadataSnapshot|status、disc generation、disc ID、candidates、optional selected candidate key、error code|
|ArtworkSnapshot|NOT_REQUESTED/LOADING/AVAILABLE/UNAVAILABLE/ERROR、release_id、source reference、optional取得済み画像reference|

country/date等は欠落を表せる型にする。dateは年だけ・年月だけもあるため無理に完全日付にしない。
画像bytesやJSON DOMをPlayerStateへ入れない。曲長と再生位置の正規情報はDiscToc/PlayerState。
cover availability hint（MBが報告した有無）と実際のdownload結果は別の情報。

## Workerとdisc交換

MediaWorkerと似たrequest/pop形の専用workerを1本追加する。
同時HTTP requestは1件、pendingは最新1件、結果もboundedにする。
network/JSON変換はmutexを解放して行い、mainは短いrequest/result移動だけを行う。
HTTPの差し替え用小interfaceまたはcallbackでfixture試験を可能にし、factory frameworkは作らない。

request/resultに`generation + Disc ID + TOC snapshot`を持たせる。
世代は取り出し・unsupported・新しいTOC受理で更新し、seek/pauseでは変えない。
同じCDの再挿入でも別世代。loadingへ移った時点で進行中結果を無効化し、TOC再確認まで
新規結果を適用しない。同じTOCが戻っても新しいrequestとして扱う。
mainはmedia結果を先に反映してからmetadata結果を検査する。
generationとTOC identityが現在discに一致した場合だけ適用する。
artwork結果はさらに選択release/mediumに対応するselection世代を確認する。

古いrequestはcurl progress callbackで中断するが、中断成功に正しさを依存させない。
完了済みの古い結果もmainで破棄する。終了時はcancel、待機解除、joinする。
再生出力の停止をworker joinより先に行う。

現行media監視はPLAYING中に新規status要求を止める。物理的な交換がまだ観測されていない
期間や、同じTOCの別discを完全に識別する保証はない。これはDisc IDだけでは解決しない。
今回保証するのは、観測済みの交換・新TOCへ古いrequestを適用しないこと。
metadataのためにdriveアクセスを増やす設計にはしない。

## HTTP/JSON dependency

HTTPはlibcurl easy APIを専用workerで使う案を推奨する。
Boost.BeastはAsio・TLS接続管理等の実装量が今回の逐次GETには大きい。
cpp-httplibは小さくC++向けだがTLS依存は残り、curlの制限・redirect・取消機構を使う方が自然。
HTTP/TLS自前実装は採用しない。
[Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/introduction.html)、[cpp-httplib](https://github.com/yhirose/cpp-httplib)

libcurlはcurl license（MITに類似、同一ではない）。Raspberry Pi OSでは
`libcurl4-openssl-dev`、CMakeは`find_package(CURL REQUIRED)` / `CURL::libcurl`、
pkg-config名は`libcurl`。runtimeはlibcurlとTLS等の依存で、curl実行ファイルは不要。
現在のcurlは8.14.1/OpenSSL 3.5.7、AsynchDNSあり。実際のリンク先でもfeatureを確認する。
[license](https://curl.se/docs/copyright.html)、[FindCURL](https://cmake.org/cmake/help/latest/module/FindCURL.html)

worker専有handle、worker開始前のglobal初期化、`CURLOPT_NOSIGNAL=1`を使用する。
SIGPIPE対策も初期化時に扱う。DNS timeoutのため非同期resolver対応を実行時に確認し、
不足時はmetadataだけ無効化する。cancel/timeoutが同期DNSで効かない構成を避ける。
libcurl内部resolverが補助threadを作る場合はある。
[thread/signal/resolver仕様](https://curl.se/libcurl/c/threadsafe.html)

JSONはnlohmann/jsonを推奨。C++値型への変換と厳密な型検査を小さく書ける。
header-onlyだが実行コード・DOMメモリは消費する。parser専用cppにincludeを閉じ込め、
Piでは-j1でbuildする。json-cは小さなshared libraryで既にruntimeがあるが、C APIの
reference countをRAIIで包むコードが必要。さらに軽量化が必要になれば比較対象にする。
両者MIT。nlohmannのCMake targetは`nlohmann_json::nlohmann_json`。
[nlohmann CMake](https://json.nlohmann.me/integration/cmake/)、[license](https://json.nlohmann.me/home/license/)、[json-c](https://github.com/json-c/json-c)

初回調査ではlibcurl/json-c runtimeのみだった。実装開始時に`libdiscid-dev` 0.6.4、
`libcurl4-openssl-dev` 8.14.1、`nlohmann-json3-dev` 3.11.3の導入を確認した。
apt cache上のlibdiscid0 0.6.4はInstalled-Size 86 KiB、libcurl4t64は1017 KiB、
json-c runtimeは168 KiB。これはpackage配置量で、追加RAMや最終image差分の実測ではない。
必要候補: `libdiscid-dev libcurl4-openssl-dev nlohmann-json3-dev`とCA証明書。

Buildroot調査revision: `22540e0d41382a8085af110462e2aea6508da4d5`。
libcurl、json-for-modern-cpp、json-cはpackageあり。libdiscidは完全なtree一覧で未収録を確認。
採用時はBR2_EXTERNALへ小さなautotools packageとhash/license定義を追加する方針。
libcurlはTLS backendとCA bundleを明示し、不要protocolやcurl CLIを省ける。
TLS検証のため実機時計も必要。時計不正はmetadata ERRORに留める。
[Buildroot tree](https://github.com/buildroot/buildroot/tree/22540e0d41382a8085af110462e2aea6508da4d5/package)、
[libcurl](https://github.com/buildroot/buildroot/blob/master/package/libcurl/Config.in)、
[nlohmann](https://github.com/buildroot/buildroot/blob/master/package/json-for-modern-cpp/json-for-modern-cpp.mk)、
[json-c](https://github.com/buildroot/buildroot/blob/master/package/json-c/json-c.mk)

## 利用条件・通信上限案

User-Agentは`PiCDPlayer/0.1.0 (https://github.com/udonchan/picdplayer)`を既定案とし、
versionはCMakeから生成、連絡先は設定可能にする。MusicBrainzへのrequest開始間隔は
少なくとも1秒、実装値は1.1秒を案とする。連続lookup・retryも同じlimiterを通す。
metadata変更の定期pollingはしない。429/503はRetry-Afterを尊重し、少数のretry後ERROR。
待機はcancel可能とする。診断CLIとserviceの同時lookupは避ける。
[MusicBrainz利用条件](https://musicbrainz.org/doc/MusicBrainz_API/Rate_Limiting)

初期上限案: 接続5秒/request全体15秒、JSON展開後2 MiB、深さ32、文字列4 KiB、
候補100件、artwork download5 MiB、redirect5回。上限超過はERRORで、候補を黙って切り捨てない。
write callbackで実bytesを制限し、Content-Lengthだけを信用しない。
HTTP statusとContent-Typeを確認し、不正JSON/UTF-8/予期しない型を拒否、optional欠落は許容。
ログでは制御文字をescapeし、外部文字列をそのまま端末へ流さない。
HTTPSのみ、証明書/hostname検証有効。redirectもHTTPSと許可hostを検査し、
private/link-local等への転送は拒否する。C callbackからC++例外を漏らさない。

## Cover Art

選択済みreleaseだけを`https://coverartarchive.org/release/{release_id}/`で問い合わせる。
front画像の500px thumbnailを優先。CAA経由のURLからredirectを検証して取得する。
最初はJSON一覧とreference取得まで、binary保存・画像decode上限は次の小ステップにする。
binary取得段階ではMIME/形式/画像寸法上限（例4096x4096）を検証してからAVAILABLEにする。
HTMLやSVG等を画像として公開しない。UIは将来自前APIの検証済み画像referenceを使う。
CAA 404はUNAVAILABLE、通信失敗はERRORだがDiscMetadataは維持する。
release-group画像へのfallbackは異なる盤の画像になり得るため初期版では自動実施しない。

公式APIは250/500/1200px、307 redirectを規定し、固定rate limitは現在ないと記載する一方、
503も定義する。こちらは逐次・低頻度でアクセスしbackoffする。
[CAA API](https://musicbrainz.org/doc/Cover_Art_Archive/API)
画像の取得可能性は再配布licenseを意味しない。source/release referenceを保持する。
[Cover Art](https://musicbrainz.org/doc/Cover_Art)

## Cache境界

純粋なJSON変換とHTTP transportを分離し、workerが行うlookupの前後へcacheを挿入可能にする。
初期実装はraw JSONを`/var/cache/picdplayer/metadata`と`cover-art`へ保存可能にし、
read-only rootでは別の書き込み可能mountへ置く。cache書き込み失敗でもnetwork結果は利用する。
現在はraw responseをDisc ID/release IDでkey化し、読み出し時に同じparserと上限を適用する。
期限、schema version、TOC identityを含むcache manifestは今後追加する。候補と選択結果を区別し、
ユーザーの選択は消去可能cacheではなく`/var/lib/picdplayer`等の永続stateへ置く。
一時file+rename、容量/期限上限、短いnegative cacheを段階的に追加する。DBは不要。

## CMake・実装順序・試験

`ENABLE_METADATA=OFF`を既定案とする。OFFでは既存依存だけでbuild可能。
ONでlibdiscid/pkg-config、CURL、nlohmann_jsonをREQUIRED検出し、不足packageを明示する。
configure中のdownload/FetchContentは行わない。metadata library target内だけへ依存を閉じる。
runtimeは`--metadata off|musicbrainz`等で無効化できる案とする。

1. TOC変換+Disc ID、`--probe-disc-id /dev/sr0`、公式vectorとfirst!=1/overflow試験。実装済み。
2. JSON fixtureから内部候補modelへ変換。0/1/複数、joinphrase、欠落、不正型を試験。実装済み。
3. HTTP clientと`--probe-metadata /dev/sr0`、`--lookup-disc ID`を追加。実装済み。
   前者は既存TOC取得、後者はhardware不要で候補を診断する。後者だけではTOC整合を保証しない。
   raw responseは明示指定で保存する診断機能に留め、通常ログへ全量表示しない。
4. MetadataWorkerとmedia lifecycleへ統合。世代とTOCを再照合し古い結果を破棄。最小実装済み。
   A→B、A→取り出し→A、LOADING、旧error、旧artwork、終了中を含む世代試験。
5. 単一候補releaseのCAA JSON lookupとHTTPS画像reference取得を実装。画像binary保存と候補選択UIは未実装。
6. Disc ID/release ID単位の上限付きraw JSON cache、mkdir、temporary file+renameを実装。

parserをHTTPより先に作ることでlive serviceなしで正規化と曖昧性を確認できる。
全phaseでmetadata OFF/ON、paranoia OFF/ONと既存CTestを適切に確認する。
HTTP timeout、429/503、404、巨大body、深いJSON、invalid UTF-8、redirect拒否もfixture/fakeで試験。
live lookupと再生中ネットワーク障害の実機試験はユーザーにコマンド・期待結果を提示して行う。
装置の正常再生をネットワーク障害で停止させないことを完了条件に含める。

## 実機確認（2026-09-16）

ASUS SDRW-08D2S-Uの14曲Audio CDでmetadata有効playerを起動した。media認識とTOC取得後、
PlayerControllerが先に`STOPPED`となり、その後metadataが`LOADING`から`AVAILABLE`へ遷移した。
Disc IDは`6JTbUgqHL29gzUyOH5ir60K3hz0-`、候補は1件。metadata取得中もCECのpower status応答を
継続し、取得後にREGZAリモコンからPlay/Pauseでき、再生状態とmetadata状態が独立していることを
確認した。初回は`cache=miss`。診断CLIでは同じraw JSONを使った`cache=hit`と14曲の再解析も確認済み。

未確認なのは、lookup中のディスク交換、ネットワーク切断、MusicBrainzの長時間障害、複数候補CD、
CAA 404を伴う実機運転。これらは再生を止めない異常系試験として残す。現在の429/503 retryは
1.1秒間隔の最大3回で、`Retry-After` headerの解釈は未実装。

同日の外部API確認でCover Art ArchiveのJSON endpointが307を返し、artworkだけ`ERROR`になることを
確認した。CAAが仕様としてredirectを利用するため、CAA requestに限り最大3回のHTTPS redirectを
許可した。MusicBrainz requestはredirect拒否を維持する。修正後のartwork取得は次回の実機lookupで
再確認する。

shutdown時はMetadataWorkerの終了要求をlibcurl progress callbackとrate-limit待機へ伝える。
HTTP timeout/retryの完了までjoinしてsystemdの停止期限を超えることを避ける。CDDAとmediaの
kernel ioctl自体は安全に強制cancelできないため、終了時に進行中ioctlが返るまで待つ設計を維持する。
