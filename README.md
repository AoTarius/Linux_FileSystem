# EXT2 File System Simulator

一个用 C 语言实现的用户态 ext2 文件系统模拟器。使用单个普通文件 (`./Ext2`) 作为虚拟磁盘，实现了超级块、块组描述符、inode 表、块/inode 位图、目录项等 ext2 核心数据结构，提供交互式 shell 进行文件系统操作。

A user-space ext2-like file system simulator written in C. It uses a single regular file (`./Ext2`) as a virtual disk and implements core ext2 concepts. Provides an interactive shell for file system operations.

## 快速开始 / Quick Start

### 环境要求 / Prerequisites

- GCC or any C compiler
- Linux / macOS / WSL

### 编译 / Build

```bash
make           # 编译
make run       # 编译并运行
make debug     # Debug 模式编译（-g -O0）
make clean     # 清理编译产物（obj/ + output/）
make distclean # 清理编译产物 + 虚拟磁盘（./Ext2），彻底重置
```

> **注意**：`make clean` 只删二进制文件，不会动 `./Ext2`（虚拟磁盘）。
> 你在 shell 中创建的所有文件和目录都持久化在 `./Ext2` 里。
> 想从头开始，执行 `make distclean && make && make run`。


### 运行 / Run

```bash
./output/ext2fs
```

首次运行时，程序会自动创建虚拟磁盘文件 `./Ext2` 并初始化文件系统。默认创建一个 `root` 用户（密码 `root`）。登录提示符如下：

```
========================================
  EXT2 File System Simulator - Login
========================================
Available users:
  root (uid=0)

Username: root
Password: root

Welcome, root!

[root@/]#
```
> root (uid=0) 提示符以 `#` 结尾，普通用户以 `$` 结尾，与 Linux 一致。


## 命令列表 / Commands

| 命令 | 用法 | 说明 |
|------|------|------|
| `ls` | `ls` | 列出当前目录内容（含物理地址和修改时间） |
| `cd` | `cd <目录名>` | 切换目录 (`.` 当前, `..` 上级) |
| `mkdir` | `mkdir <目录名>` | 创建目录 |
| `touch` | `touch <文件名>` | 创建普通文件 |
| `open` | `open <文件名>` | 打开文件 (读写前必须执行) |
| `close` | `close <文件名>` | 关闭文件 |
| `read` | `read <文件名>` | 读取文件内容 |
| `write` | `write <文件名>` | 写入文件 (以 `#` 结束输入) |
| `rm` | `rm <文件名>` | 删除文件 |
| `rmdir` | `rmdir <目录名>` | 删除空目录 |
| `format` | `format` | 格式化磁盘 (清除所有数据) |
| `ckdisk` | `ckdisk` | 查看磁盘状态 |
| `whoami` | `whoami` | 查看当前登录用户信息 |
| `useradd` | `useradd <用户名>` | 创建新用户 (仅限 root) |
| `passwd` | `passwd` | 修改当前用户密码 |
| `login` | `login` | 重新登录 (切换用户) |
| `quit` | `quit` | 退出程序 |

### 示例 / Example Session

```
[root@/]# mkdir docs
[root@/docs/]# touch readme.txt
[root@/docs/]# open readme.txt
File readme.txt opened!
[root@/docs/]# write readme.txt
Hello World!#
[root@/docs/]# read readme.txt
Hello World!
[root@/docs/]# close readme.txt
File readme.txt closed!
[root@/docs/]# ls
items          type           mode         blocks             mtime            size
.              <DIR>          rwxr-xr-x         525  07-06 15:49:13          ----
..             <DIR>          rwxr-xr-x         524  07-06 15:49:13          ----
readme.txt     <FILE>         rw-r--r--         526  07-06 15:49:13            12 bytes
[root@/docs/]# cd ..
[root@/]# whoami
User: root  (uid=0, gid=0)
[root@/]# ckdisk
volume name       : EXT2FS
disk size         : 4612(blocks)
blocks per group  : 4612(blocks)
ext2 file size    : 2306(kb)
block size        : 512(kb)
[root@/]# quit
```

