// SPDX-License-Identifier: GPL-2.0
/*
 * inode.c — inode 操作：iget、write_inode、lookup、create、mkdir、
 *           unlink、rmdir、evict_inode
 */

#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include "ext2_sim_fs.h"

/* ═════════════════════════════════════════════════════════════
 *  iget — 从磁盘读取 inode 或从缓存获取
 * ═════════════════════════════════════════════════════════════ */

struct inode *ext2_sim_iget(struct super_block *sb, uint16_t ino)
{
    struct ext2_sim_inode_disk *raw;
    struct inode *inode;
    struct buffer_head *bh;
    int block;

    if (ino < 1 || ino > EXT2_SIM_TOTAL_INODES)
        return ERR_PTR(-EINVAL);

    /* 从 VFS inode 缓存获取或创建 */
    inode = iget_locked(sb, ino);
    if (!inode)
        return ERR_PTR(-ENOMEM);

    /* 已在缓存中 → 直接返回（v7.x: i_state 为 struct，用 accessor */
    if (!(inode_state_read_once(inode) & I_NEW))
        return inode;

    /* ── 新分配的 inode：从磁盘填充 ───────────────────── */

    block = EXT2_SIM_INODE_BLOCK(ino);
    bh = sb_bread(sb, block);
    if (!bh) {
        iget_failed(inode);
        return ERR_PTR(-EIO);
    }

    raw = (struct ext2_sim_inode_disk *)(bh->b_data
          + EXT2_SIM_INODE_OFFSET(ino));

    /* 填充 VFS inode 字段 */
    inode->i_mode   = le16_to_cpu(raw->i_mode);
    i_uid_write(inode, le16_to_cpu(raw->i_uid));
    i_gid_write(inode, le16_to_cpu(raw->i_gid));
    inode->i_size   = le32_to_cpu(raw->i_size);
    inode->i_blocks = le16_to_cpu(raw->i_blocks);

    /* v7.x 时间戳：通过 setter 函数设置 */
    inode_set_atime(inode, (time64_t)le32_to_cpu(raw->i_atime), 0);
    inode_set_mtime(inode, (time64_t)le32_to_cpu(raw->i_mtime), 0);
    inode_set_ctime(inode, (time64_t)le32_to_cpu(raw->i_ctime), 0);

    inode->i_ino = ino;
    set_nlink(inode, le16_to_cpu(raw->i_links_count));

    /* 根据文件类型设置操作表 */
    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &ext2_sim_dir_inode_operations;
        inode->i_fop = &ext2_sim_dir_file_operations;
    } else {
        inode->i_op = &ext2_sim_file_inode_operations;
        inode->i_fop = &ext2_sim_file_file_operations;
    }

    brelse(bh);
    unlock_new_inode(inode);

    return inode;
}

int ext2_sim_write_inode(struct inode *inode, struct writeback_control *wbc)
{
    /* TODO: Phase 4 — CLAUDE.md § 4.5.2 */
    return 0;
}

/* ── VFS inode_operations 回调 ───────────────────────────── */

struct dentry *ext2_sim_lookup(struct inode *dir, struct dentry *dentry,
                               unsigned int flags)
{
    /* TODO: Phase 4 — CLAUDE.md § 4.5.3 */
    return NULL;
}

int ext2_sim_create(struct mnt_idmap *idmap, struct inode *dir,
                    struct dentry *dentry, umode_t mode, bool excl)
{
    /* TODO: Phase 4 — CLAUDE.md § 4.5.4 */
    return -ENOSYS;
}

struct dentry *ext2_sim_mkdir(struct mnt_idmap *idmap, struct inode *dir,
                               struct dentry *dentry, umode_t mode)
{
    /* TODO: Phase 4 — CLAUDE.md § 4.5.5 */
    return ERR_PTR(-ENOSYS);
}

int ext2_sim_unlink(struct inode *dir, struct dentry *dentry)
{
    /* TODO: Phase 7 — CLAUDE.md § 4.5.6 */
    return -ENOSYS;
}

int ext2_sim_rmdir(struct inode *dir, struct dentry *dentry)
{
    /* TODO: Phase 7 — CLAUDE.md § 4.5.7 */
    return -ENOSYS;
}

/* ═════════════════════════════════════════════════════════════
 *  inode_operations 注册
 * ═════════════════════════════════════════════════════════════ */

const struct inode_operations ext2_sim_file_inode_operations = {
    .getattr = ext2_sim_getattr,
};

const struct inode_operations ext2_sim_dir_inode_operations = {
    .lookup  = ext2_sim_lookup,
    .create  = ext2_sim_create,
    .unlink  = ext2_sim_unlink,
    .mkdir   = ext2_sim_mkdir,
    .rmdir   = ext2_sim_rmdir,
    .getattr = ext2_sim_getattr,
};
