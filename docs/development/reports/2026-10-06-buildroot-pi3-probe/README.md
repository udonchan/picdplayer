# Buildroot Pi 3 arm64最小imageの検証（#198）

検証日: 2026-10-06。対象実機は読取専用SSHで確認した
`Raspberry Pi 3 Model B Rev 1.2`、`aarch64`。現在の起動環境は
Debian 13（Trixie）であり、この検証では変更していない。

## ホストと入力

- ホスト: Apple Silicon Mac、Docker Engine 29.8.0、Linux/aarch64 container。
  Dockerへの割当は約7.7GiB/16 vCPU。ビルドcontainerは4 CPU/6GiBに制限。
- Buildroot: 公式source revision
  [`c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b`](https://github.com/buildroot/buildroot/tree/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b)。
  GitHub source archiveのSHA-256は
  `719c8da6bec40f48bb2d101a7ff0fb477324ce5f39b191a74e3223f8d8c2c4d9`。
- Buildroot host container: [Dockerfile](Dockerfile)で隔離して作成した
  `picdplayer-buildroot-probe:2026-10-05`。生成image IDは
  `sha256:fcea06083354295da836e49b80e86a450c13dbc7463681c3c9bbd1783f78173b`。
  Dockerfileはbase image digestを固定したが、apt package版は固定していない。
  このhost環境を正式release用の完全再現可能なbuild環境とは扱わない。
- 設定: 公式`raspberrypi3_64_defconfig`を変更せず使用。
  `BR2_TOOLCHAIN_EXTERNAL_ARM_AARCH64=y`、`BR2_LINUX_KERNEL_DEFCONFIG="bcm2711"`を
  生成`.config`で確認した。後者がPi 3で必要なdeviceを有効にするかは実機未確認。
  initはBusyBox、DHCP interfaceは`eth0`。Dropbear/SSHはこの設定では有効でないため、
  生成imageの実機確認にはconsole/serialまたはSSHを安全に追加した試験設定が必要。
  生成kernel `.config`では`CONFIG_BLK_DEV_SR=m`、`CONFIG_CDROM=m`、
  `CONFIG_USB_STORAGE=y`、`CONFIG_CEC_CORE=m`、`CONFIG_DRM_VC4=m`、
  `CONFIG_SND_SOC_HDMI_CODEC=m`を確認した。module設定の存在は、
  Piで実際にdevice nodeが作られ動作することの証拠ではない。

## 再現手順

作業dirを`/private/tmp/picdplayer-br198`として、上記source archiveを展開する。

```sh
mkdir -p /private/tmp/picdplayer-br198
curl -fL -o /private/tmp/picdplayer-br198/buildroot.tar.gz \
  https://codeload.github.com/buildroot/buildroot/tar.gz/c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b
shasum -a 256 /private/tmp/picdplayer-br198/buildroot.tar.gz
tar -xzf /private/tmp/picdplayer-br198/buildroot.tar.gz \
  -C /private/tmp/picdplayer-br198
```

上記hashと一致することを確認してから使用する。
ホストcontainerを次のように作る（image tagはこの試験専用）。

```sh
docker build -t picdplayer-buildroot-probe:2026-10-05 \
  docs/development/reports/2026-10-06-buildroot-pi3-probe
```

Buildroot sourceが`/private/tmp/picdplayer-br198/buildroot-<revision>`にある場合:

```sh
docker volume create picdplayer-br198-output
docker run --rm -v /private/tmp/picdplayer-br198:/work \
  -v picdplayer-br198-output:/work/output \
  -w /work/buildroot-c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b \
  picdplayer-buildroot-probe:2026-10-05 \
  make O=/work/output raspberrypi3_64_defconfig

docker run --rm --cpus=4 --memory=6g \
  -v /private/tmp/picdplayer-br198:/work \
  -v picdplayer-br198-output:/work/output \
  -w /work/buildroot-c29ff5b9b38e87af3df0c95c7e3d6f51c0c0688b \
  picdplayer-buildroot-probe:2026-10-05 \
  make O=/work/output -j4
```

作業dirとcontainerは現行PiCDPlayerのbuild/deploy経路と分離する。
現在のPiの起動mediaやサービスへは一切適用しない。

## 結果

- Docker host image build: 成功。
- 公式Pi 3 arm64 defconfig生成: 成功。
- 最初にMacのbind mountを`O=`の出力先にしたbuildは、kernel source展開時に
  `tar: ... Directory renamed before its status could be extracted`で停止した。
  source archiveのhash照合は成功しており、失敗をkernelの非互換とは扱わない。
  同じoutput内容をDocker管理volumeへ保全コピーし、部分展開dirを退避して再開した。
  以後の作業では上記のように初めからDocker volumeを出力先にする。
- 最小`sdcard.img`の生成: **成功**。Docker volume上に153MiBのDOS/MBR image、
  32MiB FAT boot partitionと120MiB ext4 rootfs partitionを確認した。
  `boot.vfat`、`rootfs.ext4`、kernel `Image`、Pi 3 B device treeも生成された。
  image SHA-256は`05339194a9534a63affa4b586f4f958c93b2830ca33e18197a5fcf52fcbf287e`。
  隔離host作業領域へコピーしたimageのhashも一致した。
- 初回bind mount buildはUTC 00:03:51–00:26:12で失敗、volumeへの移行後の再開は
  UTC 00:28:12–00:42:10で成功（約14分）。合計経過には失敗とコピー作業を含み、
  これをclean buildの所要時間と混同しない。Docker volume上の出力は約8.1GiB。
- Pi実機起動、CD-ROM、ALSA、CEC、DRM/KMS: **未実施**。

`sdcard.img`はDocker volume内にあり、隔離host作業領域へ次のようにコピーできる。
コピー後に上記SHA-256と照合する。**この手順はSDカードへの書込みを行わない。**

```sh
docker run --rm -v picdplayer-br198-output:/out:ro \
  -v /private/tmp/picdplayer-br198:/export debian:trixie-slim \
  cp /out/images/sdcard.img /export/sdcard.img
shasum -a 256 /private/tmp/picdplayer-br198/sdcard.img
```

Buildroot imageが生成されても、Piで起動できること、光学driveから読めること、
HDMI音声やCEC、DRM表示が機能することを意味しない。実機検証は通常の起動mediaを
保持したうえで、別mediaと復旧手順を用意して行う。
