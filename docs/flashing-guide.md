# HG5580A / HG5380A 刷机教程

> Airoha AN7583 · FORESEE F35SQB002G 或 SkyHigh S35ML02G300 · 256 MiB SPI-NAND
> U-Boot 2026.07 + TF-A 2.10 · UBI 布局

本文适用于 HG5580A 和 HG5380A —— 两者同板，分区表完全一致，只有闪存批次
和光模块不同。

---

## 零、先读这一段

**这台机器上唯一不可恢复的数据是 `factory` 分区**（MAC 地址、PON 序列号、
光模块校准）。它不在 UBI 里，任何 U-Boot 操作都不碰它 —— 但**刷机前必须先备份**。

分区布局（原厂 10 分区，实测确认）：

```
0x00000000  0x280000   bootloader    原厂 BL2+FIP，刷 U-Boot 会替换
0x00280000  0x080000   bootflag      原厂 A/B 选择器
0x00300000  0x600000   KernelA       原厂系统槽 A
0x00900000  0x2800000  RootfsA       原厂系统槽 A
0x03100000  0x600000   KernelB       原厂系统槽 B
0x03700000  0x2800000  RootfsB       原厂系统槽 B
0x05f00000  0x6800000  apps      ★   UBI 放这里（104 MiB）
0x0c700000  0x1000000  frameworka
0x0d700000  0x1000000  frameworkb
0x0e700000  0x500000   factory   ★★   永不写入
```

---

## 一、准备工作

### 1.1 硬件

| 项 | 要求 |
| --- | --- |
| 串口 | CH340 / FTDI，115200 8N1 |
| 网线 | lan1（2.5G）或 lan2~lan4 |
| PC 网络 | 静态 `192.168.0.2/24` |

### 1.2 需要下载的文件

| 文件 | 大小 | 用途 |
| --- | --- | --- |
| `an7583-fiberhome-hg5580a-preloader.bin` | ~118 KB | BL2，写到 NAND `0x0` |
| `an7583-fiberhome-hg5580a-bl31-u-boot.fip` | ~371 KB | BL31 + U-Boot，写进 UBI `fip` 卷 |
| `ponwrt-...-hg5580a-ubi-squashfs-sysupgrade.itb` | ~34 MB | 系统固件 |
| `ponwrt-...-hg5580a-ubi-initramfs-recovery.itb` | ~30 MB | 救援 initramfs |

校验值在 release 正文里，刷之前先 `sha256sum` 对一遍。

---

## 二、进入 U-Boot

### 2.1 原厂 U-Boot

上电，串口出现：

```
UserName:
Password:
```

输入原厂凭据后进入 `U-Boot>`。

**注意**：看到 `UserName:` 或 `Password:` 后**不要再发回车或 Ctrl-C**，
否则会把已输入的字符清掉，导致密码被打进用户名字段。

### 2.2 新 U-Boot

上电后出现菜单：

```
U-Boot HG5580A
1. Boot system
2. Web recovery
3. Boot initramfs via TFTP
4. Flash system via TFTP
5. Flash U-Boot via TFTP
6. Flash BL2 via TFTP
7. Show storage information
8. Reboot
0. Exit
```

### 2.3 Web 恢复

选 `2. Web recovery`，或命令行 `run boot_recovery`：

```
HTTP management: http://192.168.0.1/  (DHCP enabled)
```

浏览器打开 **http://192.168.0.1/**

---

## 三、备份（必做）

### 3.1 Web 页面

Web → **Backup** 标签 → 选择目标 → **Download backup**

或直接点 **Download all** —— 按优先级批量备份，`factory` 排第一，
中途中断也已经拿到最不能丢的文件。每个文件的 MD5 显示在结果区。

**必须备份的（按优先级）：**

1. `factory` — MAC / PON SN / 校准，**丢了不可恢复**
2. `bootloader` — 原厂 BL2+FIP，回退用
3. `bootflag` — A/B 状态
4. `KernelA` / `RootfsA` — 原厂系统，可回退

### 3.2 原厂 U-Boot 命令行

```sh
mtd read factory 0x88000000 0x0 0x500000
```

然后靠串口或网络传出去。原厂 U-Boot 没有 `md` 命令，但可以用
`crc32` 校验：