## 磁盘布局 / Disk Layout

```
+-----------+-----------+-----------+-----------+-----------+-----------+
| Super     | Group     | Block     | Inode     | Inode     | Data      |
| Block     | Desc      | Bitmap    | Bitmap    | Table     | Blocks    |
| (512B)    | (512B)    | (512B)    | (512B)    | (256KB)   | (~2MB)    |
+-----------+-----------+-----------+-----------+-----------+-----------+
Block 0     Block 1     Block 2     Block 3     Block 4-515  Block 516+
```

- **块大小 / Block size**: 512 bytes
- **总块数 / Total blocks**: 4612 (~2.3 MB)
- **数据块数 / Data blocks**: 4096
- **最大文件 / Max file size**: 4 KB (8 direct blocks × 512 bytes)
- **最大 inode 数 / Max inodes**: 4096
- **最大文件名 / Max filename length**: 8 字符

## 核心数据结构 / Data Structures

| 结构体 | 大小 | 说明 |
|--------|------|------|
| `super_block` | 32B | 卷名、磁盘大小、块大小 |
| `group_desc` | 32B | 位图位置、空闲块/inode 计数 |
| `inode` | 64B | 文件类型、权限、大小、时间戳、直接块指针(最多8个) |
| `dir_entry` | 16B | inode 号、文件名、文件类型(1=文件, 2=目录) |

## 文件结构 / File Structure

```
.
├── include/                    # 公共头文件
│   ├── ext2_types.h            # 数据结构 (super_block, inode, dir_entry...)
│   ├── ext2_constants.h        # 磁盘布局常量
│   ├── fs_context.h            # 全局上下文 & 生命周期 API
│   ├── disk_io.h               # 层 0: 磁盘物理 I/O
│   ├── bitmap.h                # 层 1: 位图分配 / 回收
│   ├── directory.h             # 层 2: 目录项操作
│   ├── file_ops.h              # 层 2: 文件操作
│   └── main.h                  # 公共命令接口 (供 shell 使用)
├── src/                        # 源文件
│   ├── main.c                  # 入口 (20 行)
│   ├── shell.c                 # 交互循环 & 命令解析 (66 行)
│   ├── context.c               # ctx 所有者 & 生命周期 (216 行)
│   ├── disk_io.c               # 层 0: 磁盘 I/O (131 行)
│   ├── bitmap.c                # 层 1: 位图管理 (111 行)
│   ├── directory.c             # 层 2: 目录操作 (181 行)
│   └── file_ops.c              # 层 2-3: 文件操作 & 删除 (350 行)
├── output/                     # 编译输出目录
│   └── ext2fs                  # 编译后的二进制 (~52 KB)
├── Makefile                    # 构建系统
└── README.md                   # 本文件
```

## 发现的问题 / Issues Found

### ✅ 已修复 (Phase 1-3 重构中解决)

| # | 问题 | 修复方式 |
|---|------|----------|
| 1 | 删除 inode 时错误清除 block 位图 | **已修复** |
| 2 | `fopen_table[15]` 时 off-by-one 返回"未找到" | **已修复** |
| 3 | `inode_area[i]` 数组越界访问 | **已修复** |
| 4 | `i_size > 512` 时读取超出 `Buffer` 边界 | **已修复** |
| 5 | 文件句柄泄漏：`update_*` 函数重复 `fopen` 无 `fclose` | **Phase 2 修复** — `fp` 在 `fs_init()` 中打开一次，`fs_shutdown()` 关闭 |
| 6 | 首次运行 crash：`Ext2` 不存在时 `fp` 处于关闭状态 | **Phase 2 修复** — `initialize_memory` 在 `fs_mkfs` 后正确重新打开 `fp` |
| 7 | 26 个编译警告（static 函数声明在头文件中） | **Phase 1 修复** — static 函数声明移除，编译 0 warning |
| 8 | 无 Makefile | **Phase 1 修复** |
| 9 | `main.c` 末尾 147 行死代码 | **Phase 1 移除** |
| 10 | 中文注释 GBK 编码 | **已修复为 UTF-8** |
| 11 | `sleep()` 函数声明但无实现 | **Phase 3 移除**（不再声明） |

