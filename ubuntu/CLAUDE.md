# CLAUDE.md — EXT2 内核模块开发规格说明书

> **角色**：你是 Linux 内核模块开发工程师。
> **目标**：在 `ubuntu/` 目录下实现一个 Linux 内核模块，注册一个 ext2 格式的文件系统，使得系统原生的 `ls`、`cat`、`touch` 等命令可以直接操作块设备上的 ext2 磁盘。

---

## 〇、内核版本与 API 兼容性

> **本规格书基于 Linux 内核 v6.18.37 的 VFS API 编写，所有函数签名已与内核源码交叉验证（见 `fs/ext2/super.c`、`fs/ext2/inode.c`、`fs/ext2/balloc.c`、`fs/ext2/namei.c`、`fs/ext2/dir.c`、`fs/ext2/file.c`、`include/linux/fs.h`）。**

### 0.1 关键 API 选择

由于我们在内核层直接用 `sb_bread` 操作 buffer_head，**不接入内核页缓存（address_space）**，因此做以下取舍：

| API | 选择 | 原因 |
|-----|------|------|
| `file_operations->read` / `->write` | **使用** | 简单；不接入 address_space 就不需要 `read_iter`/`write_iter` |
| `file_operations->iterate_shared` | **使用**（非 `->iterate`） | Linux 5.x+ 已移除 `->iterate`，统一用 `->iterate_shared` |
| `inode_operations->lookup` 返回值 | **使用 `d_splice_alias()`** | 处理目录别名，新内核标准做法（替代旧式 `d_add`） |
| `super_operations->write_inode` | 返回 `int`（非 `void`） | 新内核签名变更 |
| `super_operations->free_inode` | **使用**（非 `->destroy_inode`） | `destroy_inode` 已废弃，由 `free_inode` 替代 |
| `mount_bdev()` 包装 | **已移除**（v7.x） | 使用 `fs_context` + `get_tree_bdev()` 替代（见 §4.1） |

### 0.2 需要加入 `struct mnt_idmap *idmap` 参数的回调

> 以下 VFS 回调在新内核中多了第一个参数 `struct mnt_idmap *idmap`：

| 回调 | 完整签名 |
|------|---------|
| `create` | `int (*create)(struct mnt_idmap *, struct inode *, struct dentry *, umode_t, bool)` |
| `mkdir` | `int (*mkdir)(struct mnt_idmap *, struct inode *, struct dentry *, umode_t)` |
| `getattr` | `int (*getattr)(struct mnt_idmap *, const struct path *, struct kstat *, u32, unsigned int)` |

以下回调**不需要** `idmap` 参数（签名为旧式）：

| 回调 | 签名 |
|------|------|
| `unlink` | `int (*unlink)(struct inode *, struct dentry *)` |
| `rmdir` | `int (*rmdir)(struct inode *, struct dentry *)` |
| `lookup` | `struct dentry *(*lookup)(struct inode *, struct dentry *, unsigned int)` |

---

## 一、产品概述

### 1.1 我们要做什么

实现一个 Linux 内核模块（`.ko`），向内核 VFS 层注册文件系统类型 `ext2sim`。用户执行 `mount -t ext2sim /dev/loop0 /mnt/ext2` 后，`/mnt/ext2` 下的所有操作——`ls`、`cat`、`touch`、`mkdir`、`rm`、`echo "hello" > file`——都由该模块处理。

### 1.2 不做的事

- **不写 shell**：命令解析由 `/bin/ls`、`/bin/cat` 等系统程序完成，模块只提供 VFS 回调。
- **不写用户管理**：宿主 Linux 的 uid/gid 体系直接复用，模块只需在 inode 中正确读写 `i_uid`/`i_gid`，VFS 自动做权限检查。
- **不复用 Windows_macOS 代码**：两套代码完全独立，仅遵守相同的磁盘格式约定。

### 1.3 参考实现

`Windows_macOS/` 目录下有一个用户态 ext2 模拟器，**仅用作行为参考**——它的命令效果（`ls` 列什么、`mkdir` 产生什么磁盘结构）就是内核模块要达到的效果。**不参考其代码实现方式**（它是 `fread`/`fwrite`，我们是 `sb_bread`）。

---

## 二、磁盘格式规范（两份代码的共同约定）

这是内核模块和用户态模拟器之间**唯一的共同点**。

### 2.1 磁盘总览

| 参数 | 值 |
|---|---|
| 块大小 | 512 字节 |
| 总块数 | 4612 |
| 磁盘镜像大小 | 4612 × 512 = 2,361,344 字节 (~2.3 MB) |

### 2.2 块布局

```
块号      内容                    字节偏移
───────────────────────────────────────────
0         超级块 (super_block)    0
1         组描述符 (group_desc)    512
2         块位图 (block bitmap)    1024
3         inode 位图              1536
4～515    inode 表 (4096 条)      2048 ~ 264191
516～4611 数据块 (4096 个)       264192 ~ 2361343
```

### 2.3 超级块 (32 字节，大端/小端见 2.8)

```c
// 偏移 0，长度 32 字节
struct super_block_disk {
    char     s_volume_name[16];      // 卷标，如 "EXT2FS"
    uint16_t s_disk_size;           // 磁盘总块数 = 4612
    uint16_t s_blocks_per_group;    // 每组块数 = 4612（仅一个块组）
    uint16_t s_size_per_block;      // 每块字节数 = 512
    uint16_t s_free_blocks_count;   // 空闲数据块数
    uint16_t s_free_inodes_count;   // 空闲 inode 数
    char     s_pad[6];              // 填充
};
```

### 2.4 组描述符 (32 字节)

```c
struct group_desc_disk {
    char     bg_volume_name[16];      // 卷标副本
    uint16_t bg_block_bitmap;         // 块位图所在块号 = 2
    uint16_t bg_inode_bitmap;         // inode 位图所在块号 = 3
    uint16_t bg_inode_table;          // inode 表起始块号 = 4
    uint16_t bg_free_blocks_count;    // 本组空闲块数
    uint16_t bg_free_inodes_count;    // 本组空闲 inode 数
    uint16_t bg_used_dirs_count;      // 已分配目录数
    char     bg_pad[4];
};
```

### 2.5 inode (64 字节，inode 表共 4096 条)

```
inode N 位于磁盘位置：块 4 + (N-1)/8，块内偏移 ((N-1) % 8) × 64
（每块 512 字节，每条 inode 64 字节，每块存 8 条）
```

```c
struct inode_disk {
    uint16_t i_mode;          // 权限位 (0755=目录, 0644=普通文件)
    uint16_t i_blocks;        // 已分配数据块数量
    uint16_t i_uid;           // 所有者 uid
    uint16_t i_gid;           // 所有者 gid
    uint16_t i_links_count;   // 硬链接数
    uint16_t i_flags;
    uint32_t i_size;          // 文件字节大小
    uint32_t i_atime;         // 最后访问时间 (Unix timestamp)
    uint32_t i_ctime;         // 创建/元数据变更时间
    uint32_t i_mtime;         // 最后修改时间
    uint32_t i_dtime;         // 删除时间
    uint16_t i_block[15];     // 数据块指针数组
    char     i_pad[2];
};
```