```sh
crc32 0x88000000 0x200
```

---

## 四、首次安装：三步

### 步骤 1：重建 UBI

**Web → Flash → UBI operations → Rebuild UBI**

格式化 `apps`（104 MiB），建立：

```
fip          待写入
ubootenv   126,976 B
ubootenv2  126,976 B
bosa       380,928 B
ri         380,928 B
```

> ⚠️ 此时 `fip` 卷是空的。如果此时断电重启，设备会回到原厂 U-Boot
> （因为 `bootloader` 分区没动），原厂系统仍能启动。这是安全兜底。

### 步骤 2：写入 U-Boot

**Web → Flash → Flash U-Boot → 选择 `*-bl31-u-boot.fip` → Upload and write**

写入 UBI 的 `fip` 卷。

**关键**：我们的 FIP 不带证书，BL2 必须跳过验签。如果 `bl2_main` 没有调用
`dyn_disable_auth()`，会看到：

```
ERROR:   Authentication failed
```

release 里的产物已打过补丁。

### 步骤 3：写入系统

**Web → Flash → Install system → 选择 `*-ubi-squashfs-sysupgrade.itb`**

写入 `fit` 卷并创建 `rootfs_data`，然后 **Boot system**。

---

## 五、刷 BL2（最后一步，风险最高）

**只有在前面都验证通过后才做。**

**Web → Flash → Flash BL2 → 选择 `*-preloader.bin`**

替换 `bootloader` 分区（`0x0`，2.5 MiB）。

### 刷 BL2 之前的检查清单

1. ✅ `factory` 已备份且 md5 对得上
2. ✅ `bootloader` 已备份
3. ✅ 新 U-Boot 已能在 UBI 里正常启动（步骤 2、3 已通过）
4. ✅ 有 XMODEM 救援手段（见第六节）
5. ✅ **BL2 能识别你的闪存**（见下方警告）

### ⚠️ 闪存识别警告

BL2 有独立的 NAND 芯片表：

```c
// tf-a/plat/ecnt/common/drivers/flash/spi_nand_flash_table.c
mfr 0x01 + dev 0x25   SkyHigh S35ML02G3
mfr 0xCD + dev 0xA1   FORESEE FS35ND01GD1F1   1 Gbit
mfr 0xCD + dev 0xA2   FORESEE FS35ND02GS2F1   2 Gbit
mfr 0xCD + dev 0xB2   FORESEE FS35ND02GD1F1   2 Gbit
```

**如果机器的芯片 ID 不在表里，刷 BL2 会直接变砖。**

先刷 FIP（步骤 2）不动 BL2，观察新 U-Boot 是否能识别闪存：

```
spi_nand nand@0: ... found
spi-nand: 256 MiB, block size: 128 KiB, page size: 2048, OOB size: ...
```

能识别再刷 BL2；不能识别就在 `foresee.c` 补表项重编。

### 为什么 BL2 和 FIP 必须配套

原厂 FIP 带 6 张证书，我们的不带。四种组合只有一种能跑：

| BL2 | FIP | 结果 |
| --- | --- | --- |
| 原厂 | 原厂 | ✅ |
| 原厂 | 我们的 | ❌ 验签失败 |
| 我们的（未打补丁） | 我们的 | ❌ 验签失败 |
| **我们的（已打补丁）** | **我们的** | ✅ |

**所以 BL2 和 FIP 必须一起换。**

---

## 六、救援：X 模式（XMODEM）

**这是最关键的保命手段。** 只要 BL2 还能跑，就不怕刷坏 U-Boot。

BL2 找不到有效 FIP 时会请求：

```
Press 'x' for XMODEM download...
```

**FIP 只进内存，不写 flash**，可无限次重试。

用 SecureCRT / Tera Term 的 XMODEM 发送功能，或：

```powershell
.\tools\xmodem-send.ps1 -Port COM4 -File <fip> -WaitForPrompt
```

### 原厂 U-Boot 侧的免刷测试

原厂 U-Boot 有 `tftpboot` 和 `go`，可以在内存里试跑我们的 U-Boot：

```sh
setenv ipaddr 192.168.0.1
setenv serverip 192.168.0.2
tftpboot 0x90000000 u-boot.bin
go 0x90000000
```