### 仍存在的问题 / Remaining Issues

| # | 严重程度 | 位置 | 问题 |
|---|----------|------|------|
| 1 | **高** | `shell.c` | 所有 `scanf("%s",...)` 无缓冲区大小限制 — 输入过长会导致**栈溢出** |
| 2 | **高** | `shell.c` / `file_ops.c` | `fflush(stdin)` 是 C 标准的**未定义行为** |
| 3 | **中** | `file_ops.c:rmdir()` | 递归删除时覆盖 `current_path` / `current_dir`，导致非空目录删除失败 |
| 4 | **低** | `main.h` | `cat()` 实际语义是 `touch`（创建文件），函数名有误导性 |
| 5 | **低** | — | `help` 命令已声明但未实现 |
| 6 | **低** | — | 不支持多级路径 (`mkdir /a/b` 无效) |
| 7 | **低** | `file_ops.c:file_write()` | 以 `#` 作为输入结束符，文件内容不能包含 `#` |

## 后续开发计划 / Development Roadmap

### 第一阶段：补齐课程要求（满足 Requirements.md 全部条目）

| 优先级 | 任务 | 说明 | 涉及文件 | 工作量 |
|--------|------|------|----------|--------|
| ~~🔴 P0~~ | ~~**login 用户登录系统**~~ | ~~设计用户数据结构，实现登录认证、会话管理，与 inode 的 `i_uid`/`i_gid` 联动~~ ✅ 已完成 | `shell.c`、`src/user.c`、`include/user.h` | 大 |
| ~~🔴 P0~~ | ~~**用户信息持久化**~~ | ~~在磁盘上模拟 `/etc/passwd` 和 `/etc/shadow`，初始化时创建默认 root 用户~~ ✅ 已完成 | `context.c`、`src/user.c` | 中 |
| ~~🟡 P1~~ | ~~**ls 列出物理地址**~~ | ~~`ls()` 输出增加一列，显示文件数据块在磁盘上的块号~~ | ~~`directory.c:dir_list()`~~ | ~~小~~ ✅ 已完成 |
| ~~🟡 P1~~ | ~~**时间戳更新**~~ | ~~引入 `<time.h>`，在读写/创建时更新 `i_atime`/`i_mtime`/`i_ctime`~~ | ~~`file_ops.c`、`directory.c`~~ | ~~小~~ ✅ 已完成 |
| 🟠 P2 | **间接块寻址** | `i_block[8]` → `i_block[15]`（12 直接 + 1 间接 + 1 双间接 + 1 三间接） | `ext2_types.h`、`bitmap.c`、`file_ops.c` | 大 |
| 🟠 P2 | **超级块添加空闲计数** | `super_block` 增加 `sb_free_blocks_count` / `sb_free_inodes_count` | `ext2_types.h`、`context.c` | 小 |

### 第二阶段：补全 ext2 固有功能

| 任务 | 说明 | 涉及文件 | 工作量 |
|------|------|----------|--------|
| **多块组支持** | 当前仅 1 个块组，需支持多块组（独立 GDT 副本和位图） | `ext2_types.h`、`disk_io.c`、`context.c` | 大 |
| ~~**完整权限系统**~~ | ~~`i_mode` 实现 user/group/other 三级 `rwxrwxrwx` 共 9 位~~ | ~~`file_ops.c`、`directory.c`~~ | ~~中~~ ✅ 已完成 |
| **硬链接** | `i_links_count` 字段已定义未使用，实现 `ln` 命令 | `file_ops.c`、`directory.c` | 中 |
| **chmod / chown 命令** | 修改文件的权限位和所有者 | `file_ops.c` | 中 |
| **绝对路径 & 多级路径** | 支持 `mkdir /home/user/docs`、`cd /home` 等 | `directory.c` | 中 |
| **文件追加写模式** | `write` 增加 `>>` 追加模式 | `file_ops.c:file_write()` | 小 |
| **cp / mv 命令** | 文件复制和移动（跨目录） | `file_ops.c` | 中 |
| **目录项变长支持** | 利用 `rec_len` 支持变长文件名（>8 字符） | `ext2_types.h`、`directory.c` | 中 |
| **卷标读写** | 增加 `volname` 命令读/写卷标 | `context.c` | 小 |

