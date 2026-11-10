# HG5580A — 移植笔记

> FiberHome HG5580A · Airoha AN7583 · FORESEE F35SQB002G
> 与 HG5380A 同板，差异仅在闪存与内存

本目录记录 HG5580A 目标的移植过程、实测数据和已知差异。所有结论都标注了
验证方式；未验证的部分明确标为「待确认」。

---

## 1. 与 HG5380A 的关系

HG5580A 与 HG5380A **使用同一块主板**，U-Boot 侧只需增加一个构建目标：

| 项 | HG5380A | HG5580A |
| --- | --- | --- |
| SoC | AN7583 | AN7583（相同） |
| RAM | 512 MiB | 512 MiB（相同） |
| SPI-NAND | FORESEE F35SQB002G | FORESEE F35SQB002G（相同） |
| 2.5G PHY | Motorcomm YT8821 | 同（待复核） |
| 光模块 | EN7572 | Airoha UX3363（**不同**） |
| 分区表 | 10 分区 | 10 分区（**完全相同**） |

分区表逐字节一致 —— 这已在 5580A 实机 `mtd list` 上确认：

```
0x0000000  0x280000   bootloader
0x0280000  0x080000   bootflag
0x0300000  0x600000   KernelA
0x0900000  0x2800000  RootfsA
0x3100000  0x600000   KernelB
0x3700000  0x2800000  RootfsB
0x5f00000  0x6800000  apps        (104 MiB)
0xc700000  0x1000000  frameworka
0xd700000  0x1000000  frameworkb
0xe700000  0x500000   factory
```

---

## 2. 实机实测数据（HG5580A）

原厂 U-Boot 2023.04 的启动信息与命令输出：

```
U-Boot 2023.04 (Jan 26 2026 - 02:52:15 +0800)
aarch64-buildroot-linux-gnu-gcc.br_real (Buildroot) 13.3.0
GNU ld (GNU Binutils) 2.41
```

### `mtd list`

```
* airoha-flash
  - device: legacy_nand_driver@1
  - parent: spi@1100a000
  - driver: aiorha-flash (legacy)
  - type: NAND flash
  - block size: 0x20000 bytes      (128 KiB)
  - min I/O: 0x800 bytes           (2048)
  - OOB size: 64 bytes
  - OOB available: 0 bytes
  - 0x000000000000-0x00000ee00000 : "airoha-flash"     (238 MiB)
```

### 三点需要注意

**① OOB 是 64 字节，不是 128**

上一台机器（SkyHigh S35ML02G3）的 OOB 是 128 字节。FORESEE 是 64。
这直接影响坏块标记的可用空间。

**② `OOB available: 0 bytes` 是误导**

`mtd dump apps 0x0 0x800` 的实际输出：

```
0x00000000:  55 42 49 23 01 00 00 00  00 00 00 00 00 00 00 00
0x00000010:  00 00 08 00 00 00 10 00  26 ef 0c ff 00 00 00 00
0x00000030:  00 00 00 00 00 00 00 00  00 00 00 00 35 81 8f af
```

逐字段：

```
magic      55 42 49 23   "UBI#"
version    01 00 00 00   1
erase_cnt  00 00 00 00   0
image_seq  00 00 08 00   2048
vol_type   10            1 = static
data_crc   26 ef 0c ff
vid_hdr_crc 35 81 8f af
```

**原厂在 `apps` 分区上用 UBI，并且正常工作。** 所以「OOB available: 0」
是 legacy 驱动的报告方式，不是物理限制。UBI 方案在这台机器上可行。

**③ 总容量报 238 MiB，不是 256 MiB**

`0xee00000` = 238 MiB。名义上 F35SQB002G 是 2 Gbit = 256 MiB。
差异来自原厂驱动表的容量定义，不一定是物理裁剪，但刷机时按 238 MiB 算更安全。

### 坏块

```
spinand showbad
Device 0 bad blocks:
0x01d20000
There are 1 bad blocks.
```

一个坏块，物理偏移 `0x01d20000`，落在 `KernelA`（0x300000–0x900000）区间内。
说明坏块管理正常。

