// SPDX-License-Identifier: GPL-2.0
/*
 * file.c — 文件读写、目录遍历、get_block 间接块寻址
 */

#include <linux/buffer_head.h>
#include <linux/uaccess.h>
#include "ext2_sim_fs.h"

/* ═════════════════════════════════════════════════════════════
 *  get_block — 逻辑块号 → 物理块号映射
 *  阶段 2：仅支持直接块（i_block[0]~[11]）
 *  阶段 9：扩展间接块支持
 * ═════════════════════════════════════════════════════════════ */

uint16_t ext2_sim_get_block(struct inode *inode, uint16_t logical,
                            int allocate)
{
    struct super_block *sb = inode->i_sb;
    struct buffer_head *bh;
    struct ext2_sim_inode_disk *raw;
    uint16_t block_rel;

    if (logical >= EXT2_SIM_DIRECT_BLOCKS) {
        /* TODO: Phase 9 — 间接块寻址 */
        return 0;
    }

    /* 从磁盘 inode 读取块指针 */
    bh = sb_bread(sb, EXT2_SIM_INODE_BLOCK(inode->i_ino));
    if (!bh)
        return 0;

    raw = (struct ext2_sim_inode_disk *)(bh->b_data
          + EXT2_SIM_INODE_OFFSET(inode->i_ino));
    block_rel = le16_to_cpu(raw->i_block[logical]);

    /*
     * allocate 模式：块不存在则分配新数据块。
     * 仅直接块支持分配（阶段 6），间接块在阶段 9 扩展。
     */
    if (allocate && block_rel == 0) {
        block_rel = ext2_sim_balloc(sb);
        if (block_rel == 0) {
            brelse(bh);
            return 0;  /* 磁盘满 */
        }

        /* 更新磁盘 inode 的 i_block[] */
        raw->i_block[logical] = cpu_to_le16(block_rel);
        raw->i_blocks = cpu_to_le16(le16_to_cpu(raw->i_blocks) + 1);
        mark_buffer_dirty(bh);

        /* 同步 VFS inode */
        inode->i_blocks++;

        /* 初始化新数据块为零 */
        {
            struct buffer_head *data_bh;
            int block_abs = EXT2_SIM_DATA_BLOCK_START + block_rel;
            data_bh = sb_bread(sb, block_abs);
            if (data_bh) {
                memset(data_bh->b_data, 0, EXT2_SIM_BLOCK_SIZE);
                mark_buffer_dirty(data_bh);
                brelse(data_bh);
            }
        }

        printk(KERN_INFO "ext2sim: get_block: allocated block %u for ino=%lu logical=%u\n",
               block_rel, inode->i_ino, logical);
    }

    brelse(bh);
    return block_rel;
}

/* ═════════════════════════════════════════════════════════════
 *  readdir — 目录遍历（ls 命令调用）
 * ═════════════════════════════════════════════════════════════ */

int ext2_sim_readdir(struct file *filp, struct dir_context *ctx)
{
    struct inode *inode = file_inode(filp);
    struct super_block *sb = inode->i_sb;
    struct buffer_head *bh;
    struct ext2_sim_dir_entry_disk *de;
    uint16_t block_rel;
    int block_abs;
    int entry_idx;     /* 当前块内的条目索引 */
    unsigned dtype;
    loff_t entry_pos;  /* 当前条目的 ctx->pos 对应位置 */
    uint16_t name_len;
    uint16_t ino;
    int i;

    /* ctx->pos 即目录内的字节偏移 */
    if (ctx->pos >= inode->i_size)
        return 0;  /* 已遍历完 */

    /* 计算当前在哪个逻辑块、哪个块内偏移 */
    uint16_t logical = ctx->pos / EXT2_SIM_BLOCK_SIZE;

    /* 获取物理块号 */
    block_rel = ext2_sim_get_block(inode, logical, 0);
    if (block_rel == 0 && logical > 0)
        return 0;

    block_abs = EXT2_SIM_DATA_BLOCK_START + block_rel;
    bh = sb_bread(sb, block_abs);
    if (!bh)
        return -EIO;

    /* 从块内偏移对应的条目开始遍历 */
    entry_idx = (ctx->pos % EXT2_SIM_BLOCK_SIZE) / 16;

    for (i = entry_idx; i < EXT2_SIM_DIR_ENTRIES_PER_BLOCK; i++) {
        de = (struct ext2_sim_dir_entry_disk *)(bh->b_data + i * 16);

        ino = le16_to_cpu(de->inode);
        if (ino == 0)
            continue;  /* 空条目，跳过 */

        entry_pos = (loff_t)logical * EXT2_SIM_BLOCK_SIZE + i * 16;

        /* 跳过已输出的条目 */
        if (entry_pos < ctx->pos)
            continue;

        /* 文件类型映射 */
        if (de->file_type == EXT2_SIM_FT_DIR)
            dtype = DT_DIR;
        else if (de->file_type == EXT2_SIM_FT_FILE)
            dtype = DT_REG;
        else
            dtype = DT_UNKNOWN;

        name_len = le16_to_cpu(de->name_len);

        /* 向 VFS 提交一个目录项 */
        if (!dir_emit(ctx, de->name, name_len, ino, dtype))
            break;  /* 用户缓冲区已满，下次继续 */

        ctx->pos = entry_pos + le16_to_cpu(de->rec_len);
    }

    brelse(bh);
    return 0;
}

