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

画像はさらに別の取得段階である。現在は単一候補のreleaseについてCover Art Archiveへ
問い合わせ、画像URLを得るところまで扱う。画像bytesの取得・検証・表示は完成していない。
曲名が取得できて画像が取得できない状態も正常に表現する。

cacheは繰り返しの問い合わせを減らすために使うが、書き込めなくても再生は可能である。
現在の形式と制限は[機能設計](../design/functional-design.md)、
候補変換と世代照合は[詳細設計](../design/detailed-design.md)を参照する。
依存libraryやAPIを選んだ経緯は[metadataの設計記録](../history/metadata-design.md)にある。

前：[読み取り結果について言えること](integrity.md) / [ドキュメントの案内](../README.md)