### 其他实测

```
fhhwcfg get     ->  fh_hwcfg: 0xfd
sf probe        ->  airoha-spi spi@1100a000: Invalid chip select 0:0 (err=-19)
sspi ...        ->  Error -38 during SPI transaction
```

`sf` 和 `sspi` 都不可用：这颗芯片走的是 ecnt legacy NAND 驱动，不是 spi-nor
层，也不是通用 DM SPI 层。**因此无法用常规命令读 READID。**

---

## 3. 原厂 env 里两个误导项

`printenv` 里有两组值容易被当成真实数据，其实都是 AN7583 EVB 的默认值：

```
board=an7583_evb
board_name=an7583_evb

partitions=name=bootloader,start=0x0,size=0x80000,bootable;\
           name=tclinux,start=0x84000,size=0xa00000;\
           name=filesystem,start=0xa84000,size=0x3200000;\
           name=art,start=0x10084000,size=0x800000;

ethaddr=00:AA:BB:01:23:40
```

* `partitions` 与 `mtd list` 完全不符 —— 实际分区由 DTS 决定，这个变量没被使用。
* `ethaddr` 是全零偏移的默认值，不是从 `factory` 读出来的。

真正相关的是：

```
bootcmd=ldkernel;bargsenv;bootm
bootdelay=1
rootfs_partition_name=RootfsA
fh_apps_filename=apps_ubi.img
fh_configbfs_filename=fhdata_ubi.img
```

---

## 4. 闪存识别：仍需确认的一点

两个表的 FORESEE 支持情况：

### TF-A BL2（`tf-a/plat/ecnt/common/drivers/flash/spi_nand_flash_table.c`）

```c
#define _SPI_NAND_MANUFACTURER_ID_FORESEE   0xCD

_SPI_NAND_DEVICE_ID_FS35ND01GD1F1   0xA1   1 Gbit   OOB 64
_SPI_NAND_DEVICE_ID_FS35ND02GS2F1   0xA2   2 Gbit   OOB 128
_SPI_NAND_DEVICE_ID_FS35ND02GD1F1   0xB2   2 Gbit   OOB 128
```

### U-Boot（`drivers/mtd/nand/spi/foresee.c`）

```c
SPINAND_INFO("F35SQA002G", SPINAND_ID(..., 0x72, 0x72),
             NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1), ...)

SPINAND_INFO("F35SQA001G", SPINAND_ID(..., 0x71, 0x71),
             NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1), ...)
```

**`F35SQB002G` 不出现在任何一张表里。**

但因为原厂 BL2 已经能识别这颗芯片并让 UBI 正常工作，说明它的
manufacturer/device ID 一定在 BL2 的某个表项里 —— 最可能是 `0xA2` 或 `0xB2`
（都是 2 Gbit）。也可能是原厂用了独立的表。

### 当前无法直接读到 ID

```
sf probe   ->  -19  (走错驱动层)
sspi       ->  -38  (ENOSYS)
md / md.b  ->  命令不存在
```

`mtd dump` 能读数据但没有寄存器级 READID。

### 因此的刷机策略

**先不刷 BL2。** 顺序：

1. 只把 `.fip`（BL31 + U-Boot）写进 UBI 的 `fip` 卷 —— 原厂 BL2 不动
2. 观察 U-Boot 是否打印 `spi_nand: ... found`
3. 如果能识别，再考虑刷 BL2

如果 U-Boot 侧识别失败，需要在 `foresee.c` 里补一条表项。补之前必须知道真实
ID —— 可通过以下任一方式取得：

* 在原厂系统里 `cat /proc/mtd` + `dmesg | grep -i nand`（Linux 侧驱动通常打印 ID）
* 在那台机器上跑一次 `spinand showbad` 前后的完整 `loglevel=9` 日志
* 用逻辑分析仪抓 SPI 总线

---

## 5. 构建目标

`configs/an7583_fiberhome_hg5580a_defconfig` 从 5380A 的目标派生，差异：

