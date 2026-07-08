# Schedule — ubuntu 内核模块开发计划

> **状态说明**：`⏳ 待完成` = 未开始 · `🔧 已编写` = 代码已写，测试未通过 · `✅ 已完成` = 测试通过

| 阶段 | 状态 | 文件 | 内容 | 交付产物 | 验证方式 |
|:---:|:---:|------|------|------|------|
| **1** | ✅ 已完成 | `Makefile`<br>`ext2_sim_disk.h`<br>`ext2_sim_fs.h`<br>+ 5个桩文件 | 项目骨架：Kbuild 编译系统、磁盘数据结构（`__le16`标注）、内存结构（`sbi`、`inode_info`）、所有宏定义、每模块桩函数 | 8 个文件编译通过 (`make` 零错误) | `make` → 生成 `ext2_sim.ko`；`insmod` → `dmesg` 显示 "module loaded" |
| **2** | ✅ 已完成 | `super.c`<br>`inode.c`<br>`file.c` | 模块入口/出口、`fill_super`（sb_set_blocksize→读超级块→校验→自动格式化→构建VFS sb→读根inode→`d_make_root`）、`put_super`（释放bh→kfree sbi）、`statfs`、`readdir`、`getattr`、`iget`、`get_block`（直接块）。alloc_inode/free_inode/evict_inode 使用内核默认实现。 | 可加载/挂载/卸载/重挂载、ls 可见 . 和 .. | `insmod` → `mount` → `ls` → `df` → `stat` → `umount` → 重挂载 → `dmesg` 无 oops |
| **3** | ✅ 已完成 | `balloc.c` | `balloc`（扫描块位图→置位→递减计数）、`bfree`（清零→递增计数）、`ialloc`（扫描inode位图→置位）、`ifree`（清零→递增计数） | 完整的位图分配/释放 | 通过步骤 4、5 间接验证 ✅ |
| **4** | ✅ 已完成 | `inode.c` | `iget`（✅）、`write_inode`（VFS inode→磁盘）、`lookup`（目录查找→`d_splice_alias`）、`create`（ialloc→初始化磁盘inode→add_entry→`d_instantiate`） | 可创建文件 | `touch /mnt/ext2/f1` → `ls` 可见 + `stat` 正常 ✅ |
| **5** | ✅ 已完成 | `dir.c` | `find_entry`（遍历目录块→按名匹配）、`add_entry`（找空槽→填入→mark_dirty）、`remove_entry`（定位→inode置零→mark_dirty） | 完整的目录操作 | `mkdir /mnt/ext2/d1` → `ls` 可见 + 重挂载数据持久化 ✅ |
| **6** | ✅ 已完成 | `file.c` | `file_read`（逻辑块→物理块→`sb_bread`→`copy_to_user`，空洞填零）、`file_write`（`copy_from_user`→`sb_bread`→扩大i_size，支持 O_APPEND）、`readdir`（✅）、`getattr`（✅）、`get_block` 分配模式（直接块自动分配） | 可读写文件 + 目录列表 | `echo hi > /mnt/ext2/f1 && cat /mnt/ext2/f1` → 输出 hi + 重挂载持久化 ✅ |
| **7** | ⏳ 待完成 | `inode.c` (补充) | `unlink`（目录移除条目→链接数--）、`rmdir`（检查空目录→移除条目→链接数--） | 可删除文件/目录 | `rm /mnt/ext2/f1` → 消失；`rmdir /mnt/ext2/d1` → 消失 |
| **8** | ⏳ 待完成 | `inode.c` (补充) | `evict_inode`（释放所有数据块：直接+一级间接+二级间接+三级间接→ifree） | 删除后空间回收 | `df /mnt/ext2` 删除前后空闲块数正确变化 |
| **9** | ⏳ 待完成 | `file.c` (补充) | `get_block`（逻辑块号→物理块号：直接块12个、一级间接、二级间接、三级间接；支持 `allocate` 自动扩展） | 支持大文件 | `dd if=/dev/urandom of=/mnt/ext2/big bs=512 count=200` → cat 校验 |
| **10** | ⏳ 待完成 | 全部 | 时间戳更新（创建设 atime/ctime/mtime；读更新 atime；写更新 mtime/ctime）、`mkdir` 补充（`.`/`..`初始化+`bg_used_dirs_count`）、完善 `statfs`、卸载再挂载数据持久化 | 功能完整 | 完整操作测试 + `dmesg` 无异常；卸载→挂载→数据仍在 |
| **11** | ⏳ 待完成 | 全部 | 压力验证：100个小文件创建/读取、长文件名边界、32条目满块目录自动扩展、空镜像首次挂载自动格式化、反复挂载卸载 10 次 | 稳定版本 | 全部通过，无 oops/panic/内存泄漏 |

