// SPDX-License-Identifier: GPL-2.0
/*
 * balloc.c — 位图管理：块/inode 的分配与释放
 *
 * 位图操作说明：
 *   每个字节的 bit 7 对应第一个元素（块 0 / inode 1）。
 *   使用 mask = 128 >> (n % 8) 定位 single bit。
 *   1 = 已占用，0 = 空闲。
 *
 *   块位图 (block 2): 512 bytes = 4096 bits，bit N → 数据块 N (绝对块 516+N)
 *   inode 位图 (block 3): 512 bytes = 4096 bits，bit N → inode N+1
 */

#include <linux/buffer_head.h>
#include "ext2_sim_fs.h"

#define BMP_BYTES  (EXT2_SIM_DATA_BLOCK_COUNTS / 8)  /* 512 */

/* ── 块位图内部 helper ──────────────────────────────────────────
 * 在块位图中扫描一个空闲位，从 start_byte 开始循环查找。
 * 返回相对块号，失败返回 0xFFFF。
 */
static uint16_t bitmap_scan_free(unsigned char *bmp, int start_byte,
                                  int total_bytes)
{
    int i, byte, bit;

    for (i = 0; i < total_bytes; i++) {
        byte = (start_byte + i) % total_bytes;
        if (bmp[byte] == 0xFF)
            continue;  /* 该字节全满，跳过 */

        /* 从高位 (bit 7) 向低位 (bit 0) 扫描 */
        for (bit = 0; bit < 8; bit++) {
            if (!(bmp[byte] & (128 >> bit)))
                return (uint16_t)(byte * 8 + bit);
        }
    }
    return 0xFFFF;  /* 未找到空闲位 */
}

/* ═══════════════════════════════════════════════════════════════
 *  ext2_sim_balloc — 分配一个空闲数据块
 *  返回相对块号（相对于数据区起点块 516），0 = 失败
 *  CLAUDE.md § 4.3.1
 * ═══════════════════════════════════════════════════════════════ */

uint16_t ext2_sim_balloc(struct super_block *sb)
{
    struct ext2_sim_sb_info *sbi = EXT2_SIM_SB(sb);
    struct ext2_sim_group_desc_disk *gd;
    struct ext2_sim_super_block_disk *sb_disk;
    unsigned char *bmp;
    uint16_t free_count;
    uint16_t block_rel;
    int start_byte;
    int byte, bit;

    gd = (struct ext2_sim_group_desc_disk *)sbi->s_gdbh->b_data;

    /* 1. 检查空闲计数 */
    free_count = le16_to_cpu(gd->bg_free_blocks_count);
    if (free_count == 0) {
        printk(KERN_ERR "ext2sim: balloc: no free blocks\n");
        return 0;
    }

    /* 2. 从上次分配位置开始扫描块位图 */
    bmp = (unsigned char *)sbi->s_bbh->b_data;
    start_byte = (sbi->s_last_alloc_block - EXT2_SIM_DATA_BLOCK_START) / 8;

    block_rel = bitmap_scan_free(bmp, start_byte, BMP_BYTES);
    if (block_rel == 0xFFFF) {
        printk(KERN_ERR "ext2sim: balloc: bitmap inconsistent (count=%u but no free bit)\n",
               free_count);
        return 0;
    }

    /* 3. 将该位置 1 */
    byte = block_rel / 8;
    bit  = block_rel % 8;
    bmp[byte] |= (unsigned char)(128 >> bit);
    mark_buffer_dirty(sbi->s_bbh);

    /* 4. 递减组描述符空闲计数 */
    gd->bg_free_blocks_count = cpu_to_le16(free_count - 1);
    mark_buffer_dirty(sbi->s_gdbh);

    /* 5. 同步递减超级块空闲计数 */
    sb_disk = (struct ext2_sim_super_block_disk *)sbi->s_sbh->b_data;
    sb_disk->s_free_blocks_count = cpu_to_le16(
        le16_to_cpu(sb_disk->s_free_blocks_count) - 1);
    mark_buffer_dirty(sbi->s_sbh);

    /* 6. 更新分配游标（存绝对块号） */
    sbi->s_last_alloc_block = EXT2_SIM_DATA_BLOCK_START + block_rel;

    printk(KERN_INFO "ext2sim: balloc: allocated block %u (abs %u), free=%u\n",
           block_rel, sbi->s_last_alloc_block, free_count - 1);

    return block_rel;
}

/* ═══════════════════════════════════════════════════════════════
 *  ext2_sim_bfree — 释放一个数据块
 *  参数 block_rel 为相对块号（相对于数据区起点块 516）
 *  CLAUDE.md § 4.3.2
 * ═══════════════════════════════════════════════════════════════ */