**i_block 寻址规则**：
- `i_block[0]～[11]`：直接块指针（12 个），每个指向一个数据块
- `i_block[12]`：一级间接块指针。指向的块内存储 256 个 `uint16_t` 指针（512B ÷ 2B）
- `i_block[13]`：二级间接块指针。指向的块内存储 256 个一级间接块指针（256² 数据块）
- `i_block[14]`：三级间接块指针。指向的块内存储 256 个二级间接块指针（256³ 数据块）

数据块号是**相对于数据区起始块（块 516）的偏移**。即 `i_block[0] = 5` 表示绝对块号 `516 + 5 = 521`。

**最大文件大小**：3 级间接块全部展开可寻址 ~16TB；实际受磁盘 4096 数据块限制，约 2MB。

### 2.6 目录项 (16 字节)

```c
struct dir_entry_disk {
    uint16_t inode;       // 指向的 inode 号（0 表示空槽）
    uint16_t rec_len;     // 目录项总长度
    uint16_t name_len;    // 文件名长度
    uint8_t  file_type;   // 1=普通文件, 2=目录
    char     name[9];     // 文件名（最大 8 字符 + '\0'）
};
```

每个数据块存 32 条（512 ÷ 16）。目录的前两条固定为 `.`（指向自己）和 `..`（指向父目录）。空条目的 `inode` 字段为 0。

### 2.7 位图

- **块位图**（块 2）：512 字节 = 4096 位，每位对应一个数据块（bit 0 → 数据块 0 → 绝对块 516）。1 = 已占用。
- **inode 位图**（块 3）：512 字节 = 4096 位，每位对应一个 inode（bit 0 → inode 1）。1 = 已占用。
- 位图操作：字节内高位在前（bit 7 对应块 0/8/16...），用 `128 >> (n % 8)` 定位。

### 2.8 字节序

磁盘格式为**小端序（little-endian）**。在 x86 平台上可裸读。若需跨平台，使用 `le16_to_cpu()` / `cpu_to_le16()` / `le32_to_cpu()` / `cpu_to_le32()`。**建议加上，保证代码规范**。

---

## 三、模块架构

### 3.1 文件结构

```
ubuntu/
├── include/
│   ├── ext2_sim_disk.h       # 磁盘数据结构（__le16 标注）
│   └── ext2_sim_fs.h         # 内存结构（sbi, inode_info）、宏定义
├── src/
│   ├── super.c               # 挂载/卸载/统计
│   ├── inode.c               # inode 操作（查找、创建、删除）
│   ├── file.c                # 文件读写 & 目录遍历
│   ├── balloc.c              # 位图管理（块分配释放、inode 分配释放）
│   └── dir.c                 # 目录项辅助（查找条目、添加条目、删除条目）
├── Makefile                  # Kbuild
└── CLAUDE.md                 # 本文件
```

### 3.2 模块化准则

- `super.c` 只做挂载/卸载，不直接操作 inode 或目录。
- `inode.c` 只做 inode 的创建/查找/删除，具体磁盘读写调用 `balloc.c` 和 `dir.c`。
- `file.c` 只做文件数据的读写和目录遍历，不管理 inode 生命周期。
- `balloc.c` 只做位图的查/改/写，不关心上层语义。
- `dir.c` 只做目录项的增删查，不关心 inode 和权限。
- 所有模块通过 `ext2_sim_fs.h` 中声明的函数接口互相调用，**禁止跨模块直接访问对方的数据结构**。

### 3.3 内核 API 约束

| 禁止使用 | 必须使用 |
|---|---|
| `printf` | `printk(KERN_INFO/KERN_ERR ...)` |
| `malloc`/`free` | `kmalloc`/`kfree` (GFP_KERNEL) |
| `fopen`/`fread`/`fwrite`/`fseek` | `sb_bread()` → `struct buffer_head` |
| `memcpy` 栈大数组 | `memcpy` 到 `bh->b_data`，或小栈数组 |
| `sleep()` | `msleep()` 或 `schedule_timeout()` |
| `time(NULL)` | `ktime_get_real_seconds()` |
| 全局变量 | `struct ext2_sim_sb_info` 挂载到 `sb->s_fs_info` |
| 直接 `memcpy(..., user_ptr, ...)` | `copy_to_user()` / `copy_from_user()` |
| `d_add(dentry, inode)` 用于 lookup | `d_splice_alias(inode, dentry)`（返回 dentry 指针） |
| `super_operations->destroy_inode` | `super_operations->free_inode` |
| `file_operations->iterate` | `file_operations->iterate_shared` |
| 对 __user 指针使用 `strlen()` | `strnlen_user()` |

---

## 四、各模块详细规格

### 4.1 super.c — 挂载与卸载

#### 4.1.1 `ext2_sim_fill_super(struct super_block *sb, struct fs_context *fc)`

**职责**：内核执行 `mount -t ext2sim` 时调用。从块设备读取超级块，验证合法性，构建 VFS super_block。

> **v7.x 签名变更**：第二个参数从 `void *data, int silent` 改为 `struct fs_context *fc`。静默标志通过 `fc->sb_flags & SB_SILENT` 获取。

**处理流程**：
1. `sb_bread(sb, 0)` 读取超级块
2. 校验 `s_size_per_block == 512`，不匹配则返回错误
3. 分配并初始化 `struct ext2_sim_sb_info`，挂到 `sb->s_fs_info`
4. 读取组描述符（块 1）、块位图（块 2）、inode 位图（块 3），缓存到 sbi 中
5. 设置 `sb->s_blocksize = 512`，`sb->s_blocksize_bits = 9`
6. 设置 `sb->s_magic = 0xEF53`（或自定义魔数）
7. 设置 `sb->s_op = &ext2_sim_sops`
8. 读取根 inode（inode 1），通过 `iget_locked()` 获取 VFS inode
9. 填充根 inode 属性，`unlock_new_inode()`
10. 设置 `sb->s_root = d_make_root(root_inode)`

**校验失败处理**：若超级块无效且非 silent 模式，`printk(KERN_ERR "ext2sim: bad superblock")`，返回 `-EINVAL`。

#### 4.1.2 `ext2_sim_put_super(struct super_block *sb)`

**职责**：卸载时调用。释放 sbi 中缓存的所有 buffer_head，释放 sbi 本身。

**处理流程**：
1. `brelse(sbi->s_sbh)`, `brelse(sbi->s_gdbh)`, `brelse(sbi->s_bbh)`, `brelse(sbi->s_ibh)`
2. `kfree(sbi)`
3. `sb->s_fs_info = NULL`