/* ═════════════════════════════════════════════════════════════
 *  file_read / file_write — 文件读写（阶段 6）
 * ═════════════════════════════════════════════════════════════ */

ssize_t ext2_sim_file_read(struct file *filp, char __user *buf,
                           size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(filp);
    struct super_block *sb = inode->i_sb;
    struct buffer_head *bh;
    uint16_t logical, block_rel;
    int block_abs, offset;
    size_t chunk, total = 0;

    /* EOF */
    if (*ppos >= inode->i_size)
        return 0;

    /* 截断请求长度 */
    if ((loff_t)(*ppos + len) > inode->i_size)
        len = inode->i_size - *ppos;

    while (len > 0) {
        logical = *ppos / EXT2_SIM_BLOCK_SIZE;
        offset  = *ppos % EXT2_SIM_BLOCK_SIZE;

        block_rel = ext2_sim_get_block(inode, logical, 0);
        if (block_rel == 0) {
            /*
             * 空洞（未分配块）：填充零。
             * 稀疏文件在 seek past EOF 后写入会产生空洞，
             * 标准行为是返回零数据。
             */
            chunk = min_t(size_t, len, EXT2_SIM_BLOCK_SIZE - offset);
            if (clear_user(buf, chunk))
                return total > 0 ? (ssize_t)total : -EFAULT;
        } else {
            block_abs = EXT2_SIM_DATA_BLOCK_START + block_rel;
            bh = sb_bread(sb, block_abs);
            if (!bh)
                return total > 0 ? (ssize_t)total : -EIO;

            chunk = min_t(size_t, len, EXT2_SIM_BLOCK_SIZE - offset);
            if (copy_to_user(buf, bh->b_data + offset, chunk)) {
                brelse(bh);
                return total > 0 ? (ssize_t)total : -EFAULT;
            }
            brelse(bh);
        }

        buf   += chunk;
        len   -= chunk;
        total += chunk;
        *ppos += chunk;
    }

    /* 更新 atime */
    inode_set_atime_to_ts(inode, current_time(inode));
    mark_inode_dirty(inode);

    return (ssize_t)total;
}

ssize_t ext2_sim_file_write(struct file *filp, const char __user *buf,
                            size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(filp);
    struct super_block *sb = inode->i_sb;
    struct buffer_head *bh;
    uint16_t logical, block_rel;
    int block_abs, offset;
    size_t chunk, total = 0;

    /* v7.x: O_APPEND 时 VFS 可能未设置 *pos，手动修正 */
    if (filp->f_flags & O_APPEND)
        *ppos = inode->i_size;

    while (len > 0) {
        logical = *ppos / EXT2_SIM_BLOCK_SIZE;
        offset  = *ppos % EXT2_SIM_BLOCK_SIZE;

        /* allocate=1：块不存在则自动分配 */
        block_rel = ext2_sim_get_block(inode, logical, 1);
        if (block_rel == 0) {
            /* 磁盘满，无法继续分配 */
            printk(KERN_ERR "ext2sim: write: no space for logical=%u\n", logical);
            break;
        }

        block_abs = EXT2_SIM_DATA_BLOCK_START + block_rel;
        bh = sb_bread(sb, block_abs);
        if (!bh)
            return total > 0 ? (ssize_t)total : -EIO;

        chunk = min_t(size_t, len, EXT2_SIM_BLOCK_SIZE - offset);
        if (copy_from_user(bh->b_data + offset, buf, chunk)) {
            brelse(bh);
            return total > 0 ? (ssize_t)total : -EFAULT;
        }

        mark_buffer_dirty(bh);
        brelse(bh);

        buf   += chunk;
        len   -= chunk;
        total += chunk;
        *ppos += chunk;
    }

    /* 更新 i_size（如果写入了新数据超出原长度） */
    if (*ppos > inode->i_size)
        inode->i_size = *ppos;

    /* 更新 mtime / ctime */
    {
        struct timespec64 now = current_time(inode);
        inode_set_mtime_to_ts(inode, now);
        inode_set_ctime_to_ts(inode, now);
    }
    mark_inode_dirty(inode);

    return (ssize_t)total;
}

/* ═════════════════════════════════════════════════════════════
 *  getattr — 文件属性获取（stat 命令调用）
 * ═════════════════════════════════════════════════════════════ */

int ext2_sim_getattr(struct mnt_idmap *idmap, const struct path *path,
                     struct kstat *stat, u32 request_mask,
                     unsigned int query_flags)
{
    struct inode *inode = d_inode(path->dentry);

    generic_fillattr(idmap, request_mask, inode, stat);
    return 0;
}

/* ═════════════════════════════════════════════════════════════
 *  file_operations 注册
 * ═════════════════════════════════════════════════════════════ */

const struct file_operations ext2_sim_dir_file_operations = {
    .llseek         = generic_file_llseek,
    .read           = generic_read_dir,
    .iterate_shared = ext2_sim_readdir,
};

const struct file_operations ext2_sim_file_file_operations = {
    .llseek = generic_file_llseek,
    .read   = ext2_sim_file_read,
    .write  = ext2_sim_file_write,
};
