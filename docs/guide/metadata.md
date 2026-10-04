# TOCから曲名と画像へ

Audio CDのTOCは曲の境界を表す。PiCDPlayerは既存のTOCからMusicBrainz Disc IDを計算し、
ネットワーク上の情報と照合する。MusicBrainz向けoffsetへの変換はこの境界だけで行い、
player内部の位置の意味を変更しない。

同じDisc IDに国内盤、再発盤、box setなど複数のreleaseが対応することがある。
最初に返った候補が正解とは限らないため、複数候補を曖昧なまま保持する。
一候補の場合でも、それは取得できた対応候補が一つという意味であり、
物理盤の完全な同定ではない。

metadataは再生に追加する情報である。TOCを読めた時点で再生可能にし、
曲名の通信が完了するのを待たない。通信が遅い間もCECや音声処理を進められるよう、
専用workerで取得し、mainで結果を受け取る。

取得中にdiscが変われば、届いた結果は古いかもしれない。世代とTOCを照合することで、
disc Aの曲名をdisc Bへ表示することを防ぐ。取消が間に合うことだけに正しさを依存させない。

MusicBrainzの`tracks[].position`はmedium内の順序であり、物理CDのtrack番号ではない。PiCDPlayerは
positionが1から連続し、曲数が実TOCと一致する候補だけを受け入れる。その後、TOC順に観測した物理track番号へ
対応付ける。先頭がtrack 1以外でも、曲名を番号の違うtrackへ割り当てないためである。

複数候補の場合、公開Presentation Modelの`enrichment.selection`に表示用候補と選択用世代を載せる。
選択前は従来どおりAudio CDとして再生でき、既定では候補を自動決定しない。
loopback限定の`POST /api/metadata-selection`が有効な場合、Viewはsnapshot内のsession ID、disc世代、
metadata世代、候補indexを送る。daemonは現行discとの一致を確認してから選択する。
選択は現在のdisc世代のメモリ内に限り、取り出し・再挿入時には再選択が必要となる。
選択操作が受理されても、次のsnapshotで表示反映を確認する。標準UIは複数候補があるときだけ
`Choose album`を表示し、CECの方向/決定/戻るまたはkeyboardで任意にpickerを操作できる。
選択後も複数候補であることと現在の選択番号を示し、`Change album`から取り出さずに選び直せる。
別候補の適用も現行disc世代の選択APIで行い、表示は次のsnapshotを正とする。
候補には取得できたtitle/artist、date、country、medium情報を表示し、欠損値を推測しない。

Artist Background向けのArtist MBIDは、選択済みreleaseのartist-creditに含まれるartist IDから
Enrichment内部で判定する。単一の有効なMBIDが得られた場合だけ`AVAILABLE`とし、複数の異なるIDは
`AMBIGUOUS`、ID欠損・不正形式・MusicBrainzのVarious Artists IDは`UNAVAILABLE`とする。
候補未選択やdisc交換後も`UNAVAILABLE`であり、名前からIDを推測しない。artist-creditの表示名と
join phraseは従来どおりalbum artist表示に使い、background取得のidentityとは区別する。
この段階ではMBIDをPresentation Modelへ直接公開しない。Viewへ渡す画像情報の契約は後続Issueで定義する。

画像はさらに別の取得段階である。単一候補または明示選択されたreleaseについてCover Art Archiveへ
問い合わせ、daemonがJPEG/PNG/WebPの画像bytesを上限付きで取得・検査してcacheへ保存する。
単一候補でも明示選択後でも画像取得は別workerで行い、metadataを先に表示して再生を待たせない。
metadataがAVAILABLEでも画像は当初NOT_REQUESTEDであり、後からAVAILABLE、UNAVAILABLE、ERRORに変わる。
画像失敗はmetadataのstatusを変更しない。古いdisc世代や選択に届いた画像結果は適用しない。
Now PlayingはPiCDPlayer originの`/api/presentation/artwork/cover`だけを読む。provider URLや
cache pathはUIへ公開しない。曲名だけ取得できた場合、画像取得・cache・decodeに失敗した場合も、
画像なしで表示と再生を続ける。

cacheは繰り返しの問い合わせを減らすために使うが、書き込めなくても再生は可能である。
現在の形式と制限は[機能設計](../design/functional-design.md)、
候補変換と世代照合は[詳細設計](../design/detailed-design.md)を参照する。
依存libraryやAPIを選んだ経緯は[metadataの設計記録](../history/metadata-design.md)にある。
Artist creditとVarious Artistsの識別については[MusicBrainz Artist Credits](https://musicbrainz.org/doc/Artist_Credits)と
[Various ArtistsのMusicBrainz登録](https://musicbrainz.org/artist/89ad4ac3-39f7-470e-963a-56509c546377/details)を参照する。

前：[読み取り結果について言えること](integrity.md) / [ドキュメントの案内](../README.md)