#### 4.1.3 `ext2_sim_statfs(struct dentry *dentry, struct kstatfs *buf)`

**职责**：`df` 命令调用的数据来源。

**输出**：
- `buf->f_type = 0xEF53`
- `buf->f_bsize = 512`
- `buf->f_blocks = 4096`（数据块总数）
- `buf->f_bfree = sbi->s_gd->bg_free_blocks_count`
- `buf->f_files = 4096`（inode 总数）
- `buf->f_ffree = sbi->s_gd->bg_free_inodes_count`
- `buf->f_namelen = 8`

#### 4.1.4 注册数据结构

```c
static struct super_operations ext2_sim_sops = {
    .alloc_inode   = ext2_sim_alloc_inode,
    .free_inode    = ext2_sim_free_inode,       // 注意：不是 destroy_inode！
    .write_inode   = ext2_sim_write_inode,      // 注意：返回 int，非 void
    .evict_inode   = ext2_sim_evict_inode,      // 当 i_nlink==0 时清理数据块
    .put_super     = ext2_sim_put_super,
    .statfs        = ext2_sim_statfs,
};

/* ── fs_context 操作（替代 mount_bdev，内核 v5.4+ 必需）── */

static int ext2_sim_get_tree(struct fs_context *fc)
{
    return get_tree_bdev(fc, ext2_sim_fill_super);
}

static const struct fs_context_operations ext2_sim_context_ops = {
    .get_tree = ext2_sim_get_tree,
};

static int ext2_sim_init_fs_context(struct fs_context *fc)
{
    fc->ops = &ext2_sim_context_ops;
    return 0;
}

static struct file_system_type ext2_sim_fs_type = {
    .owner           = THIS_MODULE,
    .name            = "ext2sim",
    .init_fs_context = ext2_sim_init_fs_context,
    .kill_sb         = kill_block_super,
};
```

`init_fs_context` → `get_tree_bdev()` → 最终调用 `ext2_sim_fill_super`。

#### 4.1.5 模块入口

```c
module_init(ext2_sim_init);    // register_filesystem(&ext2_sim_fs_type)
module_exit(ext2_sim_exit);    // unregister_filesystem(&ext2_sim_fs_type)
MODULE_LICENSE("GPL");
MODULE_ALIAS_FS("ext2sim");
```

---

### 4.2 ext2_sim_fs.h — 内存结构

#### 4.2.1 `struct ext2_sim_sb_info`（每个挂载点一个实例）

```c
struct ext2_sim_sb_info {
    struct buffer_head *s_sbh;      // 超级块 bh
    struct buffer_head *s_gdbh;     // 组描述符 bh
    struct buffer_head *s_bbh;      // 块位图 bh
    struct buffer_head *s_ibh;      // inode 位图 bh

    // 上述 bh->b_data 直接指向磁盘数据，修改后需 mark_buffer_dirty()

    uint16_t s_last_alloc_block;    // 上次分配的块号（加速查找）
    uint16_t s_last_alloc_inode;    // 上次分配的 inode 号
};
```

**不使用 spinlock**（初版单线程即可，后续优化再加）。

#### 4.2.2 辅助宏

```c
// 从 super_block 获取 sbi（参考内核 EXT2_SB 宏）
static inline struct ext2_sim_sb_info *EXT2_SIM_SB(struct super_block *sb) {
    return (struct ext2_sim_sb_info *)sb->s_fs_info;
}
```

#### 4.2.3 `struct ext2_sim_inode_info`（每个 VFS inode 关联一个）

```c
struct ext2_sim_inode_info {
    struct inode vfs_inode;        // ⚠️ 必须放在第一个字段！
                                   //    这是内核标准模式（参考 EXT2_I 宏），
                                   //    通过 container_of 从 struct inode * 
                                   //    获取 ext2_sim_inode_info *
};

// 双向转换宏：
static inline struct ext2_sim_inode_info *EXT2_SIM_I(struct inode *inode) {
    return container_of(inode, struct ext2_sim_inode_info, vfs_inode);
}
```

#### 4.2.4 关键宏定义

```c
#define EXT2_SIM_BLOCK_SIZE        512
#define EXT2_SIM_DATA_BLOCK_START  516        // 数据区起始绝对块号
#define EXT2_SIM_INODES_PER_BLOCK  8          // 每块 8 个 inode
#define EXT2_SIM_DIR_ENTRIES_PER_BLOCK 32     // 每块 32 个目录项
#define EXT2_SIM_NAME_LEN          8          // 文件名最大长度

#define EXT2_SIM_SB_BLOCK          0
#define EXT2_SIM_GDT_BLOCK         1
#define EXT2_SIM_BLOCK_BMP_BLOCK   2
#define EXT2_SIM_INODE_BMP_BLOCK   3
#define EXT2_SIM_INODE_TABLE_START 4

#define EXT2_SIM_DIRECT_BLOCKS     12
#define EXT2_SIM_INDIRECT_PTRS     256       // 512B / 2B

#define EXT2_SIM_DEFAULT_DIR_MODE  0755
#define EXT2_SIM_DEFAULT_FILE_MODE 0644

// 从 inode 号计算所在块号和块内偏移
#define EXT2_SIM_INODE_BLOCK(ino)  (EXT2_SIM_INODE_TABLE_START + ((ino) - 1) / EXT2_SIM_INODES_PER_BLOCK)
#define EXT2_SIM_INODE_OFFSET(ino) ((((ino) - 1) % EXT2_SIM_INODES_PER_BLOCK) * 64)
```

---

### 4.3 balloc.c — 位图管理

#### 4.3.1 `uint16_t ext2_sim_balloc(struct super_block *sb)`

**职责**：从块位图中找一个空闲数据块，标记为已用，返回**相对块号**（相对于数据区起点块 516）。

**处理流程**：
1. 检查 `sbi->s_gd->bg_free_blocks_count`，为 0 则返回 0
2. 从 `sbi->s_last_alloc_block / 8` 字节开始扫描块位图（`sbi->s_bbh->b_data`）
3. 找到第一个非 0xFF 的字节，在其中找第一个 0 位
4. 将该位置 1：`bmp[byte] |= (128 >> bit)`
5. `mark_buffer_dirty(sbi->s_bbh)`
6. 递减 `sbi->s_gd->bg_free_blocks_count`、`mark_buffer_dirty(sbi->s_gdbh)`
7. 同步递减超级块中的 `s_free_blocks_count`、`mark_buffer_dirty(sbi->s_sbh)`
8. 更新 `sbi->s_last_alloc_block = 绝对块号`
9. 返回相对块号（绝对块号 - 516）

#### 4.3.2 `void ext2_sim_bfree(struct super_block *sb, uint16_t block_rel)`

**职责**：释放一个数据块（参数为**相对块号**）。

