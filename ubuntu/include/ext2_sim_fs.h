/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ext2_sim_fs.h — 内存结构、宏定义、跨模块函数声明
 *
 * 本头文件被所有 .c 文件包含。
 */

#ifndef _EXT2_SIM_FS_H
#define _EXT2_SIM_FS_H

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/statfs.h>
#include "ext2_sim_disk.h"

/* ═══════════════════════════════════════════════════════════
 *  磁盘几何常量
 * ═══════════════════════════════════════════════════════════ */

#define EXT2_SIM_BLOCK_SIZE              512
#define EXT2_SIM_DATA_BLOCK_START        516    /* 数据区起始绝对块号 */
#define EXT2_SIM_DATA_BLOCK_COUNTS       4096   /* 数据块总数 */
#define EXT2_SIM_INODES_PER_BLOCK        8      /* 每块 8 个 inode */
#define EXT2_SIM_DIR_ENTRIES_PER_BLOCK   32     /* 每块 32 个目录项 */
#define EXT2_SIM_NAME_LEN                8      /* 文件名最大长度 */
#define EXT2_SIM_TOTAL_BLOCKS            4612
#define EXT2_SIM_TOTAL_INODES            4096

/* ── 各区域绝对块号 ──────────────────────────────────────── */

#define EXT2_SIM_SB_BLOCK            0
#define EXT2_SIM_GDT_BLOCK           1
#define EXT2_SIM_BLOCK_BMP_BLOCK     2
#define EXT2_SIM_INODE_BMP_BLOCK     3
#define EXT2_SIM_INODE_TABLE_START   4
#define EXT2_SIM_INODE_TABLE_BLOCKS  512    /* (4096 inodes / 8 per block) */

/* ── 寻址常量 ────────────────────────────────────────────── */

#define EXT2_SIM_DIRECT_BLOCKS       12
#define EXT2_SIM_INDIRECT_PTRS       256    /* 512B / 2B = 256 pointers */

/* ── 一级间接: 12 + 256 = 268 blocks ─────────────────────── */
#define EXT2_SIM_IND_BLOCK           12     /* i_block[12] 索引 */
#define EXT2_SIM_IND_BOUNDARY        268

/* ── 二级间接: 268 + 256*256 = 65804 blocks ──────────────── */
#define EXT2_SIM_DIND_BLOCK          13     /* i_block[13] 索引 */
#define EXT2_SIM_DIND_BOUNDARY       65804

/* ── 三级间接: 65804 + 256*256*256 = 16843020 blocks ─────── */
#define EXT2_SIM_TIND_BLOCK          14     /* i_block[14] 索引 */

/* ── 默认权限 ────────────────────────────────────────────── */

#define EXT2_SIM_DEFAULT_DIR_MODE    0755
#define EXT2_SIM_DEFAULT_FILE_MODE  0644

/* ── inode 定位宏 ────────────────────────────────────────── */

/* 从 inode 号计算所在块号 */
#define EXT2_SIM_INODE_BLOCK(ino) \
    (EXT2_SIM_INODE_TABLE_START + ((ino) - 1) / EXT2_SIM_INODES_PER_BLOCK)

/* 从 inode 号计算块内偏移 */
#define EXT2_SIM_INODE_OFFSET(ino) \
    ((((ino) - 1) % EXT2_SIM_INODES_PER_BLOCK) * 64)

/* 从 inode 号计算所在块内索引 (0..7) */
#define EXT2_SIM_INODE_INDEX(ino) \
    (((ino) - 1) % EXT2_SIM_INODES_PER_BLOCK)

/* ═══════════════════════════════════════════════════════════
 *  内存结构 — 挂载时分配，挂到 sb->s_fs_info
 * ═══════════════════════════════════════════════════════════ */

struct ext2_sim_sb_info {
    struct buffer_head *s_sbh;           /* 超级块 bh (块 0)          */
    struct buffer_head *s_gdbh;          /* 组描述符 bh (块 1)        */
    struct buffer_head *s_bbh;           /* 块位图 bh (块 2)          */
    struct buffer_head *s_ibh;           /* inode 位图 bh (块 3)      */

