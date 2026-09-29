# Repeat overlap通常CD再生確認（#9）

2026-09-28、Pi 3 Model BとASUS SDRW-08D2S-U（firmware F601）で、通常の14 track Audio CDを用いて
repeat readerの短時間再生を確認した。対象revisionは`ad3cab5`（PR #117を含むmaster）である。

## 条件

- daemon/kiosk: active。loopback APIをSSH越しに操作した。
- reader: `direct`。試験前はsingle policyで停止中だった。
- drive speed: 既存の`--drive-speed-x 1`設定が有効。これは#8の設定であり、実測速度の根拠ではない。
- repeat policy: 75 CD frame、2-of-3、10,000 ms。停止中にPOSTしたため直ちにeffectiveとなった。

## 結果

`POST /api/play`は204を返した。15秒後のsnapshotではPLAYING、track 1、position 1056 frame、
effective strategy `direct+repeat-2of3`だった。19 read callで1,425 frameを採用し、各readは2 candidate
attemptで`MULTIPLE_MATCH`となった。最新readとcurrent playbackの双方で、15 frame overlapは`MATCHED`、
`overlap_frames_compared=15`、`mismatches=0`、`read_independence=CACHE_POSSIBLE`だった。read error、
verification failure、underrunはこのrunで観測しなかった。

`POST /api/stop`は204を返し、STOPPEDへ戻った。試験後、同じAPIでpolicyをsingleへ復元し、requested/effective
ともSINGLE、pending=falseを確認した。

同じbootのjournalには、試験開始前のservice起動直後に`main_loop_stall stage=control duration_us=74989`が一件ある。
これはrepeat再生中ではないため本runのread障害とは断定しないが、無関係な成功として除外せず記録する。

## 限界

この確認は通常CDでのAPI・daemon観測に限る。TV実表示、試聴、傷disc、drive cacheの無効化、物理的に独立した
再読、cache軽減効果、性能や長時間安定性は確認していない。`CACHE_POSSIBLE`をcache非依存性や原盤PCMとの一致へ
読み替えない。傷disc/read stallの実機評価は#146、長期runtimeは#4、性能測定は#83が担当する。