### 第三阶段：体验与健壮性改进

| 任务 | 说明 | 涉及文件 | 工作量 |
|------|------|----------|--------|
| ✅ **Makefile** | 支持 `make` / `make clean` / `make debug` / `make run` | `Makefile` | ✅ 已完成 |
| ✅ **文件句柄管理** | `fp` 统一由 `fs_init` 打开、`fs_shutdown` 关闭 | `context.c`、`disk_io.c` | ✅ 已完成 |
| ✅ **代码模块化** | 1070 行巨石拆分为 7 个模块，依赖单向无环 | 全部 `src/` | ✅ 已完成 |
| ✅ **编译零警告** | 26 warnings → 0 | — | ✅ 已完成 |
| **输入安全** | `scanf("%s")` → `fgets` + 边界检查 | `shell.c` | 中 |
| **`fflush(stdin)` 替换** | 平台兼容方式清空输入缓冲 | `shell.c`、`file_ops.c` | 小 |
| **错误码返回** | `fread`/`fwrite`/`fseek` 添加错误检查 | `disk_io.c` | 中 |
| **`help` 命令实现** | 补全 `help()` 函数体 | `shell.c` | 小 |
| **`cat` → `touch` 重命名** | 函数名反映实际语义 | `main.h`、`file_ops.c`、`shell.c` | 小 |
| ~~**提示符格式修正**~~ | ~~`[root@ /` → `[root@ /]#`~~ → 已随 login 系统修复，root 显示 `#`，普通用户显示 `$` | `context.c` | 小 |
| **write 结束符可配置** | `#` → `Ctrl+D` (EOF) | `file_ops.c` | 小 |

### 第四阶段：扩展功能（加分项 / 兴趣探索）

| 任务 | 说明 | 工作量 |
|------|------|--------|
| **fsck 磁盘检查修复** | 扫描位图一致性、inode 链完整性、目录树连通性，自动修复孤儿块和泄漏 inode | 大 |
| **FUSE 挂载** | 通过 libfuse 将模拟文件系统挂载为真实目录，用系统原生的 `ls`、`cat`、`vim` 操作 | 大 |
| **ext3 日志 (Journal)** | 在 ext2 基础上增加日志区域，实现 `journal → checkpoint` 写入流程，支持崩溃恢复 | 大 |
| **快照 (Snapshot)** | 基于 COW（写时复制）实现文件系统快照和回滚 | 大 |
| **磁盘碎片整理** | 扫描数据块碎片率，提供 `defrag` 命令重排数据块 | 中 |
| **配额管理 (Quota)** | 限制每个用户的最大文件数和磁盘使用量 | 中 |
| **ACL 访问控制列表** | 超出传统 rwx 的细粒度权限控制 | 中 |
| **加密文件系统** | 对数据块进行 AES 等透明加解密 | 中 |
| **网络共享 (NFS 简易版)** | 通过 Socket 将文件系统操作暴露为网络服务，多客户端共享 | 大 |
| **GUI 文件管理器** | 使用 GTK/Qt/ncurses 编写可视化的文件浏览和管理界面 | 中 |
| **性能测试 & Benchmark** | 编写读写压力测试，测量 IOPS、吞吐量，对比真实 ext2 | 小 |
| **多线程并发支持** | 当前为单线程，增加文件锁（`flock`）支持多线程安全读写 | 中 |

---

## 许可 / License

此项目为网上找到的教学实现，原始仓库未提供许可证信息。

This project appears to be an educational implementation found online. No license information was provided.
