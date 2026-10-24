# v1.0 Raspberry Pi OS最小組込み仕様案

2026-10-22、[#46のv1.0計画](../../appliance-image-plan.md)のV0机上調査とB0読取り検査。
対象はRaspberry Pi OSだけとし、Buildroot等のPending作業は再開していない。
V0後の利用者の続行指示で、B0の一回のbase取得・静的検査を実施した。
製品image生成script・base最終採用・package導入/build・実機受入は未実施である。

## 1. 一構成へ絞った案

**公式Lite 64-bit/Trixieの未起動imageを複製し、既存arm64 Release `.deb`と必要依存をoffline導入する。**
現行Mac + Docker + Debian/.deb開発を維持する。pi-gen stage0からのOS再生成、独自kernel、
Buildroot移植、公開Imager repository、公式一覧掲載を作業列に入れない。
生成元pi-genは由来・標準layout/初期化の確認だけに使う。

PiCDPlayer基準は再取得した`refs/remotes/origin/master`の
`05a1a3e8cadc851e2ccb5f664586a55c431cab68`。旧glitter-lycheeのsourceを製品buildへ使わない。
生成工程は未実装で、オンライン更新用`deploy.sh`もimageへ転用しない。

## 2. 固定base候補

| 項目 | 今回取得した公式metadata |
|---|---|
| 配布directory | [raspios_lite_arm64-2026-10-06][base-dir] |
| file | `2026-10-06-raspios-trixie-arm64-lite.img.xz`。動く`latest` URLは入力にしない |
| 圧縮file SHA-256 | `483db18a48da399b5b7022ffae9a07bc6d99daab30e593aedac09b69ff422843`（[公式checksum][base-sha]） |
| 圧縮file bytes | `550466056`（HEAD値とB0実取得bytesが一致、SHA-256も一致） |
| 展開image bytes | `3078619136`（[bmap][base-bmap]のImageSizeとB0展開実測が一致） |
| 展開image SHA-256 | B0で計算: `b8a393dc1c3701450b5fffbfe74368a98fb6eab7415ce456cc0cead886ea8780` |
| 生成由来 | [.info][base-info]はpi-gen `b2cd98a9ee08a31ef7d0110ed09f2492bbd972ec`、stage2と報告。現在のarm64 branch headと区別 |
| package観測 | `.info`はuserconf-pi `0.19`、sys-mods `1:20260914`、cloud-init `25.2-1~bpo13+1+rpt20`、NetworkManager `1.52.1-1+rpt4`を列挙 |
| 版表記の照合結果 | raspi-config/raspi-config-coreの実dpkg statusも`20261026`。実changelogは同版を2026-10-02付で記録。日付に見える版番号をrelease日と解釈しない。`.info`との不一致は観測せず、先の懸念を実体照合で解消した。package署名/source全監査は別 |

B0で本体hash・partition/FSヘッダ・一覧・選定file・dpkg status・関連unitの静的linkを確認した。
実file hash検査と署名の信頼性確認は別であり、署名/信頼keyの検証は今回未実施。
公式baseのbytesをPiCDPlayer入りimageの容量・最小SD容量に転用しない。
[生成元export処理][pi-export]と同じMBR/FAT32 boot/ext4 rootの構成をB0でも観測した。

## 3. 最小組込み工程

以下は将来の実装要件で、実行済み手順ではない。imageを扱うLinux/aarch64環境と通常のapp buildを分離する。

| 順序 | 仕様 | 完了・停止条件 |
|---|---|---|
| 1 入力固定 | base URL/hash、app source、Release `.deb`/manifest、tool/containerを識別 | arm64、metadata/API/WPE/systemd ON、paranoia OFFを確認。旧cache/短期artifactの存在は仮定しない |
| 2 依存解決 | 実Dependsとbaseのdpkg/APT状態からtransitive依存を解決し、version/architecture/hashを固定して別工程で取得 | 開発Dockerのapt一覧を転記しない。font/CA/Bash/timeout/PAM等の非ELF要件も確認。不足/ABI不整合は停止 |
| 3 複製・容量 | 原本を不変保存し、試験専用imageだけを扱う。標準partition構成・boot/kernel/DTBを維持 | payload/依存/一時展開/稼働余裕を見積もる。必要ならroot末尾拡張を別途仕様化。元image/実媒体は変更しない。容量は未測定 |
| 4 offline導入 | networkなしの隔離rootfsで、揃えたpackageを通常のpackage manager経由でconfigure | dpkg/依存整合を確認。archive展開だけで代用しない。arm64実行可能なLinux環境が必要でmacOS単体mountでは不足 |
| 5 設定 | 専用user/group、EnvironmentFileと製品用の自動起動構成をimage生成側で用意する | 生成環境でserviceをstartしないことと、配布imageで自動起動することを区別する。利用者によるenable/SSHを前提にしない。base初期処理との安全な連携は未設計・実機未確認 |
| 6 検査・保全 | ELF/loader/依存、所有権/mode、unit/設定、secret、dpkg、hashと差分を保存 | SDKの新規同梱や個人cache/鍵混入を避け、検査用mount・抑止設定を解除して保全。boot成功とはしない |

アプリ配置の正本は[CMake install][app-cmake]、runtime依存は実CPack/shlibdepsのDepends。
WPEは`/opt/picdplayer-wpe/2.54.0`とprivate library pathを維持し、host glibc/ldconfigを置換しない。
同じTrixie系でもABI/helper起動/sandbox実隔離の成功を保証しない。
無差別なOS upgrade・kernel/firmware更新を依存導入に紛れて行わない。

### 隔離・既存tooling

現masterの[postinst][app-postinst]は`/run/systemd/system`があればconfigure時に
daemon-reload/try-restartを直接呼ぶ。[prerm][app-prerm]はremove/deconfigure時にstop、
[postrm][app-postrm]はreloadを同条件で呼ぶ。chrootやpolicy-rc.dだけに頼らず、
hostの`/run`・systemd socketを共有しない。依存packageの起動抑止も別に確認する。

[build-container.sh][app-build]はpackage-container/stageを作り直し、WPE cacheがなければcold buildする。
旧成果のあるworktreeで安易に実行せず、別output/workspaceと予算を先に用意する。
loop/mount等のDocker VM内権限も未検証で、host device一括共有や無断privileged実行をしない。
pi-genもDocker隔離は完全でなくloop等に依存すると[説明][pi-readme]しており、Macで成立済みとは言わない。

## 4. 最小runtime設定案

unit既定値の変更ではなく、image側のEnvironmentFile設定案である。
`/etc/default/picdplayer`は既存仕様に沿って次を候補とする。

```ini
PICDPLAYER_EXTRA_ARGS="--metadata musicbrainz --metadata-cache /var/cache/picdplayer --api-port 8080 --settings-file /var/lib/picdplayer/settings.json"
```

APIはkioskに必須でloopbackを維持する。外部metadataにはCA/DNS/時刻が必要だが、
外部通信成功をCD再生の起動条件にしない。背景は既存どおり既定OFF、共通API keyは同梱しない。
`/etc/default/picdplayer-kiosk`は次の候補とする。

```ini
PICDPLAYER_KIOSK_RUNTIME=wpe
PICDPLAYER_KIOSK_FALLBACK=none
PICDPLAYER_INSPECTOR=off
```

現fallback既定はChromiumなのでWPE専用imageでは明示noneとする案。Cage/Chromiumを追加せず、
WPE失敗を別runtimeの成功へ読み替えない。専用`picdplayer`はlocked password、system UID/GID
（通常1000未満）、nologin、home作成なしとし、初回運用userのUID1000とは分離する。
daemonのvideo/cdrom/audio、kioskのvideo/render/input、PAM/seat/device権限を実体で検査し、
root実行やworld-writable化で回避しない。

stateは`/var/lib/picdplayer`、cacheは`/var/cache/picdplayer`、runtimeは`/run/picdplayer-kiosk`、
stdout/stderrはjournalとし、独自log領域や永続partitionを追加しない。
既存共有StateDirectoryのmodeはdaemon 0700 / kiosk 0750で不一致が残る。
稼働後の容量上限、意図するmode、settings schemaと復旧時の互換は導入前に決める。

## 5. 家電UXを基準にした初回起動

家電らしい体験は新しい追加要件ではなく、[基本設計][product-basic]の「物理CDを入れ、TVリモコンで操作する」
という既存方針である。USB keyboardをSSHへ置き換えるだけでは満たさない。
前案の二boot file・認証情報生成・SSH login・service切替・手動rebootは、一般利用の必須導線から撤回する。
入力生成toolの実装も現在の次作業ではない。技術的な観測は下の履歴に保存し、必要な開発/保守でのみ再利用する。

| 利用段階 | v1.0で満たすUX / 未実装の境界 |
|---|---|
| 導入と利用を分離 | 配布imageをImagerでSDへ書く作業は導入担当者の作業。その後の初回起動・通常利用でLinux account作成、hash/file編集、IP探索、SSHを必須にしない |
| 初回電源投入 | TV・drive等を接続して起動し、利用者のOS設定なしでPlayer画面/No Disc等へ到達する。baseのuser作成対話やshellを利用者の作業場にしない。自動初期化・起動連携は未実装 |
| 再生・操作 | 物理CDとTVリモコンで操作する。既存の「起動時に入っていたdiscはSTOPPED、挿入観測後は自動再生」を変更せず、image化で別の再生規則を作らない |
| networkなし | CD再生・基本操作のために初回DHCP/Internet/アカウントを要求しない。metadata/画像は任意の付加機能。network条件や写真keyを基本再生のセットアップへ混ぜない |
| 個人設定 | 既存Player/CECの設定契約を利用し、OS設定全般を新UIへ積み増さない。Wi-Fi等を必要とする場合の利用者向け導線は別途小さく設計し、SSHを通常手順の代用にしない |
| 管理・保守 | SSH・管理accountは開発/保守の別経路。共通credentialやdeveloper sudoersを配布せず、保守経路の未決を一般利用者へ転嫁しない |
| 異常・復旧 | Linux操作が分かることを正常利用の条件にしない。利用者向け案内・再試行/媒体復元の最小範囲を定める。全失敗matrixやV2 #254のhardeningを自動的に必須化しない |

基本設計にあるLinux起動画面を見せない目標も未実装/未検証のまま残す。
#19のPendingを理由に達成済み・要件消滅とは扱わない。一方、専用splash/演出全体を無断再開せず、
v1.0の自動Player到達と起動表示の最小対策・残課題を分けて受入へ記録する。

次の調査は、baseのどの初期化（FS拡張・identity等）を保持し、どの汎用user対話/gettyを製品構成から外すか、
非login service user・PAM/seat/DRM・既存unitによる自動起動がどう成立するかの静的な責務整理に絞る。
userconfigの無効化やUID1000 account削除を既に採用/実装したとはしない。共通imageの未知password/鍵を放置する案にもせず、
account/SSH policyと安全な保守経路を#220と照合してから仕様化する。大きな新wizardや別OS比較へ戻らない。

<details>
<summary>履歴: 撤回した一般利用向けSSH初期設定案（技術調査は保全）</summary>

以下は家電UXの優先を取り落とした時点の案であり、現在の一般利用導線・着手指示ではない。
keyboard不要の条件自体は維持するが、SSH/input toolで代替する判断は撤回した。

### 旧案: headless初期設定・復旧

**利用者判断: v1.0は初回もUSB keyboardなしを必須とする。** 先のローカル対話案は採用しない。
以下はその要件に対する一構成案で、headlessの実動作合格ではない。
独自wizard/Imagerカスタマイズ/公開repositoryを追加せず、boot fileと有線DHCP・SSHを使う。
有線DHCPとSDを書いたMac等からのSSHが初期設定環境の前提案。Wi-Fiだけの初回設定は追加せず、必要ならSSH確立後にbase標準手順で設定する。

### 5.1 共通imageと利用者入力を分離

配布する共通imageには利用者固有のuser情報/password/hash/authorized key/network credentialを投入しない。
標準のuserconfig/sshswitch/host鍵初期化は維持し、daemon/kioskはboot-disabledとする案。
SSHは共通imageでは既定無効とし、利用者が書込み後に明示有効化する。

将来、利用者が書込み済みFAT boot領域のrootへ置くfileを次の二つに限定する。
稼働時のmount pointは`/boot/firmware`であり、FAT内にそのdirectory階層を作る意味ではない。
今回はSDを書いたり実credentialを生成したりしていない。

| file | 入力契約 |
|---|---|
| `userconf.txt` | regular fileにASCII一行の`<利用者user名>:<SHA-512 crypt hash>`と末尾LF。hashは完全な`$6$...`形式、fresh random saltと強い機体ごとのpasswordを使う。平文password、余分なcolon/行/BOMを許さない |
| `ssh` | 空のregular file。SSH要求markerでありuser作成・認証成功の証拠ではない。`ssh.txt`へ勝手に拡張しない |

user名はbaseの規則（1〜32文字、先頭ASCII小文字、以後小文字/数字/hyphen）を守り、
root、picdplayer、baseおよび最終imageの依存導入で追加されるuser/group名等との衝突を事前拒否する。
予約名一覧は対象image版に結び、未確定なら入力生成を受け付けない。B0のbase一覧だけを最終一覧としない。
`userconf`があると`userconf.txt`より優先されるので、共存や既存inputの無断上書きを拒否する。
生成・検証支援toolはまだ未実装。入力作成を一般利用者の手作業任せで安全と判断しない。
支援toolはlocal専用directoryを0700、出力を0600で新規作成し、symlink/無断上書き/SD直接書込みを拒否する設計とする。
passwordは隠した入力からhash processへ渡し、argv/history/logへ出さない。FATへ移した後も0600が保たれるとは約束しない。

実baseのuserconf-serviceは非空password欄しか検査せず、hash形式・衝突・余分なfieldを十分検査しない。
不正/欠落入力はtty8のkeyboard対話へ落ち得る。この場合はkeyboardで補完するのではなく、
headlessセットアップ失敗としてoffline訂正/再image復旧へ進む。

### 5.2 非対話setupからkioskへ

1. 利用者がlocalで検査済みの二fileを用意してFATへ配置する。共通配布artifactへ再混入させない。
2. 信頼できる有線LAN/DHCPへ接続して初回boot。baseが有効inputを非対話で処理し、userを設定しSSHを要求する。daemon/kioskはまだ起動しない。
3. DHCP lease一覧等の確認できる方法で対象IPを特定し、意図したuser/passwordでSSH loginする。hostname/mDNSだけに依存せず、public転送や無条件のhost-key警告無視をしない。
4. 意図したuserのUID/GID1000・home・shell・管理操作、userconfig終了/再試行なし、個人inputの消費状態を確認する。fileが消えたことやunit終了だけでは成功としない。
5. 設定完了後、getty@tty1を次回boot用にdisable、daemon/kioskを次回boot用にenableする。`--now`/start/stopは使わずSSHとgettyの現在状態を保つ。
6. 明示reboot後、SSH再接続とkiosk運転・getty非競合を確認する。SSH復旧経路は維持する。今回これらを実行した意味ではない。

SSHのbootstrap passwordは強く一意なものを使い、imageに開発sudoersや共通credentialを入れない。
有効なpassword認証・root login拒否等のSSH policyは採用imageで明示・検査する必要があり、
baseのコメント記述だけを実効設定の証拠にしない。key-only運用へ移す場合は利用者の公開鍵登録と
別接続での成功確認後にpassword認証を停止し、先に復旧経路を閉じない。
初回server identityの確認/信頼手順も必要で、無検証の`StrictHostKeyChecking=no`を手順にしない。

実userconf/cancel-renameは内部の一部失敗を後続cleanupの成功で隠し得る。
sshswitchもmarkerを消してから非同期startを要求するため、marker消失は待受・認証成功を意味しない。
`cancel-rename`がgetty@tty1を再enable/startすることは実体で確認した。初回setup中にkioskを起動せず、
切替を完了確認後の次bootへ分離する。disableはmaskではなくgenerator/他unit入口は未検証。
`After=userconfig.service`を成功gateにせず、`After=cloud-final.service`の単純追加も
multi-user.targetとのordering cycleになり得るため行わない。V2の全失敗復帰#254は再開しない。

### 5.3 keyboardなし受入・失敗時の境界

| ケース | 合格/失敗と最小対応（すべて将来の受入） |
|---|---|
| 正常input | USB keyboardを一度も接続せず、IP特定→SSH認証→意図したuser/管理権限→切替/reboot→SSHとkiosk運転まで確認。ここまで実機未確認 |
| 欠落/不正hash・user衝突 | host側の生成/検証時に拒否。boot後に対話要求や意図しないidentityが出たら失敗。keyboardで救済して合格としない |
| file消失だがSSH不可 | 認証/待受成功としない。DHCP/電源/媒体/SSHの観測を分け、無制限の自動再試行をしない。待ち時間と観測項目は実機手順で上限を決める |
| 部分適用・中断 | 状態不明として既知の正常imageへの再imageを基本とする。userconfig無効化後はfileを再配置するだけで再適用できると約束しない |
| networkなし | 初回setupの前提不成立。Wi-Fi wizardやkeyboard fallbackを追加しない。setup後のCD再生は外部network成功を起動条件にしない |

FAT上のhashや`failed_userconf*`もoffline推測攻撃の対象として扱う。
成功後に不要inputの残存を確認してもFAT削除をsecure eraseと呼ばず、秘密を含む媒体imageを再配布しない。
rootfs全体・鍵・machine-idを他機体へcloneした復旧をしない。

### 5.4 Mac側hash生成の限定確認

現在のMacでは標準`/usr/bin/openssl`はLibreSSL3.3.6で、passwdの`-6`は対応一覧にない。
既設OpenSSL3.6.5（`/opt/homebrew/opt/openssl@3/bin/openssl`）で、非秘密fixtureをstdinで渡す
SHA-512 cryptの形式・同salt再計算一致・random saltの変化を確認した。実password/hashは表示・保存していない。
既存build containerの`/usr/bin/openssl`も試したがbinary不在で失敗。container内で生成可能と仮定しない。
追加install・弱いhash方式へのfallbackは行わず、支援toolでは対応toolの事前検査と明示選択が必要。
これはhash utilityの確認で、入力支援tool完成やPi上の`chpasswd`/SSH成功の証拠ではない。

復旧は既知の正常imageを別SDへ書き、初回設定後に必要最小限のsettings/利用者UIを明示的に戻す案。
OS全体・cache/journal・machine-id・SSH host鍵は丸ごと復元しない。機体固有identity/鍵の再生成、
settings schema互換・所有権/modeと起動時UI検証を確認する。自動rollback/import UIや新backup形式は作らない。
秘密は利用者が別管理し、通常package更新は#147の中断復旧と合わせて範囲を限定する。
dpkgをtransactional更新として扱わない。

</details>

## 6. B0読取り検査の実績

利用者の続行指示を受け、同一base一つだけを取得・展開し、mountせず初回47 fileと補足1 fileを標準出力へ抽出した。
既存macOSのcurl 8.7.1、xz 5.8.4、7zz 26.03 arm64、Node.js v24.18.1を使用。
tool確認のため既存Debian系containerをnetworkなし/read-only/非root/権限削除で短時間起動したが、
実際のimage読取りはmacOSで行った。Linux/aarch64 buildやtarget programの実行ではない。

| 項目 | 観測・検査結果 |
|---|---|
| 時間 | 主処理2026-10-22 15:04:45〜15:11:51 UTC、約7分6秒。helper一fileの補足読取りも15:20:47 UTCに終了し、準備を含め14:57:52からの30分期限内。image再取得/再試行なし |
| 取得 | HTTP payload `550466056` bytes、公式SHA-256と一致。xzはdecode終了0で整合確認し、展開bytesもbmapと一致 |
| MBR | sector 512 bytes。boot開始`8388608`、長さ`536870912`、type0x0c。root開始`545259520`、長さ`2533359616`、type0x83。両領域はimage内で重複なし |
| FAT警告 | 7zzが末尾2048 bytesを警告。BPBは1048572 sectors = `536868864` bytesで、partition内の残2048 bytesは全て0と別読取りで確認。FS外のpaddingとして説明でき、隣接領域への越境はない |
| ext4 | block4096 bytes、618496 blocksでpartition長と一致。free111084 blocks = `455000064` bytes、reserved30924 blocks = `126664704` bytes。全fsckや全file hash検査ではない |
| 容量上の含意 | root空き約434 MiBはWPE/依存/font導入と一時展開・稼働余裕の保証にならない。追加payload測定後にroot末尾拡張が必要か決め、最小SD容量を未測定のまま約束しない |
| package実体 | dpkg status633 records。userconf-pi0.19、sys-mods1:20260914、cloud-init25.2-1~bpo13+1+rpt20等は公開infoと一致。raspi-config版表記も2節の通り照合 |
| 追加依存 | このbaseのdpkg statusにはlibgbm1/libgl1-mesa-dri/bubblewrap/xdg-dbus-proxy/fonts-noto-cjkがない。LiteのままでWPE runtimeが揃うとはしない。file独自同梱の有無まで完全監査したという意味ではない |
| 初期identity | `/etc/machine-id`は14 bytesの`uninitialized`。UID/GID1000は`pi`（nologin）、専用picdplayer user/groupは未作成。新規system accountを使う設計を維持 |
| SSH | userconfig/sshswitch/host鍵再生成のenable linkと実unitを確認。ssh/ssh.txtはbootに無く、通常のssh listener enable linkも見つからない。将来のpackage helper状態や実listenerは別判定 |
| 秘密情報の限界 | SSH host鍵/authorized_keys、cloud instance cache等の指定pathは一覧に見つからない。ただし全内容・削除領域のsecret監査ではなく、key-only SSH設定の保証でもない |
| 非変更確認 | 取得archive/展開imageのhashを再照合し、抽出partitionと元imageの該当byte範囲のhash一致、初回47 fileのhash再一致を確認。補足helperは実path/sizeとpackage MD5 ledger一致も確認。原本/抽出partitionを変更していない |

### 資源上限と制約

事前計画は30分・network1 GiB・2 CPU/2 GiB・追加disk12 GiB・開始時空き16 GiB以上。
開始時空きは`76095901696` bytes、保存後の領域使用量は`du`で約6.28 GiBだった。
HTTP payloadを期待bytes以内に制限し、tool出力/disk/期限を監視した。xz/7zzには2 threads指定、
xzにはdecompress memory1 GiB、子processとsupervisorの合計RSSには1秒間隔2 GiB停止閾値を設定した。
観測された本処理のpeakは1426160 KiB（約1.36 GiB）。

host側CPU quota/RAMのkernel hard limitではない。1秒未満のfile readerはsample前に終了するため、
その瞬間peakは未捕捉。全区間の厳密な最大CPU/RAMを証明しない。
preflightのHomebrew照会でAPI metadata更新が発生したがpackageはinstallしていない。
取得image payload以外のmetadata/TLS等のwire bytesは未計量で、総wire転送量を厳密実測したとは言わない。
以降の依存取得・package導入・WPE cold buildは別予算でありB0へ追加しない。

### 保存と追跡

raw image、圧縮原本、partition複製、一覧、47+1 file、各command receipt、補助scriptは、調査元worktree
`glitter-lychee`直下のGit除外済み`build-image-b0-20261022/`に保持した。統合先やmasterのcheckoutに
同じ相対pathで存在するとは仮定しない。既存成果/cacheは削除していない。
169 fileのbytes/hashを記録した`b0-evidence.json`のSHA-256は
`a09f383eadacef23061e284fe51d5ac24a98190b1c4781c0b950efd70b4ee5b7`。
初回選定でhelperを`/usr/sbin`と想定していたため、実path `/usr/bin/cancel-rename`を追加読取りした。
`cancel-rename-addendum.json`のSHA-256は`f35ebe24be0f6b81d881c5f7eff2812b6be231a766b3c296d294ac0bf2c1f40f`、
helper自体は`d2c5772bcec7b1ef7a08eeb35656506828345167269ceed2e34c7e320d30dd2a`。
この補足はprimary manifestを上書きせず別receiptとして保持した。

| 補助script（製品toolingではない） | SHA-256 |
|---|---|
| run-b0.mjs | `8fce72ef1f8a02cc2ed91f9d540d69484c3130473ae395e1907d000287f5771f` |
| read-selected.mjs | `00e59af602eddbe8c0dc18e9ef9956afe7ab7e0adce0f2533321a4a0d1f607c8` |
| finalize-b0.mjs | `880a190793473186a624b3b26429c915337150c6b8bd93eea10e563cfc41ecca` |
| read-cancel-rename.mjs | `509a22235a338ace8b8c6748b7555d276cf3c2521552fd8a6999fe289c2200ac` |

これは現在のlocal保存状態で、worktree/volumeの存在を長期保全・正式artifact供給の保証にしない。
別backup・保持期間・正式な保存先は未確定。大きなimageや取得toolをGitへ追加しない。

2026-10-24の文書統合では、上の一次receiptから転記した[B0要約JSON](b0-summary.json)をversion管理へ追加した。
これはbase/partition/hash・主要観測と限界を次sessionへ引き継ぐ小さな要約で、imageや全receiptの保管保証ではない。

## 7. 到達点と次の小単位

B0の取得・選定範囲の静的検査は完了。V0および#46全体は未完で、baseの製品適合は未承認。
家電UXを再確認し、一般利用者へSSH/credential生成/service切替を要求する案は撤回した。
次の小単位は**base初期化と製品service自動起動の責務・unit依存・account/権限の静的棚卸し**。
実装や安全性を先取りせず、5節のUXへ至るための不足だけを明確にする。入力生成toolは着手しない。
独自wizard、Imager対応、別OS比較、V2の全失敗復帰設計へ広げない。
root容量/依存導入量、state mode、log/cache上限、最小SD容量、正式供給/安全更新条件も未決として維持する。

今回imageは取得したが、mount/chroot/apt/package導入・target実行・製品image生成/build・SD書込み・
Pi/SSH/配備/再起動は行っていない。boot・DHCP/SSH認証・初回設定の実動作、sandbox隔離、
CD-ROM/ALSA/CEC/DRM、復旧成功は未検証。host静的検査をLinux/aarch64 buildやPi実機受入と混同しない。
license/inventoryは#66/#67/#68、採用WPE供給は#220、実機受入は#47/#184等、正式公開は#45に残す。
既存branch/worktree/試作/cacheとPendingを維持した。既存#46/#47へ、家電UXという既存方針を優先し
一般向けSSH案を撤回する訂正を追記した。以前の本文は履歴として保存し、ラベル・Open状態を保持して再取得確認した。
2026-10-22の調査では新Issue作成・PR編集・commit/push/正式公開は行っていない。
2026-10-24は利用者の別指示で文書をmasterへ統合し、ここで検討を区切る。
次のimage設計は別途再開とし、実機確認の引継ぎは[計画8節](../../appliance-image-plan.md#8-検討の区切りと実機確認への引継ぎ2026-10-24)を参照する。

## 参照

[base-dir]: https://downloads.raspberrypi.com/raspios_lite_arm64/images/raspios_lite_arm64-2026-10-06/
[base-sha]: https://downloads.raspberrypi.com/raspios_lite_arm64/images/raspios_lite_arm64-2026-10-06/2026-10-06-raspios-trixie-arm64-lite.img.xz.sha256
[base-bmap]: https://downloads.raspberrypi.com/raspios_lite_arm64/images/raspios_lite_arm64-2026-10-06/2026-10-06-raspios-trixie-arm64-lite.bmap
[base-info]: https://downloads.raspberrypi.com/raspios_lite_arm64/images/raspios_lite_arm64-2026-10-06/2026-10-06-raspios-trixie-arm64-lite.info
[pi-readme]: https://github.com/RPi-Distro/pi-gen/blob/b2cd98a9ee08a31ef7d0110ed09f2492bbd972ec/README.md
[pi-export]: https://github.com/RPi-Distro/pi-gen/blob/b2cd98a9ee08a31ef7d0110ed09f2492bbd972ec/export-image/prerun.sh
[app-cmake]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/CMakeLists.txt
[app-build]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/scripts/build-container.sh
[app-postinst]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/packaging/debian/postinst.in
[app-prerm]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/packaging/debian/prerm.in
[app-postrm]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/packaging/debian/postrm
[userconf]: https://github.com/RPi-Distro/userconf-pi/blob/2dec0368f79b084d9def4e4cfb0cf0720d3ff30c/userconf
[userconfig-unit]: https://github.com/RPi-Distro/userconf-pi/blob/2dec0368f79b084d9def4e4cfb0cf0720d3ff30c/debian/userconf-pi.userconfig.service
[cancel-rename]: https://github.com/RPi-Distro/userconf-pi/blob/2dec0368f79b084d9def4e4cfb0cf0720d3ff30c/cancel-rename
[product-basic]: https://github.com/udonchan/picdplayer/blob/05a1a3e8cadc851e2ccb5f664586a55c431cab68/docs/design/basic-design.md
