// SPDX-License-Identifier: GPL-2.0
/*
 * file.c — 文件读写、目录遍历、get_block 间接块寻址
 */

#include <linux/buffer_head.h>
#include <linux/uaccess.h>
#include "ext2_sim_fs.h"

uint16_t ext2_sim_get_block(struct inode *inode, uint16_t logical,
                            int allocate)
{
    /* TODO: Phase 9 — CLAUDE.md § 4.6.1 */
    return 0;
}

ssize_t ext2_sim_file_read(struct file *filp, char __user *buf,
                           size_t len, loff_t *ppos)
{
    /* TODO: Phase 6 — CLAUDE.md § 4.6.2 */
    return -ENOSYS;
}

ssize_t ext2_sim_file_write(struct file *filp, const char __user *buf,
                            size_t len, loff_t *ppos)
{
    /* TODO: Phase 6 — CLAUDE.md § 4.6.3 */
    return -ENOSYS;
}

int ext2_sim_readdir(struct file *filp, struct dir_context *ctx)
{
    /* TODO: Phase 6 — CLAUDE.md § 4.6.4 */
    return 0;
}

int ext2_sim_getattr(struct mnt_idmap *idmap, const struct path *path,
                     struct kstat *stat, u32 request_mask,
                     unsigned int query_flags)
{
    /* TODO: Phase 6 — CLAUDE.md § 4.6.5 */
    return -ENOSYS;
}