---

## 阶段 1 开发记录与注意事项

### 已完成的工作

1. **`Makefile`**（Kbuild）— 定义 `obj-m += ext2_sim.o`，5 个 `.o` 文件链接
2. **`include/ext2_sim_disk.h`**（94 行）— 磁盘上 4 个结构体，使用 `__le16`/`__le32` 标注小端序
3. **`include/ext2_sim_fs.h`**（166 行）— 所有宏、内存结构 `sbi`/`inode_info`、`EXT2_SIM_SB`/`EXT2_SIM_I` 宏、跨模块函数声明
4. **5 个桩 `.c` 文件**（共 257 行）— 每个只包含函数签名 + `/* TODO: Phase N */` + 返回值，**未实现任何业务逻辑**

### 关键 API 决策（基于内核 v6.18.37 源码交叉验证）

| 事项 | 决策 |
|------|------|
| `create`/`mkdir`/`getattr` 签名 | 第一参数 `struct mnt_idmap *idmap` |
| `unlink`/`rmdir`/`lookup` 签名 | **无需** `idmap` 参数 |
| `write_inode` 返回值 | `int`（非 `void`） |
| `super_operations` | 使用 `.free_inode`（非 `->destroy_inode`），含 `.evict_inode` |
| `lookup` 返回值 | `d_splice_alias(inode, dentry)` |
| `create`/`mkdir` | 使用 `d_instantiate_new()` |
| `file_operations` | `->iterate_shared`（非 `->iterate`） |
| `inode_info` 布局 | `struct inode vfs_inode` 为第一字段（`container_of` 依赖） |
| `generic_fillattr` | 新签名 `generic_fillattr(idmap, request_mask, inode, stat)` |

### Linux 内核版本适配

- **你的 Ubuntu 内核：v7.0.0**，属于最新版，`mount_bdev` 已被彻底移除
- **已修复**：`super.c` 改用 `fs_context` API（`init_fs_context` → `get_tree_bdev` → `fill_super`）
- **已修复**：`fill_super` 签名从 `(sb, void *data, int silent)` 改为 `(sb, struct fs_context *fc)`
- **已确认兼容**：`iterate_shared`、`struct mnt_idmap *idmap` 在 v7.x 中保持不变
- 如果后续在其他内核版本上编译仍有问题，对照 CLAUDE.md §0 的 API 兼容表排查

### 编译验证步骤

```bash
cd ubuntu/
make
# 预期：无错误无警告，生成 ext2_sim.ko
# 常见报错见 README.md
```

### 模块加载测试

```bash
sudo insmod ext2_sim.ko
dmesg | tail -3
# 预期输出: ext2sim: module loaded
```

> ⚠️ **此时 `mount` 会失败**——`fill_super` 只是返回 `-EINVAL` 的桩函数。这是预期的，说明阶段 1 验证通过。`mount` 功能在阶段 2 实现。

---

### 🔍 阶段 1 Ubuntu 端验证流程（完整测试步骤）

以下流程在 Ubuntu 虚拟机上逐步执行。每一步都列出**预期输出**，如有偏差则定位问题。

#### 前置条件检查

```bash
# 1. 确认内核版本（需与已安装的 headers 匹配）
uname -r
# 预期输出示例: 6.8.0-45-generic

# 2. 确认内核开发工具链已安装
dpkg -l | grep linux-headers-$(uname -r)
# 预期: 能看到 ii 开头的行，表示已安装

# 3. 如果未安装，执行：
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
```

#### 步骤 1：获取代码

```bash
# 从 GitHub 克隆项目（替换为你的仓库地址）
git clone https://github.com/AoTarius/Linux_FileSystem.git
cd Linux_FileSystem/ubuntu/

# 确认所有文件存在
ls -la
# 预期看到: Makefile, include/, src/, README.md, CLAUDE.md, Schedule.md

ls include/
# 预期: ext2_sim_disk.h  ext2_sim_fs.h

ls src/
# 预期: balloc.c  dir.c  file.c  inode.c  super.c
```

