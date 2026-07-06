# EXT2 File System Simulator

一个用 C 语言实现的用户态 ext2 文件系统模拟器。使用单个普通文件 (`./Ext2`) 作为虚拟磁盘，实现了超级块、块组描述符、inode 表、块/inode 位图、目录项等 ext2 核心数据结构，提供交互式 shell 进行文件系统操作。

A user-space ext2-like file system simulator written in C. It uses a single regular file (`./Ext2`) as a virtual disk and implements core ext2 concepts. Provides an interactive shell for file system operations.

## 快速开始 / Quick Start

### 环境要求 / Prerequisites

- GCC or any C compiler
- Linux / macOS / WSL

### 编译 / Build

```bash
gcc -o output/ext2fs main.c init.c
```

### 运行 / Run

```bash
./output/ext2fs
```

首次运行时，程序会自动创建虚拟磁盘文件 `./Ext2` 并初始化文件系统。提示符如下：

```
[root@ /]#
```

## 命令列表 / Commands

| 命令 | 用法 | 说明 |
|------|------|------|
| `ls` | `ls` | 列出当前目录内容 |
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
| `quit` | `quit` | 退出程序 |

### 示例 / Example Session

```
[root@ /]# mkdir docs
[root@ /docs/]# touch readme.txt
[root@ /docs/]# open readme.txt
File readme.txt opened!
[root@ /docs/]# write readme.txt
Hello World!#
[root@ /docs/]# read readme.txt
Hello World!
[root@ /docs/]# close readme.txt
File readme.txt closed!
[root@ /docs/]# ls
items          type           mode           size
.             <DIR>          r_w_x          ----
..            <DIR>          r_w_x          ----
readme.txt    <FILE>         r_w__          12 bytes
[root@ /docs/]# cd ..
[root@ /]# ckdisk
volume name       : EXT2FS
disk size         : 4612(blocks)
blocks per group  : 4612(blocks)
ext2 file size    : 2306(kb)
block size        : 512(kb)
[root@ /]# quit
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
├── main.h          # 公共函数声明 / Public declarations
├── main.c          # 入口、命令解析、交互循环 / Entry point, command parser
├── init.h          # 常量、结构体、静态变量、内部声明 / Constants, structs, internals
├── init.c          # 核心文件系统实现 / Core implementation
├── output/         # 编译输出目录 / Build output
│   └── ext2fs      # 编译后的二进制 / Compiled binary
└── README.md       # 本文件 / This file
```

## 发现的问题 / Issues Found

### 已修复的关键 Bug / Fixed Critical Bugs

| # | 位置 | 问题 |
|---|------|------|
| 1 | `init.c:remove_inode()` | ~~删除 inode 时错误地清除了 block 位图 (`bitbuf`) 而非 inode 位图 (`ibuf`)，导致 block 位图损坏~~ → **已修复** |
| 2 | `init.c:search_file()` | ~~后置 `++` 导致 off-by-one：当文件在 `fopen_table[15]` 时返回"未找到"~~ → **已修复** |
| 3 | `init.c:del()` | ~~`inode_area[i]` 数组越界访问 — `inode_area` 大小仅为 1，`i` 是 inode 号，读取垃圾内存~~ → **已修复** |
| 4 | `init.c:read_file()` | ~~`i_size > 512` 时读取超出 `Buffer` 边界~~ → **已修复** |

### 仍存在的问题 / Remaining Issues

| # | 严重程度 | 问题 |
|---|----------|------|
| 1 | **高** | 所有 `scanf("%s",...)` 无缓冲区大小限制 — 输入过长会导致**栈溢出** |
| 2 | **高** | `fflush(stdin)` 是 C 标准的**未定义行为**，跨平台兼容性差 |
| 3 | **中** | `rmdir()` 递归删除时直接覆盖 `current_path` 和 `current_dir`，导致路径损坏 |
| 4 | **中** | `update_*`/`reload_*` 系列函数重复 `fopen` 而不 `fclose`，导致**文件句柄泄漏** |
| 5 | **低** | `cat()` 函数名有误导性 — 实际是创建文件 (`touch`)，不是显示内容 |
| 6 | **低** | `help` 命令已声明但未实现，主循环也未注册 |
| 7 | **低** | `sleep()` 函数已声明但无实现 |
| 8 | **低** | `main.c` 末尾注释掉的代码中，`close` 命令错误地调用了 `open_file()` (copy-paste 错误) |
| 9 | **低** | 所有静态函数声明放在头文件中，导致每个 `.c` 文件各有一份拷贝，并产生大量 compiler warning |
| 10 | **低** | 不支持多级路径 (`mkdir /a/b` 无效) |
| 11 | **低** | `write` 以 `#` 作为输入结束符，文件内容不能包含 `#` |
| 12 | **低** | `current_path` 格式不规范 (`[root@ /` 缺少 `]`) |
| 13 | **低** | 无 Makefile |
| 14 | ~~低~~ ✅ | ~~中文注释为 GBK 编码，非中文环境显示乱码~~ → 已修复为 UTF-8 中文 |

