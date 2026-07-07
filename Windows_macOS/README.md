# EXT2 File System Simulator — Windows & macOS 用户态版本

用 C 语言实现的用户态 ext2 文件系统模拟器。使用单个普通文件 `./Ext2` 作为虚拟磁盘，提供交互式 shell 操作。

## 快速开始

```bash
make           # 编译
make run       # 编译并运行
make clean     # 清理编译产物
make distclean # 清理编译产物 + 虚拟磁盘（彻底重置）
```

首次运行自动创建虚拟磁盘，默认用户 `root` / `root`。

## 环境要求

- GCC / Clang
- Windows (MinGW / WSL) 或 macOS

## 原理

```
┌─────────────────────────┐
│  shell.c (交互命令)      │  ← 你在这里
├─────────────────────────┤
│  file_ops / directory   │  ← ext2 逻辑（inode、目录、权限）
├─────────────────────────┤
│  bitmap.c               │  ← 位图分配回收
├─────────────────────────┤
│  disk_io.c              │  ← fread/fwrite 读写 ./Ext2 普通文件
└─────────────────────────┘
         │
    ./Ext2（一个普通文件，模拟磁盘）
```

## 与 Linux 内核版本的关系

本文件夹是**用户态模拟版本**，底层用 `fread`/`fwrite` 操作普通文件。对应的 Linux 内核模块版本（通过 VFS 注册，操作真块设备）在并列的 `ubuntu/` 目录中。两个版本共享相同的 ext2 核心逻辑（位图、inode、目录结构），仅 I/O 层和接口层不同。

## 命令列表

| 命令 | 说明 |
|------|------|
| `ls` | 列出目录 |
| `cd` | 切换目录 |
| `mkdir` | 创建目录 |
| `touch` | 创建文件 |
| `open` / `close` | 打开 / 关闭文件 |
| `read` | 读取文件 |
| `write` / `write >>` | 覆盖 / 追加写入 |
| `cat` | 打印文件内容 |
| `cp` / `mv` | 复制 / 移动 |
| `rm` / `rmdir` | 删除文件 / 目录 |
| `chmod` / `chown` | 修改权限 / 所有者 |
| `useradd` / `passwd` / `login` | 用户管理 |
| `ckdisk` / `format` / `volname` | 磁盘工具 |
| `help` | 全部命令 |
| `quit` | 退出 |

输入 `help` 查看完整用法。

## 文件结构

```
Windows_macOS/
├── include/                    # 公共头文件
│   ├── ext2_types.h            # 数据结构 (super_block, inode, dir_entry...)
│   ├── ext2_constants.h        # 磁盘布局常量
│   ├── fs_context.h            # 全局上下文 & 生命周期 API
│   ├── disk_io.h               # 层 0: 磁盘物理 I/O
│   ├── bitmap.h                # 层 1: 位图分配 / 回收
│   ├── directory.h             # 层 2: 目录项操作
│   ├── file_ops.h              # 层 2: 文件操作
│   ├── user.h                  # 用户管理 API
│   └── main.h                  # 公共命令接口
├── src/                        # 源文件
│   ├── main.c                  # 入口
│   ├── shell.c                 # 交互循环 & 命令解析
│   ├── context.c               # ctx 所有者 & 生命周期
│   ├── disk_io.c               # 层 0: 磁盘 I/O
│   ├── bitmap.c                # 层 1: 位图管理
│   ├── directory.c             # 层 2: 目录操作
│   ├── file_ops.c              # 层 2: 文件操作层
│   ├── file_cmd.c              # 层 3: cp / mv / chmod / chown
│   └── user.c                  # 用户管理子系统
├── output/                     # 编译输出
├── Makefile
└── README.md
```

## 磁盘布局

```
+-----------+-----------+-----------+-----------+-----------+-----------+
| Super     | Group     | Block     | Inode     | Inode     | Data      |
| Block     | Desc      | Bitmap    | Bitmap    | Table     | Blocks    |
| (512B)    | (512B)    | (512B)    | (512B)    | (256KB)   | (~2MB)    |
+-----------+-----------+-----------+-----------+-----------+-----------+
Block 0      Block 1     Block 2     Block 3     Block 4-515   Block 516+
```

- **块大小**: 512 bytes · **总块数**: 4612 (~2.3 MB) · **数据块数**: 4096
- **最大文件**: 134 KB (12 direct + 256 single-indirect)；理论最大 ~16TB (含二级/三级间接块，磁盘容量为实际瓶颈)
- **最大 inode 数**: 4096 · **最大文件名**: 8 字符

## 开发路线 / Development Roadmap

### 第一阶段：补齐课程要求（满足 Requirements.md 全部条目）