#### 步骤 2：编译验证

```bash
make clean 2>/dev/null   # 清理上次编译残留（首次可忽略）
make
```

**预期输出**（零错误零警告）：
```
make -C /lib/modules/6.8.0-45-generic/build M=/home/user/Linux_FileSystem/ubuntu modules
make[1]: Entering directory '/usr/src/linux-headers-6.8.0-45-generic'
  CC [M]  /home/user/Linux_FileSystem/ubuntu/src/super.o
  CC [M]  /home/user/Linux_FileSystem/ubuntu/src/inode.o
  CC [M]  /home/user/Linux_FileSystem/ubuntu/src/file.o
  CC [M]  /home/user/Linux_FileSystem/ubuntu/src/balloc.o
  CC [M]  /home/user/Linux_FileSystem/ubuntu/src/dir.o
  LD [M]  /home/user/Linux_FileSystem/ubuntu/ext2_sim.o
  MODPOST /home/user/Linux_FileSystem/ubuntu/Module.symvers
  CC [M]  /home/user/Linux_FileSystem/ubuntu/ext2_sim.mod.o
  LD [M]  /home/user/Linux_FileSystem/ubuntu/ext2_sim.ko
  BTF [M] /home/user/Linux_FileSystem/ubuntu/ext2_sim.ko
make[1]: Leaving directory '/usr/src/linux-headers-6.8.0-45-generic'
```

> ⚠️ **注意**：BTF 行可能因内核配置不同而不出现，不影响编译结果。

**验证产物**：
```bash
ls -lh ext2_sim.ko
# 预期: -rw-r--r-- 1 user user 约 200K-400K  ext2_sim.ko

# 快速检查模块信息
modinfo ext2_sim.ko
# 预期输出中包含: filename: .../ext2_sim.ko  description: Simple EXT2 file system  license: GPL
```

#### 步骤 3：常见编译错误排查

| 错误信息 | 原因 | 解决方法 |
|----------|------|----------|
| `make: *** /lib/modules/.../build: No such file or directory` | 未安装 kernel headers | `sudo apt install linux-headers-$(uname -r)` |
| `implicit declaration of function 'mount_bdev'` | 内核版本较新，已废弃 `mount_bdev` | 需改用 `fs_context` API（参考 CLAUDE.md §0） |
| `unknown field 'iterate'` | 内核版本 ≥ 5.x，废弃 `.iterate` | 已使用 `.iterate_shared`，检查代码中是否存在遗漏 |
| `too many arguments to function 'generic_fillattr'` | 内核版本 < 5.12，无 `idmap` 参数 | 去掉 `idmap` 和 `request_mask` 参数 |
| `'struct mnt_idmap' undeclared` | 内核版本 < 5.12 | 去掉 `create`/`mkdir`/`getattr` 中的 `idmap` 参数 |

#### 步骤 4：加载模块

```bash
# 以 root 权限插入模块
sudo insmod ext2_sim.ko

# 检查插入是否成功（无输出即成功）
echo $?
# 预期: 0
```

#### 步骤 5：验证模块已加载

```bash
# 方法 1：查看内核日志
dmesg | tail -5
# 预期输出:
# [timestamp] ext2sim: module loaded

# 方法 2：检查 lsmod 列表
lsmod | grep ext2_sim
# 预期输出: ext2_sim  xxxxx  0  (注意 Used by 列是 0，因为还没挂载)
```

#### 步骤 6：确认 mount 会失败（预期行为）

```bash
# 先创建挂载点
sudo mkdir -p /mnt/ext2

# 尝试挂载（此时还没有磁盘镜像和块设备，所以用任意 loop 设备测试即可）
# 这一步会失败——这正是预期的！
sudo mount -t ext2sim /dev/loop0 /mnt/ext2 2>&1
# 预期输出: mount: /mnt/ext2: wrong fs type, bad option, bad superblock on /dev/loop0, ...

dmesg | tail -3
# 预期看到 fill_super 被调用但返回 -EINVAL 的日志
```

