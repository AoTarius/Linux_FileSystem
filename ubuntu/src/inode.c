// SPDX-License-Identifier: GPL-2.0
/*
 * inode.c — inode 操作：iget、write_inode、lookup、create、mkdir、
 *           unlink、rmdir、evict_inode
 */

#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include "ext2_sim_fs.h"

struct inode *ext2_sim_iget(struct super_block *sb, uint16_t ino)
{
    /* TODO: Phase 4 — CLAUDE.md § 4.5.1 */
    return NULL;
}

int ext2_sim_write_inode(struct inode *inode, struct writeback_control *wbc)
{
    /* TODO: Phase 4 — CLAUDE.md § 4.5.2 */
    return 0;
}

void ext2_sim_evict_inode(struct inode *inode)
{
    /* TODO: Phase 8 — CLAUDE.md § 4.5.0-c */
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

int ext2_sim_mkdir(struct mnt_idmap *idmap, struct inode *dir,
                   struct dentry *dentry, umode_t mode)
{
    /* TODO: Phase 4 — CLAUDE.md § 4.5.5 */
    return -ENOSYS;
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