**处理流程**：
1. 绝对块号 = block_rel + 516（实际不需转换，位图中 bit 号就是相对块号）
2. `byte = block_rel / 8`, `bit = block_rel % 8`
3. `bmp[byte] &= ~(128 >> bit)`
4. 递增空闲计数，标记脏

#### 4.3.3 `uint16_t ext2_sim_ialloc(struct super_block *sb)`

**职责**：从 inode 位图分配一个空闲 inode，返回 inode 号（1～4096）。

**处理流程**：
1. 检查空闲计数
2. `cur = (sbi->s_last_alloc_inode - 1) / 8`，扫描 inode 位图
3. 找到空闲位，置 1
4. 更新计数，标记脏
5. 返回 inode 号

#### 4.3.4 `void ext2_sim_ifree(struct super_block *sb, uint16_t ino)`

**职责**：释放一个 inode。

**处理流程**：
1. `byte = (ino - 1) / 8`, `bit = (ino - 1) % 8`
2. `bmp[byte] &= ~(128 >> bit)`
3. 递增计数，标记脏

---

### 4.4 dir.c — 目录项操作

#### 4.4.1 `int ext2_sim_dir_find_entry(struct inode *dir, const char *name, int namelen, struct ext2_sim_dir_entry_disk *result, struct buffer_head **res_bh)`

**职责**：在目录 inode 中查找指定名称的条目。

**处理流程**：
1. 遍历目录的所有数据块（通过 `ext2_sim_get_block` 获取物理块号）
2. `sb_bread()` 读取每个数据块
3. 逐条比较 `dir_entry.name`，匹配则拷贝到 `result`，设置 `*res_bh`
4. 找到返回 0，未找到返回 `-ENOENT`
5. **调用者负责 `brelse(*res_bh)`**

#### 4.4.2 `int ext2_sim_dir_add_entry(struct inode *dir, const char *name, int namelen, uint16_t ino, uint8_t file_type)`

**职责**：在目录中新增一个条目。

**处理流程**：
1. 遍历目录的所有数据块，找第一个 `inode == 0` 的空槽
2. 若所有块都满（`i_size == i_blocks * 512`），分配新数据块
3. 填入 `dir_entry`：inode、name_len、file_type、strcpy(name)
4. `mark_buffer_dirty(res_bh)`
5. 目录 `i_size += 16`
6. 更新目录的 `i_mtime`、`i_ctime`
7. `mark_inode_dirty(dir)`
8. 返回 0，失败返回 `-ENOSPC`

#### 4.4.3 `int ext2_sim_dir_remove_entry(struct inode *dir, const char *name, int namelen)`

**职责**：从目录中删除指定条目。

**处理流程**：
1. 调用 `ext2_sim_dir_find_entry` 定位条目
2. 将 `inode` 字段置 0
3. `mark_buffer_dirty`
4. 目录 `i_size -= 16`
5. 若产生全空块，释放该数据块并收缩 `i_block[]`
6. 更新 `i_mtime`、`i_ctime`

---

### 4.5 inode.c — inode 操作

#### 4.5.0 `struct inode *ext2_sim_alloc_inode(struct super_block *sb)`

**职责**：VFS 需要分配一个新的 VFS inode 时调用。为我们的私有数据 `ext2_sim_inode_info` 分配内存。

**处理流程**：
1. `struct ext2_sim_inode_info *ei = kmalloc(sizeof(*ei), GFP_KERNEL)`
2. 返回 `&ei->vfs_inode`（将 `struct inode` 嵌入 `ext2_sim_inode_info` 中）

```c
// ext2_sim_fs.h 中定义：
struct ext2_sim_inode_info {
    struct inode vfs_inode;     // 必须放在第一个字段！
    // 预留扩展字段
};

// 注意：struct inode 必须嵌入在 ext2_sim_inode_info 的第一个字段，
// 这样才能用 container_of 相互转换。
```

#### 4.5.0-b `void ext2_sim_free_inode(struct inode *inode)`

**职责**：VFS 释放 inode 时调用。释放 `ext2_sim_inode_info` 的内存。

**处理流程**：
1. `struct ext2_sim_inode_info *ei = EXT2_SIM_I(inode)` — 提取私有数据
2. `kfree(ei)`

```c
// 辅助宏（放在 ext2_sim_fs.h）：
static inline struct ext2_sim_inode_info *EXT2_SIM_I(struct inode *inode) {
    return container_of(inode, struct ext2_sim_inode_info, vfs_inode);
}
```

#### 4.5.0-c `void ext2_sim_evict_inode(struct inode *inode)`

**职责**：当 inode 的引用计数归零且 `i_nlink == 0`（文件已被删除）时调用。**在这里释放该 inode 占用的所有数据块**，然后调用 `truncate_inode_pages_final()` 清理页缓存。

> **注意**：之前 CLAUDE.md 在 `ext2_sim_unlink()` 中做数据块释放是**错误的**。`unlink` 只从目录中移除条目，真正的数据块清理在 `evict_inode` 中进行。这保证了即使文件仍被其他进程打开（未 close），数据也不会丢失。

**处理流程**：
1. 若 `inode->i_nlink == 0 && !is_bad_inode(inode)`（文件已被删除且不再是坏 inode）：
   - 调用 `truncate_inode_pages_final(&inode->i_data)` — 清理页缓存
   - 释放该 inode 的所有数据块（**与 unlink 中描述的逻辑一致**）：
     - 遍历 i_block[0～11]：逐个 `ext2_sim_bfree()`
     - i_block[12] 间接块：读块 → 释放 256 个数据块 → 释放间接块自身
     - i_block[13] 二级间接块：读块 → 遍历 256 个一级间接块递归释放 → 释放二级间接块
     - i_block[14] 三级间接块：读块 → 遍历 256 个二级间接块递归释放 → 释放三级间接块
   - `ext2_sim_ifree(sb, inode->i_ino)` — 释放 inode 号
2. `clear_inode(inode)` — 最终清理

**关键认知**：数据块释放从 `unlink` 中**移除**，移入 `evict_inode`。`unlink` 仅负责：
- 从父目录删除条目
- `inode->i_links_count--`
- `mark_inode_dirty(inode)`

剩余清理由 VFS 在最后一个 `iput` 时自动调用 `evict_inode` 完成。

#### 4.5.1 `struct inode *ext2_sim_iget(struct super_block *sb, uint16_t ino)`

**职责**：根据 inode 号获取 VFS inode。若已在缓存则返回缓存，否则分配新 inode 并从磁盘读取。

