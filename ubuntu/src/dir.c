// SPDX-License-Identifier: GPL-2.0
/*
 * dir.c — 目录项操作：查找、添加、删除条目
 *
 * 目录项以 16 字节对齐排列，每块 32 条。
 * inode == 0 表示空槽，可被复用。
 * 名称比较固定 8 字符以内，namelen 由 VFS 提供。
 */

#include <linux/buffer_head.h>
#include <linux/string.h>
#include "ext2_sim_fs.h"

/* ═══════════════════════════════════════════════════════════════
 *  ext2_sim_dir_find_entry — 在目录中按名查找条目
 *  CLAUDE.md § 4.4.1
 * ═══════════════════════════════════════════════════════════════ */

int ext2_sim_dir_find_entry(struct inode *dir, const char *name, int namelen,
                            struct ext2_sim_dir_entry_disk *result,
                            struct buffer_head **res_bh)
{
    struct super_block *sb = dir->i_sb;
    struct buffer_head *bh = NULL;
    struct ext2_sim_dir_entry_disk *de;
    uint16_t logical, block_rel;
    int block_abs;
    int i;

    *res_bh = NULL;

    /*
     * 遍历目录的所有已分配数据块。
     * 注意：block_rel=0 表示"数据区第 0 块"（绝对块 516），是合法块！
     * 不能用 block_rel==0 判断块是否存在——i_blocks 的语义才是"已分配块数"。
     */
    for (logical = 0; logical < dir->i_blocks; logical++) {
        block_rel = ext2_sim_get_block(dir, logical, 0);
        /* block_rel==0 是合法块（相对块 0），继续处理 */

        block_abs = EXT2_SIM_DATA_BLOCK_START + block_rel;
        bh = sb_bread(sb, block_abs);
        if (!bh)
            return -EIO;

        /* 扫描块内所有 32 个条目位 */
        for (i = 0; i < EXT2_SIM_DIR_ENTRIES_PER_BLOCK; i++) {
            de = (struct ext2_sim_dir_entry_disk *)(bh->b_data + i * 16);

            /* 已校验完目录的有效数据范围则停止 */
            if ((loff_t)logical * EXT2_SIM_BLOCK_SIZE + i * 16 >= dir->i_size) {
                brelse(bh);
                return -ENOENT;
            }

            if (le16_to_cpu(de->inode) == 0)
                continue;  /* 空槽 */

            if (de->name_len != 0 &&
                (int)le16_to_cpu(de->name_len) == namelen &&
                memcmp(de->name, name, namelen) == 0) {
                /* 找到，拷贝到 result，保留 bh 引用 */
                memcpy(result, de, sizeof(*result));
                *res_bh = bh;
                return 0;
            }
        }
        brelse(bh);
    }

    return -ENOENT;
}

/* ═══════════════════════════════════════════════════════════════
 *  ext2_sim_dir_add_entry — 在目录中新增一个条目
 *  CLAUDE.md § 4.4.2
 * ═══════════════════════════════════════════════════════════════ */

