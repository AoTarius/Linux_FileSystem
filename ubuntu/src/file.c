// SPDX-License-Identifier: GPL-2.0
/*
 * file.c — 文件读写、目录遍历、get_block 间接块寻址
 */

#include <linux/buffer_head.h>
#include <linux/uaccess.h>
#include "ext2_sim_fs.h"

/* ── 辅助：分配并清零一个数据块，更新 VFS inode 计数 ────────── */
static uint16_t alloc_new_block(struct super_block *sb, struct inode *inode)
{
    uint16_t block_rel = ext2_sim_balloc(sb);
    struct buffer_head *data_bh;

    if (block_rel == 0)
        return 0;

    data_bh = sb_bread(sb, EXT2_SIM_DATA_BLOCK_START + block_rel);
    if (data_bh) {
        memset(data_bh->b_data, 0, EXT2_SIM_BLOCK_SIZE);
        mark_buffer_dirty(data_bh);
        brelse(data_bh);
    }
    inode->i_blocks++;
    return block_rel;
}

/* ═════════════════════════════════════════════════════════════
 *  get_block — 逻辑块号 → 相对块号映射
 *  支持直接块 + 一级/二级/三级间接块，allocate 模式自动扩展。
 * ═════════════════════════════════════════════════════════════ */

uint16_t ext2_sim_get_block(struct inode *inode, uint16_t logical,
                            int allocate)
{
    struct super_block *sb = inode->i_sb;
    struct buffer_head *inode_bh, *bh;
    struct ext2_sim_inode_disk *raw;
    uint16_t block_rel = 0;

    /* 读取磁盘 inode */
    inode_bh = sb_bread(sb, EXT2_SIM_INODE_BLOCK(inode->i_ino));
    if (!inode_bh)
        return 0;
    raw = (struct ext2_sim_inode_disk *)(inode_bh->b_data
          + EXT2_SIM_INODE_OFFSET(inode->i_ino));

    /* ═════════ 直接块 (0~11) ═════════ */
    if (logical < EXT2_SIM_DIRECT_BLOCKS) {
        block_rel = le16_to_cpu(raw->i_block[logical]);
        if (allocate && block_rel == 0) {
            block_rel = alloc_new_block(sb, inode);
            if (block_rel) {
                raw->i_block[logical] = cpu_to_le16(block_rel);
                raw->i_blocks = cpu_to_le16(le16_to_cpu(raw->i_blocks) + 1);
                mark_buffer_dirty(inode_bh);
                printk(KERN_INFO "ext2sim: get_block: direct block %u for ino=%lu logical=%u\n",
                       block_rel, inode->i_ino, logical);
            }
        }
        brelse(inode_bh);
        return block_rel;
    }

    /* ═════════ 一级间接块 (12~267) ═════════ */
    if (logical < EXT2_SIM_IND_BOUNDARY) {
        int idx = logical - EXT2_SIM_DIRECT_BLOCKS;
        uint16_t ind_block = le16_to_cpu(raw->i_block[EXT2_SIM_IND_BLOCK]);
        uint16_t *ptrs;

        if (ind_block == 0) {
            if (!allocate) { brelse(inode_bh); return 0; }
            ind_block = alloc_new_block(sb, inode);
            if (!ind_block) { brelse(inode_bh); return 0; }
            raw->i_block[EXT2_SIM_IND_BLOCK] = cpu_to_le16(ind_block);
            raw->i_blocks = cpu_to_le16(le16_to_cpu(raw->i_blocks) + 1);
            mark_buffer_dirty(inode_bh);
        }

        bh = sb_bread(sb, EXT2_SIM_DATA_BLOCK_START + ind_block);
        if (!bh) { brelse(inode_bh); return 0; }
        ptrs = (uint16_t *)bh->b_data;
        block_rel = le16_to_cpu(ptrs[idx]);

        if (allocate && block_rel == 0) {
            block_rel = alloc_new_block(sb, inode);
            if (block_rel) {
                ptrs[idx] = cpu_to_le16(block_rel);
                mark_buffer_dirty(bh);
            }
        }
        brelse(bh);
        brelse(inode_bh);
        return block_rel;
    }

    /* ═════════ 二级间接块 (268~65803) ═════════ */
    if (logical < EXT2_SIM_DIND_BOUNDARY) {
        int dbl_idx = (logical - EXT2_SIM_IND_BOUNDARY) / EXT2_SIM_INDIRECT_PTRS;
        int sgl_idx = (logical - EXT2_SIM_IND_BOUNDARY) % EXT2_SIM_INDIRECT_PTRS;
        uint16_t dind_block = le16_to_cpu(raw->i_block[EXT2_SIM_DIND_BLOCK]);
        uint16_t sgl_block;
        uint16_t *dptrs, *sptrs;

        if (dind_block == 0) {
            if (!allocate) { brelse(inode_bh); return 0; }
            dind_block = alloc_new_block(sb, inode);
            if (!dind_block) { brelse(inode_bh); return 0; }
            raw->i_block[EXT2_SIM_DIND_BLOCK] = cpu_to_le16(dind_block);
            raw->i_blocks = cpu_to_le16(le16_to_cpu(raw->i_blocks) + 1);
            mark_buffer_dirty(inode_bh);
        }

        /* 读二级间接块 → 取出对应的一级间接块号 */
        bh = sb_bread(sb, EXT2_SIM_DATA_BLOCK_START + dind_block);
        if (!bh) { brelse(inode_bh); return 0; }
        dptrs = (uint16_t *)bh->b_data;
        sgl_block = le16_to_cpu(dptrs[dbl_idx]);

        if (sgl_block == 0) {
            if (!allocate) { brelse(bh); brelse(inode_bh); return 0; }
            sgl_block = alloc_new_block(sb, inode);
            if (!sgl_block) { brelse(bh); brelse(inode_bh); return 0; }
            dptrs[dbl_idx] = cpu_to_le16(sgl_block);
            mark_buffer_dirty(bh);
        }
        brelse(bh);

        /* 读一级间接块 → 取出数据块号 */
        bh = sb_bread(sb, EXT2_SIM_DATA_BLOCK_START + sgl_block);
        if (!bh) { brelse(inode_bh); return 0; }
        sptrs = (uint16_t *)bh->b_data;
        block_rel = le16_to_cpu(sptrs[sgl_idx]);

        if (allocate && block_rel == 0) {
            block_rel = alloc_new_block(sb, inode);
            if (block_rel) {
                sptrs[sgl_idx] = cpu_to_le16(block_rel);
                mark_buffer_dirty(bh);
            }
        }
        brelse(bh);
        brelse(inode_bh);
        return block_rel;
    }

    /* ═════════ 三级间接块 (65804+) ═════════ */
    {
        int tpl_idx = (logical - EXT2_SIM_DIND_BOUNDARY)
                    / (EXT2_SIM_INDIRECT_PTRS * EXT2_SIM_INDIRECT_PTRS);
        int dbl_idx = ((logical - EXT2_SIM_DIND_BOUNDARY)
                    / EXT2_SIM_INDIRECT_PTRS) % EXT2_SIM_INDIRECT_PTRS;
        int sgl_idx = (logical - EXT2_SIM_DIND_BOUNDARY)
                    % EXT2_SIM_INDIRECT_PTRS;
        uint16_t tind_block = le16_to_cpu(raw->i_block[EXT2_SIM_TIND_BLOCK]);
        uint16_t dind_block, sgl_block;
        uint16_t *tptrs, *dptrs, *sptrs;

        if (tind_block == 0) {
            if (!allocate) { brelse(inode_bh); return 0; }
            tind_block = alloc_new_block(sb, inode);
            if (!tind_block) { brelse(inode_bh); return 0; }
            raw->i_block[EXT2_SIM_TIND_BLOCK] = cpu_to_le16(tind_block);
            raw->i_blocks = cpu_to_le16(le16_to_cpu(raw->i_blocks) + 1);
            mark_buffer_dirty(inode_bh);
        }

        /* 三级 → 二级 */
        bh = sb_bread(sb, EXT2_SIM_DATA_BLOCK_START + tind_block);
        if (!bh) { brelse(inode_bh); return 0; }
        tptrs = (uint16_t *)bh->b_data;
        dind_block = le16_to_cpu(tptrs[tpl_idx]);

        if (dind_block == 0) {
            if (!allocate) { brelse(bh); brelse(inode_bh); return 0; }
            dind_block = alloc_new_block(sb, inode);
            if (!dind_block) { brelse(bh); brelse(inode_bh); return 0; }
            tptrs[tpl_idx] = cpu_to_le16(dind_block);
            mark_buffer_dirty(bh);
        }
        brelse(bh);

        /* 二级 → 一级 */
        bh = sb_bread(sb, EXT2_SIM_DATA_BLOCK_START + dind_block);
        if (!bh) { brelse(inode_bh); return 0; }
        dptrs = (uint16_t *)bh->b_data;
        sgl_block = le16_to_cpu(dptrs[dbl_idx]);

        if (sgl_block == 0) {
            if (!allocate) { brelse(bh); brelse(inode_bh); return 0; }
            sgl_block = alloc_new_block(sb, inode);
            if (!sgl_block) { brelse(bh); brelse(inode_bh); return 0; }
            dptrs[dbl_idx] = cpu_to_le16(sgl_block);
            mark_buffer_dirty(bh);
        }
        brelse(bh);

        /* 一级 → 数据块 */
        bh = sb_bread(sb, EXT2_SIM_DATA_BLOCK_START + sgl_block);
        if (!bh) { brelse(inode_bh); return 0; }
        sptrs = (uint16_t *)bh->b_data;
        block_rel = le16_to_cpu(sptrs[sgl_idx]);

        if (allocate && block_rel == 0) {
            block_rel = alloc_new_block(sb, inode);
            if (block_rel) {
                sptrs[sgl_idx] = cpu_to_le16(block_rel);
                mark_buffer_dirty(bh);
            }
        }
        brelse(bh);
        brelse(inode_bh);
        return block_rel;
    }
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