> ✅ 如果 `mount` 返回错误（而非内核 oops/panic），说明框架正确：VFS 正确调用了你的 `ext2_sim_fill_super()`，而 `fill_super` 按设计返回了 `-EINVAL`。

#### 步骤 7：卸载模块

```bash
sudo rmmod ext2_sim

dmesg | tail -3
# 预期输出: ext2sim: module unloaded

lsmod | grep ext2_sim
# 预期: 无输出（模块已卸载）
```

#### 步骤 8：完整清理

```bash
# 清理编译产物（可选，提交前执行）
make clean
# 预期: rm -f *.o *.ko *.mod.c *.mod *.order *.symvers ...

# 确认 git 状态干净（仅 Schedule.md 变更 + 新文件）
git status
```

#### 验证通过标准

| 检查项 | 通过条件 |
|--------|----------|
| 编译 | `make` 零错误零警告，生成 `ext2_sim.ko` |
| 加载 | `insmod` 返回 0，`lsmod` 可见，`dmesg` 显示 "module loaded" |
| mount 行为 | `mount` 失败（返回错误），内核无 oops/panic |
| 卸载 | `rmmod` 成功，`lsmod` 中消失，`dmesg` 显示 "module unloaded" |

全部通过后，将 Schedule.md 中阶段 1 的状态更新为 `✅ 已完成`，然后进入阶段 2。

---

### 给 Ubuntu 端 Claude 的上下文

当此项目在 Ubuntu 上被 Claude 打开时：

1. **先读 `CLAUDE.md`** — 完整的内核模块开发规格说明书（v7.0.0 API）
2. **再读本文件** — 了解当前进度、已完成事项、注意事项
3. **当前状态**：阶段 1 ✅、阶段 2 ✅（挂载/卸载/重挂载/readdir/getattr/iget 全部通过）
4. **下一步**：进入阶段 3（balloc.c 位图管理）或阶段 4（inode.c 文件/目录创建）
5. **阶段 2 关键修复记录见下方「阶段 2 开发记录与注意事项」**

---

### 阶段 2 开发记录与注意事项

#### 测试结果

全流程通过：`insmod` → `mount`（自动格式化）→ `ls`（`.` 和 `..`）→ `df`（2.0M）→ `stat`→ `umount`（无崩溃）→ 重挂载（跳过格式化）→ `ls` → `rmmod`。

#### 修复的 Bug

| # | Bug | 文件 | 症状 | 根因 | 修复 |
|---|---|---|---|---|---|
| 1 | mount 死循环 | super.c | `mount` 卡在 `__find_get_block_slow`，97% CPU | 直接赋值 `sb->s_blocksize=512` 未调用 `sb_set_blocksize()`，buffer cache 未重新配置 | 改用 `sb_set_blocksize(sb, 512)` |
| 2 | ls 空指针崩溃 | super.c | `ls` 触发 `__mark_inode_dirty` → `inode_io_list_move_locked` 空指针 | VFS 更新 atime 时标记 inode 为脏，但 inode 未接入 writeback 体系 | `sb->s_flags \|= SB_NOATIME` 禁用 atime |
| 3 | umount 内核 oops | super.c inode.c | `umount` 时 `list_lru_del` 空指针崩溃，remount 卡在 `super_lock` | 自定义 `evict_inode` → `clear_inode` → `list_lru_del` 在 `kmalloc` 分配的 inode 上找不到 LRU 节点 | 移除自定义 `alloc_inode`/`free_inode`/`evict_inode`，改用内核默认实现（`kmem_cache_alloc_lru`） |
| 4 | 格式化数据未持久化 | super.c | remount 可能读到脏数据 | `ext2_sim_format_disk` 写磁盘后未强制同步 | 格式化末尾调用 `sync_blockdev(sb->s_bdev)` |

#### 关键设计决策

| 事项 | 决策 |
|------|------|
| `alloc_inode` / `free_inode` | **不在阶段 2 实现**，使用内核默认的 `kmem_cache_alloc_lru`。阶段 8 需要 `ext2_sim_inode_info` 扩展字段时再加回来。 |
| `evict_inode` | **不在阶段 2 实现**，使用内核默认行为。阶段 8 实现数据块释放时再加回来。 |
| atime | 当前禁用（`SB_NOATIME`）。后续实现完整的 writeback 支持后移除。