int ext2_sim_dir_add_entry(struct inode *dir, const char *name, int namelen,
                           uint16_t ino, uint8_t file_type)
{
    struct super_block *sb = dir->i_sb;
    struct buffer_head *bh = NULL;
    struct ext2_sim_dir_entry_disk *de;
    uint16_t logical, block_rel;
    int block_abs;
    int i;

    /* 1. 在已有数据块中找空槽 (inode == 0) */
    for (logical = 0; logical < dir->i_blocks; logical++) {
        block_rel = ext2_sim_get_block(dir, logical, 0);
        /* block_rel==0 是合法块（数据区第 0 块），不跳出 */

        block_abs = EXT2_SIM_DATA_BLOCK_START + block_rel;
        bh = sb_bread(sb, block_abs);
        if (!bh)
            return -EIO;

        for (i = 0; i < EXT2_SIM_DIR_ENTRIES_PER_BLOCK; i++) {
            de = (struct ext2_sim_dir_entry_disk *)(bh->b_data + i * 16);
            if (le16_to_cpu(de->inode) == 0) {
                /* 找到空槽，直接填充 */
                goto fill_entry;
            }
        }
        brelse(bh);
        bh = NULL;
    }
    printk(KERN_INFO "ext2sim: add_entry: no empty slot, allocating new block (dir ino=%lu i_blocks=%lu)\n",
           dir->i_ino, (unsigned long)dir->i_blocks);

    /*
     * 2. 无空槽 → 分配新数据块。
     *    逻辑块号 = 当前 i_blocks（即紧接最后一个已分配块之后）。
     *    仅支持直接块（12 个），目录通常不超过此限制。
     */
    if (dir->i_blocks >= EXT2_SIM_DIRECT_BLOCKS) {
        printk(KERN_ERR "ext2sim: add_entry: directory block limit reached\n");
        return -ENOSPC;
    }

    block_rel = ext2_sim_balloc(sb);
    if (block_rel == 0)
        return -ENOSPC;

    /* 更新磁盘 inode 的 i_block[] 和 i_blocks */
    {
        struct buffer_head *inode_bh;
        struct ext2_sim_inode_disk *raw;

        inode_bh = sb_bread(sb, EXT2_SIM_INODE_BLOCK(dir->i_ino));
        if (!inode_bh)
            return -EIO;

        raw = (struct ext2_sim_inode_disk *)(inode_bh->b_data
              + EXT2_SIM_INODE_OFFSET(dir->i_ino));
        raw->i_block[dir->i_blocks] = cpu_to_le16(block_rel);
        raw->i_blocks = cpu_to_le16(le16_to_cpu(raw->i_blocks) + 1);
        mark_buffer_dirty(inode_bh);
        brelse(inode_bh);
    }

    dir->i_blocks++;  /* VFS inode 同步 */

    /* 读取新块并清零 */
    block_abs = EXT2_SIM_DATA_BLOCK_START + block_rel;
    bh = sb_bread(sb, block_abs);
    if (!bh)
        return -EIO;
    memset(bh->b_data, 0, EXT2_SIM_BLOCK_SIZE);

    /* 新块第一个条目 */
    de = (struct ext2_sim_dir_entry_disk *)bh->b_data;
    /* fall through 到 fill_entry */

fill_entry:
    /* 安全截断：文件名不超过 EXT2_SIM_NAME_LEN 字节 */
    if (namelen > EXT2_SIM_NAME_LEN)
        namelen = EXT2_SIM_NAME_LEN;

    /* 填充目录项 */
    de->inode     = cpu_to_le16(ino);
    de->rec_len   = cpu_to_le16(16);
    de->name_len  = cpu_to_le16((uint16_t)namelen);
    de->file_type = file_type;
    memset(de->name, 0, sizeof(de->name));
    if (namelen > 0)
        memcpy(de->name, name, namelen);

    mark_buffer_dirty(bh);
    brelse(bh);

    /* 更新目录元数据 */
    {
        struct timespec64 now = current_time(dir);
        dir->i_size  += 16;
        inode_set_mtime_to_ts(dir, now);
        inode_set_ctime_to_ts(dir, now);
        mark_inode_dirty(dir);
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  ext2_sim_dir_remove_entry — 从目录中删除指定条目
 *  CLAUDE.md § 4.4.3
 * ═══════════════════════════════════════════════════════════════ */

int ext2_sim_dir_remove_entry(struct inode *dir, const char *name, int namelen)
{
    struct ext2_sim_dir_entry_disk de;
    struct buffer_head *bh;
    struct ext2_sim_dir_entry_disk *entry;
    int ret;
    int i;

    /* 1. 定位条目（find_entry 已验证存在并返回 bh） */
    ret = ext2_sim_dir_find_entry(dir, name, namelen, &de, &bh);
    if (ret != 0)
        return ret;

    /* 2. 在 bh 中重新扫描找到匹配条目并清零 inode */
    for (i = 0; i < EXT2_SIM_DIR_ENTRIES_PER_BLOCK; i++) {
        entry = (struct ext2_sim_dir_entry_disk *)(bh->b_data + i * 16);
        if (le16_to_cpu(entry->inode) != 0 &&
            (int)le16_to_cpu(entry->name_len) == namelen &&
            memcmp(entry->name, name, namelen) == 0) {
            entry->inode = 0;   /* 标记空槽 */
            break;
        }
    }

    mark_buffer_dirty(bh);
    brelse(bh);

    /* 3. 更新目录元数据 */
    {
        struct timespec64 now = current_time(dir);
        if (dir->i_size >= 16)
            dir->i_size -= 16;
        inode_set_mtime_to_ts(dir, now);
        inode_set_ctime_to_ts(dir, now);
        mark_inode_dirty(dir);
    }

    return 0;
}