```
CONFIG_DEFAULT_DEVICE_TREE="an7583-fiberhome-hg5580a"
CONFIG_DEFAULT_FDT_FILE="an7583-fiberhome-hg5580a.dtb"
CONFIG_SYS_PROMPT="HG5580A> "
CONFIG_ENV_SOURCE_FILE="an7583_fiberhome_hg5580a"
CONFIG_CMD_HTTPD=y            <- Web 恢复
```

`board/airoha/an7583/an7583_fiberhome_hg5580a.env`：

```
#include <env/airoha/an758x.env>
loadaddr=0x90000000
bootmenu_title=U-Boot HG5580A
bootled_status=green:power
bootfile_bl2=an7583-fiberhome-hg5580a-preloader.bin
bootfile_fip=an7583-fiberhome-hg5580a-bl31-u-boot.fip
bootfile_upg=ponwrt-...-hg5580a-ubi-squashfs-sysupgrade.itb
```

DTS 与 5380A 的差异只在 BOSA 节点：5580A 是 Airoha UX3363（paged，i2c0 0x51），
5380A 是 EN7572。

---

## 6. 与验签相关的补丁

TF-A `make_helpers/defaults.mk:254` 无条件 `TRUSTED_BOARD_BOOT := 1`，BL2 会走
`load_auth_image()` 要求完整信任链。原厂 FIP 带 6 张证书，自编译的不带。

TF-A 有逃生门：

```c
if (dyn_is_auth_disabled() == 0)
        return load_auth_image_recursive(...);
return load_image(...);
```

但 ecnt 平台从不填充 fconf TBBR（`fconf_populate_tbbr_dyn_config` 在
`plat/ecnt/` 下无调用点），`disable_auth` 恒为 0。

解法：在 `bl2_main.c` 里直接调 `dyn_disable_auth()`。**关键是第二步**：
`TCSUPPORT_*` 变量不会自动变成 `-D`，必须在 `tf-a/Makefile` 里用 `add_define`
注册，否则 `#if` 恒假、代码被预处理器删掉。

验证（反汇编 `bl2.elf`）：

```
81000e80  bl  auth_mod_init
81000e84  bl  dyn_disable_auth     <- 必须出现
81000e8c  bl  bl2_load_images
```

---

## 7. 救援：x 模式

BL2 找不到有效 FIP 时会请求 XMODEM：

```
Press 'x' for XMODEM download...
```

**FIP 只进内存，不写 flash**，可无限次重试。这是刷 BL2 之前必须确认可用的退路。

反汇编证据（`bl2_load_images`）：

```
bl  bl2_do_load_images       先从 flash 读（会验签）
bl  fip_image_xmodem_load    失败后走 XMODEM
bl  bl2_do_load_images       重试
```

原厂 U-Boot 侧也可以用 `loadx`（xmodem）或 `tftpboot` + `go` 在内存里试跑，
不碰 flash。

---

## 8. Web 恢复页面

`CONFIG_CMD_HTTPD=y` 编译进来后，`http://192.168.0.1/` 提供：

* Flash BL2 / Flash U-Boot / Install system
* Rebuild UBI
* Backup（分区级，含 factory）
* Backup all（按优先级批量，factory 排第一）
* RAM boot（上传 initramfs 只在内存跑）
* Clear bad block markers
* Command console

备份下载的 URL 格式是斜杠分隔，不是查询字符串：

```
GET /api/download/<task_id>/<offset>/<length>
```

分段上限 4 MiB（`HTTPD_DOWNLOAD_SEGMENT`）。一次 backup 任务可以连续取多段：
`fs_close_custom()` 在中间段重置 30 秒窗口，最后一段才关闭会话。

---

## 9. 分区写入安全边界

**永不写入**：

```
bootloader   0x0        刷 BL2 会改这里，风险最高
factory      0xe700000  MAC / PON SN / 光校准，唯一不可恢复
frameworka   0xc700000
frameworkb   0xd700000
```

**可以写入**：

```
apps         0x5f00000  UBI 容器，我们的固件全在这里
KernelB      0x3100000
RootfsB      0x3700000
```

保留原厂分区表的整个意义就是让 `factory` 永远不在 U-Boot 的写入范围内。
