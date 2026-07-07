// SPDX-License-Identifier: GPL-2.0
/*
 * dir.c — 目录项操作：查找、添加、删除条目
 */

#include <linux/buffer_head.h>
#include <linux/string.h>
#include "ext2_sim_fs.h"

int ext2_sim_dir_find_entry(struct inode *dir, const char *name, int namelen,
                            struct ext2_sim_dir_entry_disk *result,
                            struct buffer_head **res_bh)
{
    /* TODO: Phase 5 — CLAUDE.md § 4.4.1 */
    return -ENOENT;
}

int ext2_sim_dir_add_entry(struct inode *dir, const char *name, int namelen,
                           uint16_t ino, uint8_t file_type)
{
    /* TODO: Phase 5 — CLAUDE.md § 4.4.2 */
    return -ENOSPC;
}

int ext2_sim_dir_remove_entry(struct inode *dir, const char *name, int namelen)
{
    /* TODO: Phase 5 — CLAUDE.md § 4.4.3 */
    return -ENOENT;
}
