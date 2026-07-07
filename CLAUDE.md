# CLAUDE.md

> EXT2 File System Simulator — 代码库导航与内部约定。
> 面向 Claude，每次新对话开始先读这个。
> 用户手册、命令列表、示例、Roadmap 见 `README.md`。

---

## 构建（详见 README）

```bash
make           # 编译（.o → obj/，二进制 → output/ext2fs）
make run       # 编译并运行
make clean     # 删 obj/ + output/（不动 ./Ext2）
make distclean # 删 obj/ + output/ + ./Ext2
```

零警告：`-Wall -Wextra -std=c99 -I./include`。

---

## 架构分层

```
main.c  →  shell.c  →  file_ops.c / directory.c / context.c
                           │            │              │
                           └───── bitmap.c ────┘       │
                                    │                   │
                                disk_io.c               │
                                    │                   │
                               ./Ext2 虚拟磁盘 ←────────┘
```

**依赖规则：单向向下，无循环。**

| 文件 | 行数 | 层 | 依赖 |
|------|------|:--:|------|
| `disk_io.c` | 131 | 0 | 无（只依赖 ext2_types/constants） |
| `bitmap.c` | 111 | 1 | `disk_io.c` |
| `directory.c` | 181 | 2 | `disk_io.c` + `bitmap.c` |
| `file_ops.c` | 350 | 2-3 | 以上全部 + `directory.c` |
| `context.c` | 216 | — | `disk_io.c`（初始化直接操作位图，不走 bitmap.c） |
| `shell.c` | 66 | 4 | `main.h`（公共 API） |
| `main.c` | 20 | 入口 | `fs_context.h`（fs_init/fs_shutdown） |

---

## 全局状态

所有状态收拢于 `context.c` 中定义的**单例**，其他模块通过 `extern` 直接访问：

```c
// fs_context.h
extern struct fs_context ctx;

// 各模块直接使用：
ctx.fp              // FILE*，fs_init 打开，fs_shutdown 关闭
ctx.sb              // super_block（替换旧 sb_block[0]）
ctx.gd              // group_desc（替换旧 gdt[0]）
ctx.inode_cache     // inode 单条缓存（替换旧 inode_area[0]）
ctx.block_bmp[512]  // 块位图（替换旧 bitbuf）
ctx.inode_bmp[512]  // inode 位图（替换旧 ibuf）
ctx.dir_cache[32]   // 目录项缓存（替换旧 dir）
ctx.data_buf[512]   // 数据块缓冲（替换旧 Buffer）
ctx.write_buf[4096] // 写入缓冲（替换旧 tempbuf）
ctx.last_alloc_inode / last_alloc_block
ctx.current_dir / current_dirlen
ctx.fopen_table[16]
ctx.current_path[256]
```

`disk_io.c` 所有 `*_read`/`*_write` 函数假定 `ctx.fp` 已打开，不再自己 `fopen`。无句柄泄漏。

---

## 命名映射（旧 init.c → 新模块）

| 旧名（已删除） | 新名 | 所在文件 |
|---------------|------|----------|
| `reload_super_block` / `update_super_block` | `sb_read` / `sb_write` | `disk_io.c` |
| `reload_group_desc` / `update_group_desc` | `gd_read` / `gd_write` | `disk_io.c` |
| `reload_block_bitmap` / `update_block_bitmap` | `block_bmp_read` / `block_bmp_write` | `disk_io.c` |
| `reload_inode_bitmap` / `update_inode_bitmap` | `inode_bmp_read` / `inode_bmp_write` | `disk_io.c` |
| `reload_inode_entry` / `update_inode_entry` | `inode_read` / `inode_write` | `disk_io.c` |
| `reload_dir` / `update_dir` | `dir_read` / `dir_write` | `disk_io.c` |
| `reload_block` / `update_block` | `data_read` / `data_write` | `disk_io.c` |
| `alloc_block` / `remove_block` | `balloc` / `bfree` | `bitmap.c` |
| `get_inode` / `remove_inode` | `ialloc` / `ifree` | `bitmap.c` |
| `reserch_file` | `dir_lookup` | `directory.c` |
| `dir_prepare` | `dir_entry_init` | `directory.c` |
| `search_file` | `file_is_open` | `directory.c` |
| `initialize_memory` + `format` | `fs_init` + `format` | `context.c` |
| `initialize_disk` | `fs_mkfs`（内部 static） | `context.c` |

---

## 公共 API（shell.c 调用）

声明于 `main.h`。旧 API 名保留，内部委托给新函数：

| shell 调用 | 实际执行 | 所在文件 |
|------------|----------|----------|
| `cd(name)` | 直接实现 | `directory.c` |
| `mkdir(name, type)` | → `file_create(name, type)` | `file_ops.c` |
| `cat(name)` | → `file_cat(name)` | `file_ops.c` |
| `rmdir(name)` | 直接实现（含递归删除） | `file_ops.c` |
| `del(name)` | → `file_delete(name)` | `file_ops.c` |
| `open_file(name)` | → `file_open(name)` | `file_ops.c` |
| `close_file(name)` | → `file_close(name)` | `file_ops.c` |
| `read_file(name)` | → `file_read(name)` | `file_ops.c` |
| `write_file(name)` | → `file_write(name)` | `file_ops.c` |
| `ls()` | → `dir_list()` | `directory.c` |
| `format()` / `check_disk()` | 直接实现 | `context.c` |
| `get_current_path()` | 直接实现 | `context.c` |

