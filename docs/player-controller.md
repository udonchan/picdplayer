# PlayerController 第1段階

backendの性能比較・採用判断は保留し、CECリモコン操作を目標にする。
状態とコマンドをhardware非依存に実装。--playerモードから実呼び出しし、
音声進捗を通知する。CEC入力も同じcontrollerへコマンドを渡す。

PlayerControllerがDiscTocのコピーとPlayerStateを所有し、state()は値のsnapshotを返す。
外部からsnapshotを書き換えてもcontrollerの状態は変わらない。
将来daemonのmain threadだけがcontrollerを操作し、CECやUIはコマンドを渡す。
workerはPlayerStateを所有・変更しない。

|操作|動作|
|---|---|
|load_disc|TOCを検証し、最初のトラックを選択してSTOPPED|
|remove_disc|NO_DISC、トラックと位置をクリア（物理ejectは行わない）|
|play|ディスクがあれば現在位置からPLAYING|
|pause|PLAYINGのときだけPAUSED|
|stop|STOPPEDにし、Track 1の先頭へ戻る|
|select_track|実トラック番号で選択。無効番号はfalse、状態不変|
|next|次トラック先頭。最終トラックでは何もしない|
|previous|曲頭から3秒未満なら前トラック先頭、3秒以上なら現在曲の先頭。最初のトラックではその先頭|
|seek_relative|CDフレーム単位の前後移動。曲境界を越え、選択トラックも更新|

選択とseekはPLAYING/PAUSED/STOPPEDを維持する。NO_DISCでは操作は何もしない。
seekは最初の開始LBA〜lead-outの1フレーム前にclampし、繰り返し再生しない。
Previousの境界は225 CDフレーム（3秒）。現在の再生位置で判断し、PAUSED/STOPPEDでも同じ規則を使う。
曲頭へ戻った直後にもう一度操作すると前の曲へ移る。長押し等はCEC入力を見て検討する。
TOCは公開構造体なのでload時に再検証し、不正なTOCでは既存状態を保持する。

PLAYINGは現在は再生要求の状態で、hardwareでの発音を保証しない。
位置は操作と音声エンジンのplayback_position通知で変わる。
読み取りcursorを再生位置として流用しない。finished通知で最初のトラックを選択してSTOPPED。
エラー時は音声エンジンがstopを呼ぶ。古いPCMの除外はengine/workerの世代番号で行う。

hardware不要のCTestで状態遷移、NO_DISC、番号3開始、曲境界、巨大なseek値、
不正TOC、media交換、snapshotの独立性を検証する。