    /* bh->b_data 直接指向磁盘数据，修改后需 mark_buffer_dirty()      */

    uint16_t s_last_alloc_block;         /* 上次分配的块号（加速查找） */
    uint16_t s_last_alloc_inode;         /* 上次分配的 inode 号       */
};

/* ── 从 super_block 获取 sbi ─────────────────────────────── */

static inline struct ext2_sim_sb_info *EXT2_SIM_SB(struct super_block *sb)
{
    return (struct ext2_sim_sb_info *)sb->s_fs_info;
}

/* ═══════════════════════════════════════════════════════════
 *  内存结构 — 每个 VFS inode 关联一个
 * ═══════════════════════════════════════════════════════════ */

struct ext2_sim_inode_info {
    struct inode vfs_inode;              /* ⚠️ 必须是第一个字段！
                                            container_of 依赖此布局 */
};

/* ── 从 struct inode * 获取 ext2_sim_inode_info * ────────── */

static inline struct ext2_sim_inode_info *EXT2_SIM_I(struct inode *inode)
{
    return container_of(inode, struct ext2_sim_inode_info, vfs_inode);
}

/* ═══════════════════════════════════════════════════════════
 *  跨模块函数声明
 * ═══════════════════════════════════════════════════════════ */

/* ── balloc.c ─────────────────────────────────────────────── */
uint16_t ext2_sim_balloc(struct super_block *sb);
void     ext2_sim_bfree(struct super_block *sb, uint16_t block_rel);
uint16_t ext2_sim_ialloc(struct super_block *sb);
void     ext2_sim_ifree(struct super_block *sb, uint16_t ino);

/* ── dir.c ────────────────────────────────────────────────── */
int ext2_sim_dir_find_entry(struct inode *dir, const char *name, int namelen,
                            struct ext2_sim_dir_entry_disk *result,
                            struct buffer_head **res_bh);
int ext2_sim_dir_add_entry(struct inode *dir, const char *name, int namelen,
                           uint16_t ino, uint8_t file_type);
int ext2_sim_dir_remove_entry(struct inode *dir, const char *name, int namelen);

/* ── inode.c ──────────────────────────────────────────────── */
struct inode *ext2_sim_iget(struct super_block *sb, uint16_t ino);
int           ext2_sim_write_inode(struct inode *inode,
                                   struct writeback_control *wbc);
void          ext2_sim_evict_inode(struct inode *inode);

/* VFS 回调（inode_operations / super_operations）              */
struct inode *ext2_sim_alloc_inode(struct super_block *sb);
void          ext2_sim_free_inode(struct inode *inode);
struct dentry *ext2_sim_lookup(struct inode *dir, struct dentry *dentry,
                               unsigned int flags);
int ext2_sim_create(struct mnt_idmap *idmap, struct inode *dir,
                    struct dentry *dentry, umode_t mode, bool excl);
int ext2_sim_mkdir(struct mnt_idmap *idmap, struct inode *dir,
                   struct dentry *dentry, umode_t mode);
int ext2_sim_unlink(struct inode *dir, struct dentry *dentry);
int ext2_sim_rmdir(struct inode *dir, struct dentry *dentry);

/* ── file.c ───────────────────────────────────────────────── */
uint16_t ext2_sim_get_block(struct inode *inode, uint16_t logical,
                            int allocate);
ssize_t ext2_sim_file_read(struct file *filp, char __user *buf,
                           size_t len, loff_t *ppos);
ssize_t ext2_sim_file_write(struct file *filp, const char __user *buf,
                            size_t len, loff_t *ppos);
int ext2_sim_readdir(struct file *filp, struct dir_context *ctx);
int ext2_sim_getattr(struct mnt_idmap *idmap, const struct path *path,
                     struct kstat *stat, u32 request_mask,
                     unsigned int query_flags);
int ext2_sim_fill_super(struct super_block *sb, void *data, int silent);
void ext2_sim_put_super(struct super_block *sb);
int ext2_sim_statfs(struct dentry *dentry, struct kstatfs *buf);

/* ── 文件系统类型注册（super.c）───────────────────────────── */
extern struct file_system_type ext2_sim_fs_type;

#endif /* _EXT2_SIM_FS_H */