| 优先级 | 任务 | 状态 |
|--------|------|------|
| 🔴 P0 | **login 用户登录系统** — 用户数据结构、登录认证、会话管理、i_uid/i_gid 联动 | ✅ 已完成 |
| 🔴 P0 | **用户信息持久化** — 磁盘上模拟 `/etc/passwd` 和 `/etc/shadow`，默认 root 用户 | ✅ 已完成 |
| 🟡 P1 | **ls 列出物理地址** — 显示文件数据块在磁盘上的块号 | ✅ 已完成 |
| 🟡 P1 | **时间戳更新** — 读写/创建时更新 i_atime/i_mtime/i_ctime | ✅ 已完成 |
| 🟠 P2 | **间接块寻址** — i_block[8] → i_block[15]（12 直接 + 1 间接 + 1 双间接 + 1 三间接） | ✅ 已完成 |
| 🟠 P2 | **超级块空闲计数** — sb_free_blocks_count / sb_free_inodes_count | ✅ 已完成 |

### 第二阶段：补全 ext2 固有功能

| 任务 | 状态 |
|------|------|
| **多块组支持** | ⬜ 未开始 |
| **完整权限系统** — i_mode 实现 user/group/other 三级 rwxrwxrwx | ✅ 已完成 |
| **硬链接** — ln 命令，i_links_count | ⬜ 未开始 |
| **chmod / chown 命令** | ✅ 已完成 |
| **绝对路径 & 多级路径** — mkdir a/b/c、cd /home | ✅ 已完成 |
| **文件追加写模式** — write >> | ✅ 已完成 |
| **cp / mv 命令** — 文件复制和移动（跨目录） | ✅ 已完成 |
| **目录项变长支持** — rec_len 支持变长文件名（>8 字符） | ⬜ 未开始 |
| **卷标读写** — volname 命令 | ✅ 已完成 |

### 第三阶段：体验与健壮性改进

| 任务 | 状态 |
|------|------|
| **Makefile** — make / make clean / make debug / make run | ✅ 已完成 |
| **文件句柄管理** — fp 统一由 fs_init 打开、fs_shutdown 关闭 | ✅ 已完成 |
| **代码模块化** — 1070 行巨石拆分为 7 个模块，依赖单向无环 | ✅ 已完成 |
| **编译零警告** — 26 warnings → 0 | ✅ 已完成 |
| **输入安全** — scanf("%s") → fgets + 边界检查 | ⬜ 未开始 |
| **fflush(stdin) 替换** — 平台兼容方式清空输入缓冲 | ⬜ 未开始 |
| **错误码返回** — fread/fwrite/fseek 添加错误检查 | ⬜ 未开始 |
| **help 命令实现** | ✅ 已完成 |
| **cat → touch 重命名** | ✅ 已完成 |
| **write 结束符可配置** — # → Ctrl+D (EOF) | ⬜ 未开始 |

### 第四阶段：扩展功能（加分项 / 兴趣探索）

| 任务 | 说明 | 工作量 |
|------|------|--------|
| **FUSE 挂载** | 通过 libfuse 将模拟文件系统挂载为真实目录，系统原生 ls、cat、vim 操作 | 大 |
| **ext3 日志 (Journal)** | 增加日志区域，实现 journal → checkpoint 写入流程，支持崩溃恢复 | 大 |
| **快照 (Snapshot)** | 基于 COW 实现文件系统快照和回滚 | 大 |
| **fsck 磁盘检查修复** | 扫描位图一致性、inode 链完整性，自动修复 | 大 |
| **磁盘碎片整理** | 扫描碎片率，defrag 命令重排数据块 | 中 |
| **配额管理 (Quota)** | 限制每个用户的最大文件数和磁盘使用量 | 中 |
| **ACL 访问控制列表** | 超出传统 rwx 的细粒度权限控制 | 中 |
| **加密文件系统** | 对数据块进行 AES 等透明加解密 | 中 |
| **GUI 文件管理器** | GTK/Qt/ncurses 可视化文件浏览和管理界面 | 中 |
| **性能测试 & Benchmark** | 读写压力测试，测量 IOPS、吞吐量，对比真实 ext2 | 小 |
| **多线程并发支持** | 文件锁（flock）支持多线程安全读写 | 中 |

## 仍存在的问题

| # | 严重程度 | 位置 | 问题 |
|---|----------|------|------|
| 1 | **中** | `shell.c` | `scanf("%s",temp)` 无缓冲区大小限制 |
| 2 | **高** | `shell.c` / `file_ops.c` | `fflush(stdin)` 是 C 标准的未定义行为 |
| 3 | **中** | `file_ops.c:rmdir()` | 递归删除时覆盖 current_path/current_dir |
| 4 | **低** | `file_ops.c:file_write()` | 以 `#` 作为输入结束符，文件内容不能包含 `#` |