**处理流程**：
1. `iget_locked(sb, ino)` — 内核自动查缓存 / 分配
2. 若是新分配的 inode（`(inode->i_state & I_NEW)`）：
   - 计算 inode 所在块和偏移：`block = EXT2_SIM_INODE_BLOCK(ino)`, `offset = EXT2_SIM_INODE_OFFSET(ino)`
   - `bh = sb_bread(sb, block)`，从 `bh->b_data + offset` 拷贝 64 字节
   - 填充 VFS inode：
     - `inode->i_mode = le16_to_cpu(raw->i_mode)`
     - `inode->i_uid = make_kuid(&init_user_ns, le16_to_cpu(raw->i_uid))`
     - `inode->i_gid = make_kgid(&init_user_ns, le16_to_cpu(raw->i_gid))`
     - `inode->i_size = le32_to_cpu(raw->i_size)`
     - `inode->i_atime = le32_to_cpu(raw->i_atime)` 等
     - `inode->i_blocks = le16_to_cpu(raw->i_blocks)`
     - 若 `S_ISDIR(inode->i_mode)`：`inode->i_op = &ext2_sim_dir_inops`，`inode->i_fop = &ext2_sim_dir_fops`
     - 若 `S_ISREG(inode->i_mode)`：`inode->i_op = &ext2_sim_file_inops`，`inode->i_fop = &ext2_sim_file_fops`
   - `brelse(bh)`
   - `unlock_new_inode(inode)`
3. 返回 inode

#### 4.5.2 `int ext2_sim_write_inode(struct inode *inode, struct writeback_control *wbc)`

**职责**：将 VFS inode 的当前状态写回磁盘。

**注意**：返回值类型为 `int`（新内核签名），非 `void`。成功返回 0，失败返回负错误码。

**处理流程**：
1. 获取磁盘 inode 所在块
2. `sb_bread()` 读取，修改对应偏移的 64 字节
3. 将 VFS 字段写回磁盘结构（`cpu_to_le16`/`cpu_to_le32`）
4. `mark_buffer_dirty(bh)`，`brelse(bh)`
5. 返回 0

#### 4.5.3 `ext2_sim_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)`

**职责**：VFS 在目录中查找一个名字时调用（任何路径解析都会触发）。

**注意**：`lookup` 签名**不需要** `struct mnt_idmap *idmap` 参数。

**处理流程**：
1. 调用 `ext2_sim_dir_find_entry(dir, dentry->d_name.name, dentry->d_name.len, &de, &bh)`
2. 若找到：
   - `inode = ext2_sim_iget(dir->i_sb, de.inode)`
   - `brelse(bh)`
3. 若未找到：`inode = NULL`
4. `return d_splice_alias(inode, dentry)` — **使用 `d_splice_alias()`，不是 `d_add()`**
   - 新内核中 `d_splice_alias` 统一处理了 NULL inode（相当于 `d_add(dentry, NULL)`）和非 NULL inode（含目录别名检测）

#### 4.5.4 `ext2_sim_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)`

**职责**：`touch /mnt/ext2/newfile` 或 `open(..., O_CREAT)` 时调用。

**注意**：新内核签名的第一个参数是 `struct mnt_idmap *idmap`，需传递给 `inode_init_owner()`。

**处理流程**：
1. `ino = ext2_sim_ialloc(dir->i_sb)` — 分配新 inode
2. 初始化磁盘 inode：
   - `i_mode = mode | S_IFREG`
   - `inode_init_owner(idmap, inode, dir, mode)` — **传入 `idmap` 参数**
   - `i_links_count = 1`
   - `i_size = 0`, `i_blocks = 0`
   - 清零 `i_block[15]`
   - 时间戳：`i_atime = i_ctime = i_mtime = ktime_get_real_seconds()`
3. `mark_buffer_dirty(inode_bh)`, `brelse(inode_bh)`
4. `ext2_sim_dir_add_entry(dir, name, namelen, ino, EXT2_SIM_FT_FILE)`
5. `inode = ext2_sim_iget(dir->i_sb, ino)`
6. `d_instantiate_new(dentry, inode)` — **新内核推荐 `d_instantiate_new`（自动处理 inode 引用计数）**
7. 返回 0

#### 4.5.5 `ext2_sim_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode)`

**职责**：`mkdir /mnt/ext2/newdir` 时调用。

**注意**：新内核签名的第一个参数是 `struct mnt_idmap *idmap`。

**处理流程**：
1. `ino = ext2_sim_ialloc(dir->i_sb)`
2. 分配一个数据块：`blk = ext2_sim_balloc(dir->i_sb)`
3. 初始化磁盘 inode：
   - `i_mode = mode | S_IFDIR`
   - 调用 `inode_init_owner(idmap, inode, dir, mode)` 设置 uid/gid
   - `i_size = 32`（`.` 和 `..` 两条目）
   - `i_blocks = 1`
   - `i_block[0] = blk`
4. 初始化目录块：
   - `bh = sb_bread(sb, 516 + blk)`
   - 条目 0：`.` → inode = ino, name_len = 0, file_type = 2
   - 条目 1：`..` → inode = dir->i_ino, name_len = 0, file_type = 2
   - 其余 30 条目的 inode 置 0
   - `mark_buffer_dirty(bh)`, `brelse(bh)`
5. 递增组描述符的 `bg_used_dirs_count`
6. `ext2_sim_dir_add_entry(dir, name, namelen, ino, EXT2_SIM_FT_DIR)`
7. `inode = ext2_sim_iget(dir->i_sb, ino)`
8. `d_instantiate_new(dentry, inode)` — 使用 `d_instantiate_new` 而非 `d_instantiate`

#### 4.5.6 `ext2_sim_unlink(struct inode *dir, struct dentry *dentry)`

**职责**：`rm /mnt/ext2/file` 时调用。

**注意**：`unlink` 签名**不需要** `struct mnt_idmap *idmap` 参数。

**处理流程**：
1. 获取目标 inode = `dentry->d_inode`
2. `ext2_sim_dir_remove_entry(dir, name, namelen)` — 从父目录移除条目
3. `inode->i_links_count--` — 递减链接数
4. `mark_inode_dirty(inode)` — 标记 inode 脏（链接数变更需写回）
5. 返回 0

**重要**：不在这里释放数据块！数据块的释放由 `ext2_sim_evict_inode()` 负责（当最后一个 `iput` 时 VFS 自动调用）。`unlink` 只做"从目录取消链接"这件事。这样即使文件被 unlink 后仍有进程持有 fd，数据也不会丢失。

#### 4.5.7 `ext2_sim_rmdir(struct inode *dir, struct dentry *dentry)`

**职责**：`rmdir /mnt/ext2/emptydir` 时调用。仅删除空目录（只有 `.` 和 `..` 两个条目）。

**注意**：`rmdir` 签名**不需要** `struct mnt_idmap *idmap` 参数。

**处理流程**：
1. 检查目录是否为空：只有 `.` 和 `..` 条目（即 `i_size == 32` 或目录块中仅 2 个有效条目），不为空则返回 `-ENOTEMPTY`
2. `ext2_sim_dir_remove_entry(dir, name, namelen)` — 从父目录移除条目
3. `inode->i_links_count--`（目录的 `..` 链接）
4. `mark_inode_dirty(inode)`
5. 递减组描述符的 `bg_used_dirs_count`

