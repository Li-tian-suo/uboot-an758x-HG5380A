# WSL 上编译 OpenWrt 的路径陷阱

> 两个会让构建看起来「完全坏掉」的环境问题，以及它们的真实原因

---

## 1. 症状一：prereq 检查说所有工具都缺失

```
Checking 'gcc'... ok.
Checking 'g++'... failed.
Checking 'python3'... ok.
Checking 'file'... failed.
Checking 'egrep'... failed.
Checking 'getopt'... failed.
Checking 'realpath'... failed.
Checking 'stat'... failed.
Checking 'gzip'... failed.
Checking 'unzip'... failed.
Checking 'bzip2'... failed.
Checking 'wget'... failed.
Checking 'install'... failed.
Checking 'perl'... failed.

Build dependency: Please install GNU 'tar'
Build dependency: Please install GNU 'find'
Build dependency: Please install GNU 'bash'
Build dependency: Please install 'xargs' that supports '-r/--no-run-if-empty'
Build dependency: Please install GNU 'patch'
...

Prerequisite check failed. Use FORCE=1 to override.
```

**矛盾点**：`gcc`、`python3`、`which` 能找到，`g++`、`tar`、`find`、`bash`、
`grep`、`gzip` 找不到 —— 但它们都在 `/usr/bin`，交互式 shell 里全都能解析。

### 真实原因：带空格的 Windows 路径

WSL 默认把 Windows 的 PATH 追加进 Linux 的 PATH：

```
$ echo $PATH
/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/usr/lib/wsl/lib:/mnt/c/Program Files/Microsoft MPI/Bin/:/mnt/c/Windows/system32:...
                                                                                            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 有空格
```

`include/toplevel.mk` 这样处理它：

```makefile
path:=$(subst :,$(space),$(PATH))
export ORIG_PATH:=$(if $(ORIG_PATH),$(ORIG_PATH),$(PATH))
export PATH:=$(path)
```

第一步把 PATH 按 `:` 换成空格，变成 make 的**词列表**。但 make 的词列表本身是
**按空白分隔**的，所以：

```
/mnt/c/Program Files/Microsoft MPI/Bin/
```

被拆成两个词：

```
/mnt/c/Program
Files/Microsoft
MPI/Bin/
```

之后重新 join 成 PATH 时，整个路径串已经错位并被截断，`/usr/bin` 等系统目录
不再位于正确位置或被吞掉。

**为什么 `gcc` 和 `python3` 反而能找到**：它们可能通过 `/usr/local/bin`（在错误
发生前的位置）或 ccache 符号链接命中，纯属运气。

### 修法

构建时只传 POSIX 路径：

```bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
make -j$(nproc)
```

OpenWrt 构建不需要 Windows 侧的任何可执行文件。**推荐做法**是在 `/etc/wsl.conf`
里永久关掉互操作：

```ini
[interop]
appendWindowsPath = false
```

然后 `wsl --shutdown` 重开。

---

## 2. 症状二：`staging_dir` 半残导致 prereq 反复失败

清掉 `staging_dir` 之后，`make` 会在第一步就失败，而且**删掉 `.prereq-build`
标记也没有用**，因为失败原因不是缓存：

```
staging_dir/host/          只有 bin/，而且是空的
build_dir/                 完全为空
```

`Makefile:20` 会无条件把 `<TOPDIR>/staging_dir/host/bin` 加到 PATH 最前面：

```makefile
export PATH:=$(if $(STAGING_DIR),$(abspath $(STAGING_DIR)/../host/bin),$(TOPDIR)/staging_dir/host/bin):$(PATH)
```

目录存在但是空的，不影响解析（找不到就继续往后找），所以这不是根因 ——
**根因始终是上面第 1 条的 PATH 拆分问题**。但半残的 `staging_dir` 会让
增量编译永远无法恢复，必须连带清掉：

```bash
rm -rf staging_dir build_dir tmp
rm -f .config.old
# .config 要保留，它记录了设备与包选择
```

---

## 3. 症状三：`new_backup` 结尾的 vhdx 异常 / `Wsl/Service/CreateInstance/E_FAIL`

先查磁盘，**不要先怀疑 WSL 本体**：

```powershell
Get-Volume -DriveLetter H
```

如果 `SizeRemaining` 接近 0，WSL 起不来是因为写不了 `ext4.vhdx`。清理空间即可。

如果空间正常但仍然 `E_FAIL`：

```powershell
wsl --shutdown
Get-Service WSLService,LxssManager,vmcompute,HvHost
Start-Service WSLService
```

`LxssManager` 是 Stopped 属正常（按需启动）。`WSLService` 必须是 Running。

---

## 4. 工具链丢失后的补救

OpenWrt 的 `staging_dir/toolchain-*` 在树被清理后会消失。两条路：

**A. 重跑 make**（推荐）——工具链会由 `toolchain/` 目标自动重建，1–2 小时。

**B. 用发行版交叉编译器**（只想编 U-Boot 时够用）：

```bash
apt-get install -y gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
export CROSS_COMPILE=aarch64-linux-gnu-
```

注意：**换了 `CROSS_COMPILE` 之后不能复用旧的 `O=` 构建目录**，kconfig 会把
编译器路径和版本写进 `include/config/auto.conf`，混用会报：

```
Kconfig:91: syntax error
Kconfig:90: invalid option
```

用全新的 `O=` 目录即可。

---

## 5. 检查清单

在 WSL 上开始 OpenWrt 构建之前：

```bash
# 1. PATH 里不能有空格
echo "$PATH" | tr ':' '\n' | grep -c ' '      # 必须是 0

# 2. 必需工具全部可解析
for c in g++ gcc git rsync tar find bash grep gzip unzip bzip2 \
         perl file install realpath stat getopt egrep seq patch \
         diff xargs which wget make; do
  command -v "$c" >/dev/null || echo "MISSING: $c"
done

# 3. 磁盘空间
df -h /        # 建议 > 40 GB

# 4. 大小写敏感文件系统
touch /tmp/t.fs && [ ! -f /tmp/t.FS ] && echo 'case-sensitive OK'

# 5. umask
umask          # 需要 0022
```