**全程不碰 flash**，用来验证 U-Boot 能否识别闪存、网络、Web 恢复。
断电即恢复原状。

原厂也支持 `loadx`（XMODEM）作为无网络时的替代。

---

## 七、回滚到原厂

### 7.1 只回滚系统

Web → Install system → 上传原厂 sysupgrade

### 7.2 完整回滚

需要原厂 `factory` / `bootloader` 备份：

```sh
tftpboot 0x90000000 bootloader.bin
mtd erase bootloader
mtd write bootloader 0x90000000 0x0 0x280000
```

或 Web → Advanced → **Restore entire Flash**（需要整片备份）。

### 7.3 原厂 bootflag

```sh
bootflag read
```

> ⚠️ `bootflag swap` 会翻转全部四个标志位，**不要在不确定时用**。

---

## 八、常见问题

| 现象 | 原因 | 解决 |
| --- | --- | --- |
| `Authentication failed` | BL2 有验签但 FIP 无证书 | 用打过补丁的 BL2 |
| `cannot attach mtdN` | DTS 分区表与实际不符 | 用 release 里的 DTS |
| `bad image sequence number` | UBI 头是别的实例留下的 | Rebuild UBI |
| Web 打不开 | IP 网段不对 | PC 设 192.168.0.2/24 |
| `TFTP error -1` | serverip 不对 | `setenv serverip <PC_IP>` |
| 串口无输出 | 波特率/接线 | 115200 8N1，确认 TX/RX/GND |
| 网络很慢 | PHY 协商到 10baseT HDX | 原厂 U-Boot 也这样，属已知现象 |
| `sf probe` 报 `-19` | 芯片挂在 NAND 驱动上，不走 spi-nor | 正常，用 `mtd` 命令 |
| `sspi` 报 `-38` | 通用 DM SPI 层不支持 | 正常，无法直接读 READID |
| 登录后命令丢失字符 | 登录期间发了回车 | 看到 `UserName:`/`Password:` 后停手 |

---

## 九、编译

### 9.1 云编译

fork 仓库 → Actions → **Build HG5580A UBI + U-Boot** → Run workflow

### 9.2 本地编译 U-Boot

```sh
git clone https://github.com/Li-tian-suo/uboot-an758x-HG5380A
cd uboot-an758x-HG5380A

export CROSS_COMPILE=/path/to/aarch64-linux-musl-
export ARM32_CROSS_COMPILE=/path/to/arm-none-eabi-
export MBEDTLS_DIR=/path/to/mbedtls-3.4.1
export BUILD_JOBS=$(nproc)

./scripts/build-an758x.sh hg5580a
```

**两个坑**：

1. `MBEDTLS_DIR` 必须是含 `library/hash_info.c` 的完整源码树（3.4.x）。
   3.6.x 删掉了这个文件，TF-A 的 `mbedtls_common.mk:76` 硬编码引用它。
   OpenWrt 的 `build_dir/.../mbedtls-*` 是 CMake 配置过的目录，也不行。
2. `TCSUPPORT_*` 变量不会自动变成 `-D`，必须在 `tf-a/Makefile` 里
   `add_define` 注册，否则验签补丁会被预处理器删掉。

### 9.3 验证 BL2 跳过验签

```sh
objdump -d build/hg5580a/tf-a/build/en7523/release/bl2/bl2.elf \
  | awk '/<bl2_main>:/{f=1} f{print} f&&/^$/{c++} c>1{exit}' \
  | grep dyn_disable_auth
```

必须出现 `bl dyn_disable_auth`，否则 BL2 会拒绝无证书的 FIP。

---

## 十、验证清单

```sh
# 1. U-Boot 启动
#    串口应看到 "U-Boot HG5580A" 菜单

# 2. Web 可访问
curl http://192.168.0.1/api/device
#    -> {"model":"FiberHome HG5580A","flash_bytes":268435456,...}

# 3. 分区表正确
curl http://192.168.0.1/api/storage
#    -> bootloader/bootflag/KernelA/RootfsA/KernelB/RootfsB/ubi/
#       frameworka/frameworkb/factory

# 4. 系统启动后
cat /proc/mtd           # 10 个分区
mount | grep overlay    # overlayfs on / (UBI)
uname -a
```