**重要**：与 `unlink` 同理，`rmdir` 不在此释放数据块。数据块和 inode 的释放由 `ext2_sim_evict_inode()` 负责。

#### 4.5.8 操作结构体

```c
static struct inode_operations ext2_sim_file_inops = {
    .getattr    = ext2_sim_getattr,
    // 普通文件只需要 getattr；lookup/create/unlink/mkdir/rmdir 属于目录操作
};

static struct inode_operations ext2_sim_dir_inops = {
    .lookup     = ext2_sim_lookup,     // lookup 不需要 idmap
    .create     = ext2_sim_create,     // 第一个参数 idmap
    .unlink     = ext2_sim_unlink,     // 不需要 idmap 参数
    .mkdir      = ext2_sim_mkdir,      // 第一个参数 idmap
    .rmdir      = ext2_sim_rmdir,      // 不需要 idmap 参数
    .getattr    = ext2_sim_getattr,    // 第一个参数 idmap
};
```

> **关键区别**：
> - `create` / `mkdir` / `getattr` → 第一个参数是 `struct mnt_idmap *idmap`
> - `unlink` / `rmdir` / `lookup` → **不需要** `idmap` 参数
> - `create`/`mkdir`/`unlink`/`rmdir` 挂到**目录**的 `i_op` 上（操作发生在父目录中）
> - 普通文件的 `i_op` 只需要 `getattr`

---

### 4.6 file.c — 文件读写 & 目录遍历

#### 4.6.1 辅助：`uint16_t ext2_sim_get_block(struct inode *inode, uint16_t logical, int allocate)`

**职责**：将文件的逻辑块号映射为物理块号（绝对块号，即 516 + 相对块号）。这是间接块寻址的入口。

**处理流程**：
1. 若 `logical < 12`（直接块）：
   - 若 `allocate` 且块不存在：`blk = ext2_sim_balloc()`，写入 `i_block[logical]`，写回磁盘 inode
   - 返回 `516 + i_block[logical]`
2. 若 `logical < 268`（一级间接块，`i_block[12]`）：
   - 若 `i_block[12] == 0`：分配一级间接块并清零
   - 读间接块，检查 `ptrs[logical - 12]`
   - 若 `allocate` 且为空：分配数据块，更新指针，写回
   - 返回 `516 + ptrs[logical - 12]`
3. 若 `logical < 65804`（二级间接块，`i_block[13]`）：
   - 若 `i_block[13] == 0`：分配二级间接块并清零
   - 按 `idx = logical-268` 计算 dbl_idx / sgl_idx
   - 确保目标一级间接块存在（读外层→分配内层→写回外层，注意用栈变量保存缓冲区）
   - 从一级间接块读数据块指针
   - 返回物理块号
4. 否则（三级间接块，`i_block[14]`）：
   - 同上模式，三层嵌套：三级→二级→一级→数据块
   - 每读更深一层前，将当前层的 `unsigned short[256]` 保存到栈上

**关键实现细节**：
- 磁盘 inode 的 `i_block[]` 不在 VFS inode 中，每次需要从磁盘读
- 读取磁盘 inode：`sb_bread(sb, EXT2_SIM_INODE_BLOCK(ino))` → 偏移 `EXT2_SIM_INODE_OFFSET(ino)` → 读 64 字节
- 修改后写回：修改同一位置 → `mark_buffer_dirty` → `brelse`

#### 4.6.2 `ext2_sim_file_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)`

**职责**：`cat /mnt/ext2/file` 或 `read()` 系统调用。

**处理流程**：
1. 若 `*ppos >= inode->i_size`：返回 0（EOF）
2. 若 `*ppos + len > inode->i_size`：截断 `len`
3. 计算起始逻辑块 = `*ppos / 512`，块内偏移 = `*ppos % 512`
4. 循环读取：
   - `phys = ext2_sim_get_block(inode, logical, 0)`（不分配）
   - `bh = sb_bread(sb, phys)`
   - 从 `bh->b_data + offset` 拷贝 min(剩余, 512-offset) 字节到 `buf`
   - `brelse(bh)`
   - 推进 `buf`、`len`、`*ppos`、`logical`
5. 更新 `inode->i_atime`，`mark_inode_dirty(inode)`
6. 返回实际读取字节数

**用户态数据拷贝**：必须使用 `copy_to_user(buf, kernel_buf, bytes)`，**严禁直接 `memcpy` 到 `__user` 指针**。

#### 4.6.3 `ext2_sim_file_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)`

**职责**：`echo "hello" > /mnt/ext2/file` 或 `write()` 系统调用。

**处理流程**：
1. 计算起始逻辑块和块内偏移
2. 循环写入：
   - `phys = ext2_sim_get_block(inode, logical, 1)`（自动分配）
   - `bh = sb_bread(sb, phys)`
   - `copy_from_user(bh->b_data + offset, buf, chunk)` ← **使用 `copy_from_user`**
   - `mark_buffer_dirty(bh)`, `brelse(bh)`
   - 推进指针
3. 若 `*ppos > inode->i_size`：更新 `inode->i_size`
4. 更新 `i_mtime`、`i_ctime`，`mark_inode_dirty(inode)`
5. 返回写入字节数

**需处理的情况**：
- 追加写：`*ppos` 可能 > `i_size`，中间空洞填 0
- 覆盖写：`*ppos` 在文件范围内
- 写超出当前分配块：自动通过 `ext2_sim_get_block(..., 1)` 扩展
- 文件大小限制：受磁盘容量（4096 数据块 ≈ 2MB）约束，超出返回 `-ENOSPC`

#### 4.6.4 `ext2_sim_readdir(struct file *filp, struct dir_context *ctx)`

**职责**：`ls /mnt/ext2` 或 `getdents()` 系统调用。内核通过 `dir_context` 逐个接收目录项。

**处理流程**：
1. 若 `ctx->pos == 0`：`ctx->pos = 0`（起始位置）
2. 计算当前条目索引 = `ctx->pos / 16`
3. 跳过已输出的条目，从数据块中逐条读取：
   - `phys = ext2_sim_get_block(inode, block_idx, 0)`
   - `bh = sb_bread(sb, phys)`
   - 遍历块内 32 条目录项
   - 跳过 `inode == 0` 的空条目
   - 对有效条目调用 `dir_emit(ctx, name, name_len, inode, DT_DIR/DT_REG)`
     - `file_type == 2` → `DT_DIR`
     - `file_type == 1` → `DT_REG`
   - `ctx->pos += 16`
4. `brelse(bh)`
5. 返回 0（目录遍历完成）