---

## 如何添加新功能

### 新命令

1. 在对应模块实现 `cmd_xxx(const char *arg)`。
   - 文件相关 → `file_ops.c`，目录相关 → `directory.c`，系统 → `context.c`
2. 在 `main.h` 添加声明。
3. 在 `shell.c` 的 if-else 链添加分支。
4. **在 `shell.c:help()` 中添加新命令的帮助信息。**
5. `README.md` 命令表加一行。
6. `make clean && make`，确认零警告。

### 新磁盘结构（如间接块、日志区）

1. `ext2_types.h` 修改结构体，`ext2_constants.h` 添加常量。
2. `disk_io.c` 添加对应 I/O 函数，`disk_io.h` 声明。
3. 上层模块（`bitmap.c` / `file_ops.c`）适配新的分配/寻址逻辑。
4. `context.c:fs_mkfs()` 初始化新区。

### 新子系统（如用户管理）

1. 新建 `include/xxx.h` + `src/xxx.c`。
2. 在 `context.c:fs_init()` 中初始化。
3. `Makefile` 的 `SRCS` 添加 `src/xxx.c`。

---

## 已修复（重构中解决，README 不再列出）

- 文件句柄泄漏：`fp` 统一生命周期管理
- 首次运行 crash：`Ext2` 不存在时 `fs_mkfs` 后正确重新 `fopen`
- 26 个编译警告：static 函数声明移除、死代码删除
- 无 Makefile、中文编码乱码、`sleep()` 空声明

## 仍存在的已知问题

详见 `README.md § 仍存在的问题`，共 6 条（scanf 溢出、fflush(stdin)、rmdir 递归覆盖路径、help 未实现、write 结束符 #）。提示符格式问题已随 login 系统一并修复。cat 名不副实问题已通过新实现的 `cat` 命令解决（`file_cat` 自动 open → read → close）。

## 开发计划

完整 Roadmap 见 `README.md § 后续开发计划`。已完成条目：

- ✅ Makefile、文件句柄管理、代码模块化、首次运行自动挂载
- ✅ 编译零警告、UTF-8 中文注释、static 声明清理、死代码清理
- ✅ **login 用户登录系统** — 启动时登录认证，提示符显示当前用户，`login` 切换用户，`whoami`/`useradd`/`passwd` 命令
- ✅ **用户信息持久化** — 磁盘末尾 10 块存储用户数据库（模拟 `/etc/passwd` + `/etc/shadow`），首运行自动创建 root 用户，`i_uid`/`i_gid` 关联
- ✅ **ls 列出物理地址** — `ls` 输出增加 `blocks` 列，显示文件数据块在磁盘上的绝对块号（数据区基址 516 + 相对索引）
- ✅ **时间戳更新** — 创建时 atime/ctime/mtime 设当前时间；读取更新 atime；写入更新 mtime/ctime；删除设置 dtime
- ✅ **完整权限系统** — 9 位 `rwxrwxrwx`，基于 uid/gid 三级判断（owner → group → other），root 绕过；`ls` 显示完整权限字符串
- ✅ **卷标读写** — `volname` 命令读取/修改文件系统卷标，同步更新超级块和组描述符
- ✅ **间接块寻址** — `get_file_block()` 实现 12 直接块 + 1 一级间接 + 1 二级间接 + 1 三级间接；`free_file_blocks()` 递归释放完整间接块子树
- ✅ **文件追加写模式** — `write >>` 追加写入，保留原有内容，以 `#` 结束输入
- ✅ **help 命令** — `help` 打印全部命令用法表

> **约定**：每完成一个 Roadmap 条目，在 `README.md` 对应行用 `~~删除线~~` 标记，不要删除。CLAUDE.md 只更新「已完成」小节。

## ⚠️ 常量命名约定（避免字节偏移 / 块计数混用 bug）

`ext2_constants.h` 中所有宏遵循严格命名约定：

| 后缀 | 语义 | 示例 | 绝对不能做的事 |
|------|------|------|---------------|
| `_SIZE` | 字节数 | `INODE_SIZE=64` | — |
| `_COUNTS` | 块数/条目数 | `DATA_BLOCK_COUNTS=4096` | **禁止**直接当偏移用：`BASE + COUNTS` ❌ |
| `_BLOCKS` | 块数 | `USER_AREA_BLOCKS=10` | **禁止**直接当偏移用：`BASE + BLOCKS` ❌ |
| 无后缀 | 磁盘字节偏移 | `DATA_BLOCK=264192` | — |

**转换必须乘 `BLOCK_SIZE`**：`byte_offset = BASE + count * BLOCK_SIZE`

> 曾有一个 bug 就是 `DATA_BLOCK + DATA_BLOCK_COUNTS - USER_AREA_BLOCKS` 把块数当字节加了，
> 导致用户区覆盖了 root 目录数据块。今后新增任何磁盘区域，务必遵守此约定。