## 后续开发计划 / Development Roadmap

### 第一阶段：补齐课程要求（满足 Requirements.md 全部条目）

| 优先级 | 任务 | 说明 | 涉及文件 | 工作量 |
|--------|------|------|----------|--------|
| 🔴 P0 | **login 用户登录系统** | 设计用户数据结构（uid、用户名、密码哈希），实现登录认证、会话管理，与 inode 的 `i_uid`/`i_gid` 联动 | `main.c`、新建 `user.c/.h` | 大 |
| 🔴 P0 | **用户信息持久化** | 在磁盘上模拟 `/etc/passwd` 和 `/etc/shadow`，格式化和初始化时创建默认 root 用户 | `init.c`、新建 `user.c` | 中 |
| 🟡 P1 | **ls 列出物理地址** | `ls()` 输出增加一列，显示文件数据块在磁盘上的块号（`inode.i_block[]`） | `init.c:ls()` | 小 |
| 🟡 P1 | **时间戳更新** | 引入 `<time.h>`，在 `read_file` 时更新 `i_atime`，在 `write_file`/`cat`/`mkdir` 时更新 `i_mtime`/`i_ctime` | `init.c` | 小 |
| 🟠 P2 | **间接块寻址** | `i_block[8]` → `i_block[15]`（12 直接 + 1 间接 + 1 双间接 + 1 三间接），重构 `alloc_block`、`read_file`、`write_file`、`del` 的块寻址逻辑 | `init.h`、`init.c` | 大 |
| 🟠 P2 | **超级块添加空闲计数** | `super_block` 增加 `sb_free_blocks_count` 和 `sb_free_inodes_count` 字段 | `init.h`、`init.c` | 小 |

### 第二阶段：补全 ext2 固有功能（真实 ext2 有但当前未实现）

| 任务 | 说明 | 工作量 |
|------|------|--------|
| **多块组支持** | 当前仅 1 个块组。需支持多个块组，每组有独立的 GDT 副本和位图，超级块在每个块组有备份 | 大 |
| **完整权限系统** | 当前 `i_mode` 仅用低 3 位（rwx），应在 login 基础上实现 user/group/other 三级权限（`rwxrwxrwx` 共 9 位），`open_file` 时检查执行权限 | 中 |
| **硬链接** | `i_links_count` 字段已定义但未使用。实现 `ln` 命令，删除时仅在计数归零时释放 inode | 中 |
| **chmod / chown 命令** | 修改文件的权限位和所有者，需与 login 用户系统联动 | 中 |
| **绝对路径 & 多级路径** | 支持 `mkdir /home/user/docs`、`cd /home`、`ls /` 等绝对路径和嵌套路径操作 | 中 |
| **文件追加写模式** | 当前 `write` 为覆盖写，增加 `>>` 追加模式（类似 `echo xxx >> file`） | 小 |
| **cp / mv 命令** | 文件复制和移动（跨目录） | 中 |
| **目录项变长支持** | `dir_entry.rec_len` 字段已定义但使用固定 16B。利用 `rec_len` 支持变长文件名（>8 字符）和目录项删除时的空间合并 | 中 |
| **卷标读写** | `super_block.sb_volume_name` 和 `group_desc.bg_volume_name` 已定义，增加 `volname` 命令读/写卷标 | 小 |
| **根保留块** | ext2 允许为 root 保留一定比例的数据块，防止普通用户占满磁盘 | 小 |

### 第三阶段：体验与健壮性改进

| 任务 | 说明 | 工作量 |
|------|------|--------|
| **Makefile** | 编写 `Makefile`，支持 `make` / `make clean` / `make debug` | 小 |
| **输入安全** | 所有 `scanf("%s")` 改为 `fgets` + 边界检查，消除栈溢出风险 | 中 |
| **`fflush(stdin)` 替换** | 用 `while(getchar()!='\n')` 或平台兼容方式清空输入缓冲 | 小 |
| **文件句柄管理** | `update_*`/`reload_*` 函数改为使用全局 `fp` 而非重复 `fopen`，关闭时统一 `fclose` | 中 |
| **错误码返回** | 所有函数返回值规范化，`fread`/`fwrite`/`fseek` 添加错误检查 | 中 |
| **`help` 命令实现** | 补全 `help()` 函数体，列出所有命令及用法 | 小 |
| **`cat` 重命名为 `touch`** | 当前 `cat` 实际语义是 `touch`（创建文件），应修正命名 | 小 |
| **提示符格式修正** | `[root@ /` → `[root@ /]#` 或 `root@fs:/#` | 小 |
| **write 结束符可配置** | `#` 改为 `Ctrl+D`（EOF）或 `.` 单行结束，避免文件内容受限 | 小 |
| **中文编码统一 UTF-8** | ✅ 已完成（本次修复） | — |

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