#### 4.6.5 `ext2_sim_getattr(struct mnt_idmap *idmap, const struct path *path, struct kstat *stat, u32 request_mask, unsigned int query_flags)`

**职责**：`stat /mnt/ext2/file` 时调用，填充 `struct kstat`。

**注意**：新内核签名完全不同于旧版（旧版：`vfsmount *mnt, dentry *dentry`）。第一个参数是 `struct mnt_idmap *idmap`，用 `const struct path *path` 传入路径。

**处理流程**：
1. `struct inode *inode = d_inode(path->dentry)` — 从 path 提取 inode
2. `generic_fillattr(idmap, request_mask, inode, stat)` — **传入 idmap 和 request_mask**（非旧版 `generic_fillattr(inode, stat)`）
3. `stat->blocks = inode->i_blocks` — 额外填充块数
4. 返回 0

#### 4.6.6 操作结构体

```c
static struct file_operations ext2_sim_file_fops = {
    .read       = ext2_sim_file_read,
    .write      = ext2_sim_file_write,
    .llseek     = generic_file_llseek,
};

static struct file_operations ext2_sim_dir_fops = {
    .iterate_shared = ext2_sim_readdir,   // 注意：必须用 iterate_shared，不是 iterate
    .llseek         = generic_file_llseek,
};
```

> **`iterate_shared` vs `iterate`**：Linux 5.x+ 已将 `file_operations->iterate` 替换为 `->iterate_shared`。旧内核的 `->iterate` 成员已不存在于结构体定义中。使用 `->iterate` 将导致**编译错误**。
>
> **`read` vs `read_iter`**：使用传统的 `.read`/`.write`（`char __user *buf` 风格）而非新的 `read_iter`/`write_iter`（`struct kiocb *` + `struct iov_iter *`），因为我们不接入内核的 address_space 页缓存，传统 API 更简单且仍完全支持。

---

## 五、构建系统 (Makefile / Kbuild)

### 5.1 Kbuild Makefile

```makefile
obj-m += ext2_sim.o
ext2_sim-objs := src/super.o src/inode.o src/file.o src/balloc.o src/dir.o

ccflags-y := -I$(src)/include -Wall

all:
    make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
    make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

### 5.2 编译命令

```bash
cd ubuntu/
make          # 编译，生成 ext2_sim.ko
make clean    # 清理
```

---

## 六、验证方案

### 6.1 准备磁盘镜像

```bash
# 方式 1：空镜像（模块首次挂载时应自动格式化，或手动用 mkfs 工具）
dd if=/dev/zero of=./test.img bs=512 count=4612

# 方式 2：用 Windows_macOS 版本的 ./Ext2 作为测试数据
cp ../Windows_macOS/Ext2 ./test.img
```

### 6.2 加载测试

```bash
# 1. 设置循环设备
sudo losetup /dev/loop0 ./test.img

# 2. 加载模块
sudo insmod ext2_sim.ko

# 3. 挂载
sudo mount -t ext2sim /dev/loop0 /mnt/ext2

# 4. 基本操作测试
ls /mnt/ext2                     # 应列出根目录内容
echo "hello world" | sudo tee /mnt/ext2/test.txt
cat /mnt/ext2/test.txt           # 应输出 hello world
sudo mkdir /mnt/ext2/subdir
ls /mnt/ext2                     # 应看到 subdir
sudo rm /mnt/ext2/test.txt
ls /mnt/ext2                     # test.txt 应消失
sudo rmdir /mnt/ext2/subdir

# 5. 卸载
sudo umount /mnt/ext2
sudo rmmod ext2_sim
sudo losetup -d /dev/loop0

# 6. 查看日志
dmesg | tail -50
```

### 6.3 验收标准

| 操作 | 预期结果 | 对应回调 | 测试命令 |
|---|---|---|---|
| 挂载 | 无报错，`/mnt/ext2` 可用 | `fill_super` | `mount` |
| `ls` | 列出 `.` 和 `..`（有文件则列出全部） | `readdir` + `lookup` | `ls /mnt/ext2` |
| `touch` | 创建 0 字节文件 | `create` + `ialloc` | `touch /mnt/ext2/f1` |
| `echo >` | 写入内容，`cat` 可见 | `write` + `read` | `echo hi > /mnt/ext2/f1 && cat /mnt/ext2/f1` |
| `mkdir` | 创建目录，含 `.` 和 `..` | `mkdir` + `balloc` | `mkdir /mnt/ext2/d1` |
| `rm` | 删除文件，inode 和数据块释放 | `unlink` + `ifree` + `bfree` | `rm /mnt/ext2/f1` |
| `rmdir` | 删除空目录 | `rmdir` | `rmdir /mnt/ext2/d1` |
| `df` | 显示正确空间统计 | `statfs` | `df /mnt/ext2` |
| 卸载 | 无报错，可重新挂载 | `put_super` | `umount` |

### 6.4 压力验证（后续）

- 创建 100 个小文件 → 全部可 `cat`
- 写 100KB 大文件 → 内容完整（测试间接块）
- 32 条目一满块的目录 → 正确自动扩展新块
- 反复挂载/卸载 10 次 → 数据不丢失

---

## 七、常见内核编程陷阱（务必注意）

### 7.1 buffer_head 生命周期

```c
struct buffer_head *bh = sb_bread(sb, block);
// ... 使用 bh->b_data ...
mark_buffer_dirty(bh);  // 修改后必须调用
brelse(bh);             // 必须配对释放，否则内存泄漏
```

**每个 `sb_bread` 必须有一个对应的 `brelse`**。提前 return 时也要释放。

### 7.2 用户态数据拷贝

```c
// 错误！内核不能直接访问用户态指针
memcpy(kernel_buf, user_buf, len);  // ❌ 可能 oops

// 正确
if (copy_from_user(kernel_buf, user_buf, len))  // ✅
    return -EFAULT;
if (copy_to_user(user_buf, kernel_buf, len))    // ✅
    return -EFAULT;
```

### 7.3 inode 锁

新内核（4.x+）使用 `inode_lock(inode)` / `inode_unlock(inode)`：

```c
inode_lock(inode);       // 替代旧式 mutex_lock(&inode->i_mutex)
// ... 操作 ...
inode_unlock(inode);
```

### 7.4 GFP_KERNEL vs GFP_ATOMIC

- 进程上下文（正常 VFS 回调）：用 `GFP_KERNEL`
- 中断上下文 / 持有自旋锁：用 `GFP_ATOMIC`
- 我们所有回调都在进程上下文，统一用 `GFP_KERNEL`

### 7.5 不要在 printk 中用 %d 打印指针

```c
printk(KERN_INFO "bh=%p, data=%p\n", bh, bh->b_data);  // ✅ 用 %p
```

### 7.6 `d_instantiate_new` vs `d_instantiate`

```c
// create/mkdir 中为新分配的 inode 设置 dentry：
d_instantiate_new(dentry, inode);   // ✅ 推荐（自动 ihold + 附加）

