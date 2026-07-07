// SPDX-License-Identifier: GPL-2.0
/*
 * balloc.c — 位图管理：块/inode 的分配与释放
 */

#include <linux/buffer_head.h>
#include "ext2_sim_fs.h"

/*
 * 位图操作说明：
 *   每个字节的 bit 7 对应第一个元素（块 0 / inode 1）。
 *   使用 mask = 128 >> (n % 8) 定位 single bit。
 *   1 = 已占用，0 = 空闲。
 */

uint16_t ext2_sim_balloc(struct super_block *sb)
{
    /* TODO: Phase 3 — CLAUDE.md § 4.3.1 */
    return 0;
}

void ext2_sim_bfree(struct super_block *sb, uint16_t block_rel)
{
    /* TODO: Phase 3 — CLAUDE.md § 4.3.2 */
}

uint16_t ext2_sim_ialloc(struct super_block *sb)
{
    /* TODO: Phase 3 — CLAUDE.md § 4.3.3 */
    return 0;
}

void ext2_sim_ifree(struct super_block *sb, uint16_t ino)
{
    /* TODO: Phase 3 — CLAUDE.md § 4.3.4 */
}