void ext2_sim_bfree(struct super_block *sb, uint16_t block_rel)
{
    struct ext2_sim_sb_info *sbi = EXT2_SIM_SB(sb);
    struct ext2_sim_group_desc_disk *gd;
    struct ext2_sim_super_block_disk *sb_disk;
    unsigned char *bmp;
    int byte, bit;

    if (block_rel >= EXT2_SIM_DATA_BLOCK_COUNTS) {
        printk(KERN_ERR "ext2sim: bfree: block %u out of range\n", block_rel);
        return;
    }

    bmp  = (unsigned char *)sbi->s_bbh->b_data;
    byte = block_rel / 8;
    bit  = block_rel % 8;

    /* 已经是空闲状态，不重复操作 */
    if (!(bmp[byte] & (128 >> bit))) {
        printk(KERN_WARNING "ext2sim: bfree: block %u already free\n", block_rel);
        return;
    }

    /* 清零位图 */
    bmp[byte] &= (unsigned char)~(128 >> bit);
    mark_buffer_dirty(sbi->s_bbh);

    /* 递增组描述符空闲计数 */
    gd = (struct ext2_sim_group_desc_disk *)sbi->s_gdbh->b_data;
    gd->bg_free_blocks_count = cpu_to_le16(
        le16_to_cpu(gd->bg_free_blocks_count) + 1);
    mark_buffer_dirty(sbi->s_gdbh);

    /* 同步递增超级块空闲计数 */
    sb_disk = (struct ext2_sim_super_block_disk *)sbi->s_sbh->b_data;
    sb_disk->s_free_blocks_count = cpu_to_le16(
        le16_to_cpu(sb_disk->s_free_blocks_count) + 1);
    mark_buffer_dirty(sbi->s_sbh);

    printk(KERN_INFO "ext2sim: bfree: freed block %u\n", block_rel);
}

/* ═══════════════════════════════════════════════════════════════
 *  ext2_sim_ialloc — 分配一个空闲 inode
 *  返回 inode 号 (1~4096)，0 = 失败
 *  CLAUDE.md § 4.3.3
 * ═══════════════════════════════════════════════════════════════ */

uint16_t ext2_sim_ialloc(struct super_block *sb)
{
    struct ext2_sim_sb_info *sbi = EXT2_SIM_SB(sb);
    struct ext2_sim_group_desc_disk *gd;
    struct ext2_sim_super_block_disk *sb_disk;
    unsigned char *bmp;
    uint16_t free_count;
    uint16_t ino;
    int start_byte;
    int byte, bit;

    gd = (struct ext2_sim_group_desc_disk *)sbi->s_gdbh->b_data;

    /* 1. 检查空闲计数 */
    free_count = le16_to_cpu(gd->bg_free_inodes_count);
    if (free_count == 0) {
        printk(KERN_ERR "ext2sim: ialloc: no free inodes\n");
        return 0;
    }

    /* 2. 从上次分配位置开始扫描 inode 位图 */
    bmp = (unsigned char *)sbi->s_ibh->b_data;
    start_byte = (sbi->s_last_alloc_inode - 1) / 8;

    ino = bitmap_scan_free(bmp, start_byte, BMP_BYTES);
    if (ino == 0xFFFF) {
        printk(KERN_ERR "ext2sim: ialloc: bitmap inconsistent (count=%u but no free bit)\n",
               free_count);
        return 0;
    }
    ino += 1;  /* bit N → inode N+1 */

    /* 3. 将该位置 1 */
    byte = (ino - 1) / 8;
    bit  = (ino - 1) % 8;
    bmp[byte] |= (unsigned char)(128 >> bit);
    mark_buffer_dirty(sbi->s_ibh);

    /* 4. 递减组描述符空闲计数 */
    gd->bg_free_inodes_count = cpu_to_le16(free_count - 1);
    mark_buffer_dirty(sbi->s_gdbh);

    /* 5. 同步递减超级块空闲计数 */
    sb_disk = (struct ext2_sim_super_block_disk *)sbi->s_sbh->b_data;
    sb_disk->s_free_inodes_count = cpu_to_le16(
        le16_to_cpu(sb_disk->s_free_inodes_count) - 1);
    mark_buffer_dirty(sbi->s_sbh);

    /* 6. 更新分配游标 */
    sbi->s_last_alloc_inode = ino;

    printk(KERN_INFO "ext2sim: ialloc: allocated inode %u, free=%u\n",
           ino, free_count - 1);

    return ino;
}

/* ═══════════════════════════════════════════════════════════════
 *  ext2_sim_ifree — 释放一个 inode
 *  CLAUDE.md § 4.3.4
 * ═══════════════════════════════════════════════════════════════ */

void ext2_sim_ifree(struct super_block *sb, uint16_t ino)
{
    struct ext2_sim_sb_info *sbi = EXT2_SIM_SB(sb);
    struct ext2_sim_group_desc_disk *gd;
    struct ext2_sim_super_block_disk *sb_disk;
    unsigned char *bmp;
    int byte, bit;

    if (ino < 1 || ino > EXT2_SIM_TOTAL_INODES) {
        printk(KERN_ERR "ext2sim: ifree: inode %u out of range\n", ino);
        return;
    }

    bmp  = (unsigned char *)sbi->s_ibh->b_data;
    byte = (ino - 1) / 8;
    bit  = (ino - 1) % 8;

    /* 已经空闲，不重复操作 */
    if (!(bmp[byte] & (128 >> bit))) {
        printk(KERN_WARNING "ext2sim: ifree: inode %u already free\n", ino);
        return;
    }

    /* 清零位图 */
    bmp[byte] &= (unsigned char)~(128 >> bit);
    mark_buffer_dirty(sbi->s_ibh);

    /* 递增组描述符空闲计数 */
    gd = (struct ext2_sim_group_desc_disk *)sbi->s_gdbh->b_data;
    gd->bg_free_inodes_count = cpu_to_le16(
        le16_to_cpu(gd->bg_free_inodes_count) + 1);
    mark_buffer_dirty(sbi->s_gdbh);

    /* 同步递增超级块空闲计数 */
    sb_disk = (struct ext2_sim_super_block_disk *)sbi->s_sbh->b_data;
    sb_disk->s_free_inodes_count = cpu_to_le16(
        le16_to_cpu(sb_disk->s_free_inodes_count) + 1);
    mark_buffer_dirty(sbi->s_sbh);

    printk(KERN_INFO "ext2sim: ifree: freed inode %u\n", ino);
}