// 旧式写法（不推荐新代码使用）：
d_instantiate(dentry, inode);       // ⚠️ 需要自己确保 inode 引用正确

// lookup 中使用：
return d_splice_alias(inode, dentry);  // ✅ 自动处理 NULL inode 和目录别名
```

### 7.7 磁盘 inode 的读取与写回

每次需要读写磁盘 inode 时，必须通过 `EXT2_SIM_INODE_BLOCK()` / `EXT2_SIM_INODE_OFFSET()` 计算位置，用 `sb_bread` 读取块，从 `bh->b_data + offset` 拷出 64 字节。修改后 `mark_buffer_dirty` + `brelse`。

**严禁在 VFS inode 上追加自定义字段**——磁盘 inode 的数据只能通过上述方式访问。VFS inode 的 `i_ino` / `i_mode` / `i_size` 等字段有缓存意义，但 `i_block[]` 不存在于 VFS inode 中。

### 7.8 `struct inode` 必须嵌入在自定义结构体第一字段

```c
struct ext2_sim_inode_info {
    struct inode vfs_inode;   // ⚠️ 必须是第一个字段！
};
```

这是内核约定（参考 `fs/ext2/ext2.h` 中的 `EXT2_I` 宏模式），依赖 `container_of` 从 `struct inode *` 反查 `ext2_sim_inode_info *`。

---

## 八、实现顺序（建议按此步骤开发）

| 步骤 | 内容 | 可测试 |
|---|---|---|
| 1 | 搭建项目骨架：`Makefile`、`ext2_sim_disk.h`、`ext2_sim_fs.h` | `make` 编译通过 |
| 2 | `super.c`：`fill_super` + `put_super` + `alloc_inode` + `free_inode` + 模块注册 | `insmod` → `mount` → `ls /mnt/ext2`（看到空目录或 . 和 ..） |
| 3 | `balloc.c`：balloc/bfree/ialloc/ifree | 在步骤 4、5 中验证 |
| 4 | `inode.c`：`iget` + `lookup` + `create` + `mkdir` | `ls` 能看到 `.` 和 `..`，`touch` 能创建文件 |
| 5 | `dir.c`：find_entry/add_entry/remove_entry | `mkdir` → `ls` 可见，`rmdir` 可删除 |
| 6 | `file.c`：`read` + `write` + `readdir` + `getattr` | `echo > file` → `cat file`，完整 `ls -la` |
| 7 | `inode.c`：`unlink` + `rmdir` + `evict_inode`(数据块释放) | `rm` / `rmdir` |
| 8 | 间接块寻址：`get_block(..., logical >= 12, ...)` | 写 100KB+ 大文件验证 |
| 9 | 时间戳更新 | `stat` 看到正确时间 |
| 10 | `statfs`（df） | `df /mnt/ext2` 看到正确统计 |

---

## 九、与 VFS 的调用关系速查

```
用户操作                      VFS 调用                     我们的回调                          idmap?
──────────────────────────────────────────────────────────────────────────────────────────────────
mount /dev/loop0 /mnt/ext2    get_tree_bdev()        → ext2_sim_fill_super()               —
umount /mnt/ext2              kill_block_super()     → ext2_sim_put_super()                —
ls /mnt/ext2                  iterate_dir()          → ext2_sim_readdir()                  —
cat /mnt/ext2/f               vfs_read()             → ext2_sim_file_read()                —
echo hi > /mnt/ext2/f         vfs_write()            → ext2_sim_file_write()               —
touch /mnt/ext2/f             vfs_create()           → ext2_sim_create()                   ✅
mkdir /mnt/ext2/d             vfs_mkdir()            → ext2_sim_mkdir()                    ✅
rm /mnt/ext2/f                vfs_unlink()           → ext2_sim_unlink()                   ❌
rmdir /mnt/ext2/d             vfs_rmdir()            → ext2_sim_rmdir()                    ❌
stat /mnt/ext2/f              vfs_getattr()          → ext2_sim_getattr()                  ✅
df /mnt/ext2                  statfs()               → ext2_sim_statfs()                   —
cd /mnt/ext2/d                path_lookup → lookup() → ext2_sim_lookup()                   ❌
最后一个 iput (nlink==0)       —                      → ext2_sim_evict_inode()              —
```

> ✅ = 第一个参数是 `struct mnt_idmap *idmap`
> ❌ = 不需要 `idmap` 参数
> — = 不适用

---

## 十、关键决策记录

| 决策 | 结论 | 原因 |
|---|---|---|
| 是否复用 Windows_macOS 代码 | **不** | 接口完全不同（VFS vs 自建 shell），强行复用代价大于重写 |
| 磁盘格式是否与 Windows_macOS 兼容 | **是** | 同一份 `.img` 可被两边加载，方便交叉验证 |
| 是否支持多块组 | **初版不支持** | 当前磁盘仅 2MB，单块组足够 |
| 是否实现页缓存（address_space） | **初版不用** | 用 `sb_bread` 的 buffer_head，后续可迁移到 address_space。因此 `.read`/`.write` 使用传统 API 而非 `read_iter`/`write_iter` |
| 是否支持写时复制/日志 | **不** | 超出范围 |
| 间接块最高到几级 | **三级** | 与 Requirements.md 一致：12 直接 + 1 一级 + 1 二级 + 1 三级。Windows_macOS 版本已完整实现，ubuntu 版本同步实现 |
| 是否实现 rename | **初版不实现** | 可后续补充 |
| 是否实现 symlink/hardlink | **初版不实现** | 可后续补充 |
| 数据块释放的时机 | **在 `evict_inode` 中释放** | 遵循内核 ext2 的做法：`unlink`/`rmdir` 只取消链接，真正的空间回收在最后一个 `iput` → `evict_inode` 中完成。这保证 unlink 后 fd 仍持有的进程不受影响 |
| `lookup` 用 `d_splice_alias` 还是 `d_add` | **`d_splice_alias`** | 内核 v6.18.37 ext2 使用 `d_splice_alias`。它自动处理 NULL inode（相当于 d_add NULL）和已存在 inode 的目录别名检测 |
| `create`/`mkdir` 用 `d_instantiate_new` 还是 `d_instantiate` | **`d_instantiate_new`** | 新内核推荐，自动管理 inode 引用计数 |
| `file_operations` 用 `iterate` 还是 `iterate_shared` | **`iterate_shared`** | Linux 5.x+ `->iterate` 成员已从结构体移除，只能用 `->iterate_shared` |

---

> **你（Claude on Linux）现在已拥有实现此模块所需的全部规格信息。按第八章的顺序逐步实现，每完成一步编译验证。遇到内核 API 不确定时，查阅 `/lib/modules/$(uname -r)/build/include/linux/` 下的头文件。**
