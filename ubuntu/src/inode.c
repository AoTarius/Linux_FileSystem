// SPDX-License-Identifier: GPL-2.0
/*
 * inode.c — inode 操作：iget、write_inode、lookup、create、mkdir、
 *           unlink、rmdir、evict_inode
 */

#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include "ext2_sim_fs.h"

/* ── 辅助：读取磁盘 inode 到局部变量 ───────────────────────── */
static struct ext2_sim_inode_disk *
read_disk_inode(struct super_block *sb, uint16_t ino, struct buffer_head **bh)
{
    *bh = sb_bread(sb, EXT2_SIM_INODE_BLOCK(ino));
    if (!*bh)
        return NULL;
    return (struct ext2_sim_inode_disk *)((*bh)->b_data
           + EXT2_SIM_INODE_OFFSET(ino));
}

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

    /* 已在缓存中 → 直接返回 */
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

/* ═════════════════════════════════════════════════════════════
 *  write_inode — 将 VFS inode 写回磁盘
 *  CLAUDE.md § 4.5.2
 *
 *  注意：只写 VFS 字段（mode/uid/gid/size/blocks/nlink/时间），
 *  不修改 i_block[]（i_block 由 create/mkdir/add_entry 直接管理）。
 * ═════════════════════════════════════════════════════════════ */

int ext2_sim_write_inode(struct inode *inode, struct writeback_control *wbc)
{
    struct super_block *sb = inode->i_sb;
    struct ext2_sim_inode_disk *raw;
    struct buffer_head *bh;

    bh = sb_bread(sb, EXT2_SIM_INODE_BLOCK(inode->i_ino));
    if (!bh)
        return -EIO;

    raw = (struct ext2_sim_inode_disk *)(bh->b_data
          + EXT2_SIM_INODE_OFFSET(inode->i_ino));

    raw->i_mode        = cpu_to_le16(inode->i_mode);
    raw->i_uid         = cpu_to_le16(i_uid_read(inode));
    raw->i_gid         = cpu_to_le16(i_gid_read(inode));
    raw->i_size        = cpu_to_le32(inode->i_size);
    raw->i_blocks      = cpu_to_le16(inode->i_blocks);
    raw->i_links_count = cpu_to_le16(inode->i_nlink);
    raw->i_atime       = cpu_to_le32((__u32)inode_get_atime_sec(inode));
    raw->i_mtime       = cpu_to_le32((__u32)inode_get_mtime_sec(inode));
    raw->i_ctime       = cpu_to_le32((__u32)inode_get_ctime_sec(inode));

    mark_buffer_dirty(bh);
    brelse(bh);

    return 0;
}

/* ═════════════════════════════════════════════════════════════
 *  lookup — 目录查找（任何路径解析都触发）
 *  CLAUDE.md § 4.5.3
 * ═════════════════════════════════════════════════════════════ */

struct dentry *ext2_sim_lookup(struct inode *dir, struct dentry *dentry,
                               unsigned int flags)
{
    struct ext2_sim_dir_entry_disk de;
    struct buffer_head *bh;
    struct inode *inode;
    int ret;

    if (dentry->d_name.len > EXT2_SIM_NAME_LEN)
        return ERR_PTR(-ENAMETOOLONG);

    ret = ext2_sim_dir_find_entry(dir, dentry->d_name.name,
                                   dentry->d_name.len, &de, &bh);
    if (ret != 0) {
        /* 条目不存在 → 负 dentry */
        return d_splice_alias(NULL, dentry);
    }

    inode = ext2_sim_iget(dir->i_sb, le16_to_cpu(de.inode));
    brelse(bh);

    if (IS_ERR(inode))
        return ERR_CAST(inode);

    return d_splice_alias(inode, dentry);
}

/* ═════════════════════════════════════════════════════════════
 *  create — 创建普通文件 (touch / open O_CREAT)
 *  CLAUDE.md § 4.5.4
 * ═════════════════════════════════════════════════════════════ */

int ext2_sim_create(struct mnt_idmap *idmap, struct inode *dir,
                    struct dentry *dentry, umode_t mode, bool excl)
{
    struct super_block *sb = dir->i_sb;
    struct ext2_sim_inode_disk *raw;
    struct buffer_head *bh;
    struct inode *inode;
    uint16_t ino;
    time64_t now;

    /* O_EXCL: 文件已存在则返回 -EEXIST */
    if (excl) {
        struct ext2_sim_dir_entry_disk dummy;
        struct buffer_head *tmp;
        if (ext2_sim_dir_find_entry(dir, dentry->d_name.name,
                                     dentry->d_name.len, &dummy, &tmp) == 0) {
            brelse(tmp);
            return -EEXIST;
        }
    }

    /* 1. 分配新 inode */
    ino = ext2_sim_ialloc(sb);
    if (ino == 0)
        return -ENOSPC;

    /* 2. 初始化磁盘 inode */
    raw = read_disk_inode(sb, ino, &bh);
    if (!raw)
        return -EIO;

    memset(raw, 0, sizeof(*raw));
    now = ktime_get_real_seconds();
    raw->i_mode        = cpu_to_le16(mode);              /* mode 含 S_IFREG */
    raw->i_uid         = 0;  /* 由 inode_init_owner 覆盖 */
    raw->i_gid         = 0;
    raw->i_links_count = cpu_to_le16(1);
    raw->i_size        = 0;
    raw->i_blocks      = 0;
    raw->i_atime       = cpu_to_le32((__u32)now);
    raw->i_ctime       = cpu_to_le32((__u32)now);
    raw->i_mtime       = cpu_to_le32((__u32)now);
    /* i_block 已在 memset 中清零 */

    mark_buffer_dirty(bh);
    brelse(bh);

    /* 3. 在父目录中新增条目 */
    {
        int err = ext2_sim_dir_add_entry(dir, dentry->d_name.name,
                                          dentry->d_name.len,
                                          ino, EXT2_SIM_FT_FILE);
        if (err) {
            ext2_sim_ifree(sb, ino);
            return err;
        }
    }

    /* 4. 获取 VFS inode 并设置正确的 uid/gid */
    inode = ext2_sim_iget(sb, ino);
    if (IS_ERR(inode)) {
        /* 回滚？inode 已分配且已加入目录，难以回滚干净。先报错。 */
        return PTR_ERR(inode);
    }

    inode_init_owner(idmap, inode, dir, mode);

    /* 将 inode_init_owner 设置的 uid/gid 立即持久化到磁盘 */
    {
        struct buffer_head *bh2;
        struct ext2_sim_inode_disk *raw2;
        bh2 = sb_bread(sb, EXT2_SIM_INODE_BLOCK(ino));
        if (bh2) {
            raw2 = (struct ext2_sim_inode_disk *)(bh2->b_data
                   + EXT2_SIM_INODE_OFFSET(ino));
            raw2->i_uid = cpu_to_le16(i_uid_read(inode));
            raw2->i_gid = cpu_to_le16(i_gid_read(inode));
            mark_buffer_dirty(bh2);
            brelse(bh2);
        }
    }

    mark_inode_dirty(inode);
    d_instantiate(dentry, inode);

    return 0;
}

/* ═════════════════════════════════════════════════════════════
 *  mkdir — 创建子目录
 *  CLAUDE.md § 4.5.5
 *
 *  v7.x 注意：返回类型为 struct dentry *（非 int）。
 *  成功返回 dentry，失败返回 ERR_PTR(-errno)。
 * ═════════════════════════════════════════════════════════════ */

struct dentry *ext2_sim_mkdir(struct mnt_idmap *idmap, struct inode *dir,
                               struct dentry *dentry, umode_t mode)
{
    struct super_block *sb = dir->i_sb;
    struct ext2_sim_sb_info *sbi = EXT2_SIM_SB(sb);
    struct ext2_sim_inode_disk *raw;
    struct ext2_sim_dir_entry_disk *de;
    struct buffer_head *bh, *dir_bh;
    struct inode *inode;
    struct ext2_sim_group_desc_disk *gd;
    uint16_t ino, block_rel;
    time64_t now;
    int err;

    /* 1. 分配 inode 和目录数据块 */
    ino = ext2_sim_ialloc(sb);
    if (ino == 0)
        return ERR_PTR(-ENOSPC);

    block_rel = ext2_sim_balloc(sb);
    if (block_rel == 0) {
        ext2_sim_ifree(sb, ino);
        return ERR_PTR(-ENOSPC);
    }

    /* 2. 初始化磁盘 inode */
    raw = read_disk_inode(sb, ino, &bh);
    if (!raw) {
        ext2_sim_bfree(sb, block_rel);
        ext2_sim_ifree(sb, ino);
        return ERR_PTR(-EIO);
    }

    memset(raw, 0, sizeof(*raw));
    now = ktime_get_real_seconds();
    raw->i_mode        = cpu_to_le16(S_IFDIR | mode);
    raw->i_uid         = 0;
    raw->i_gid         = 0;
    raw->i_links_count = cpu_to_le16(2);           /* . + 父目录的 .. */
    raw->i_size        = cpu_to_le32(32);          /* . 和 .. 各 16 字节 */
    raw->i_blocks      = cpu_to_le16(1);
    raw->i_block[0]    = cpu_to_le16(block_rel);
    raw->i_atime       = cpu_to_le32((__u32)now);
    raw->i_ctime       = cpu_to_le32((__u32)now);
    raw->i_mtime       = cpu_to_le32((__u32)now);

    mark_buffer_dirty(bh);
    brelse(bh);

    /* 3. 初始化目录数据块（. 和 ..） */
    dir_bh = sb_bread(sb, EXT2_SIM_DATA_BLOCK_START + block_rel);
    if (!dir_bh) {
        ext2_sim_bfree(sb, block_rel);
        ext2_sim_ifree(sb, ino);
        return ERR_PTR(-EIO);
    }

    memset(dir_bh->b_data, 0, EXT2_SIM_BLOCK_SIZE);

    /* 条目 0: "." */
    de = (struct ext2_sim_dir_entry_disk *)dir_bh->b_data;
    de->inode     = cpu_to_le16(ino);
    de->rec_len   = cpu_to_le16(16);
    de->name_len  = cpu_to_le16(1);
    de->file_type = EXT2_SIM_FT_DIR;
    de->name[0]   = '.';

    /* 条目 1: ".." — 指向父目录 */
    de = (struct ext2_sim_dir_entry_disk *)(dir_bh->b_data + 16);
    de->inode     = cpu_to_le16(dir->i_ino);
    de->rec_len   = cpu_to_le16(16);   /* 统一 16 字节对齐 */
    de->name_len  = cpu_to_le16(2);
    de->file_type = EXT2_SIM_FT_DIR;
    de->name[0]   = '.';
    de->name[1]   = '.';

    mark_buffer_dirty(dir_bh);
    brelse(dir_bh);

    /* 4. 递增组描述符的已分配目录计数 */
    gd = (struct ext2_sim_group_desc_disk *)sbi->s_gdbh->b_data;
    gd->bg_used_dirs_count = cpu_to_le16(
        le16_to_cpu(gd->bg_used_dirs_count) + 1);
    mark_buffer_dirty(sbi->s_gdbh);

    /* 5. 在父目录中新增条目 */
    err = ext2_sim_dir_add_entry(dir, dentry->d_name.name,
                                  dentry->d_name.len,
                                  ino, EXT2_SIM_FT_DIR);
    if (err) {
        /* 回滚已分配的资源 */
        {
            struct ext2_sim_group_desc_disk *g;
            g = (struct ext2_sim_group_desc_disk *)sbi->s_gdbh->b_data;
            g->bg_used_dirs_count = cpu_to_le16(
                le16_to_cpu(g->bg_used_dirs_count) - 1);
            mark_buffer_dirty(sbi->s_gdbh);
        }
        ext2_sim_bfree(sb, block_rel);
        ext2_sim_ifree(sb, ino);
        return ERR_PTR(err);
    }

    /* 6. 获取 VFS inode，设置 uid/gid */
    inode = ext2_sim_iget(sb, ino);
    if (IS_ERR(inode)) {
        /* 资源已部分分配，难以完全回滚 */
        return ERR_CAST(inode);
    }

    inode_init_owner(idmap, inode, dir, S_IFDIR | mode);

    /* 将 inode_init_owner 设置的 uid/gid 立即持久化到磁盘 */
    {
        struct buffer_head *bh2;
        struct ext2_sim_inode_disk *raw2;
        bh2 = sb_bread(sb, EXT2_SIM_INODE_BLOCK(ino));
        if (bh2) {
            raw2 = (struct ext2_sim_inode_disk *)(bh2->b_data
                   + EXT2_SIM_INODE_OFFSET(ino));
            raw2->i_uid = cpu_to_le16(i_uid_read(inode));
            raw2->i_gid = cpu_to_le16(i_gid_read(inode));
            mark_buffer_dirty(bh2);
            brelse(bh2);
        }
    }

    mark_inode_dirty(inode);
    d_instantiate(dentry, inode);

    return dentry;
}

/* ═════════════════════════════════════════════════════════════
 *  unlink — 删除普通文件
 *  CLAUDE.md § 4.5.6
 *
 *  注意：不在此释放数据块！数据块由 evict_inode（阶段 8）负责。
 * ═════════════════════════════════════════════════════════════ */

int ext2_sim_unlink(struct inode *dir, struct dentry *dentry)
{
    struct inode *inode = d_inode(dentry);
    int ret;

    /* 从父目录移除条目 */
    ret = ext2_sim_dir_remove_entry(dir, dentry->d_name.name,
                                     dentry->d_name.len);
    if (ret)
        return ret;

    /* 递减链接数（1 → 0，触发 evict_inode 回收） */
    set_nlink(inode, inode->i_nlink - 1);
    mark_inode_dirty(inode);

    return 0;
}

/* ═════════════════════════════════════════════════════════════
 *  rmdir — 删除空目录
 *  CLAUDE.md § 4.5.7
 *
 *  注意：不在此释放数据块！数据块由 evict_inode（阶段 8）负责。
 * ═════════════════════════════════════════════════════════════ */

int ext2_sim_rmdir(struct inode *dir, struct dentry *dentry)
{
    struct inode *inode = d_inode(dentry);
    struct ext2_sim_sb_info *sbi = EXT2_SIM_SB(dir->i_sb);
    struct ext2_sim_group_desc_disk *gd;
    int ret;

    /* 检查目录是否为空（只有 . 和 .. 两个条目，各 16 字节） */
    if (inode->i_size != 32)
        return -ENOTEMPTY;

    /* 从父目录移除条目 */
    ret = ext2_sim_dir_remove_entry(dir, dentry->d_name.name,
                                     dentry->d_name.len);
    if (ret)
        return ret;

    /* 目录已删除，nlink = 0 触发 evict_inode 回收资源 */
    set_nlink(inode, 0);
    mark_inode_dirty(inode);

    /* 递减组描述符的已分配目录计数 */
    gd = (struct ext2_sim_group_desc_disk *)sbi->s_gdbh->b_data;
    gd->bg_used_dirs_count = cpu_to_le16(
        le16_to_cpu(gd->bg_used_dirs_count) - 1);
    mark_buffer_dirty(sbi->s_gdbh);

    return 0;
}

/* ═════════════════════════════════════════════════════════════
 *  evict_inode — VFS 释放 inode 的最后一步
 *  当 i_count 归零且 nlink==0 时，回收所有磁盘资源。
 *  CLAUDE.md § 4.5.0-c
 *
 *  注意：仅处理直接块（阶段 8），间接块在阶段 9 扩展。
 * ═════════════════════════════════════════════════════════════ */

void ext2_sim_evict_inode(struct inode *inode)
{
    struct super_block *sb = inode->i_sb;
    struct buffer_head *bh;
    struct ext2_sim_inode_disk *raw;
    int logical;
    uint16_t block_rel;
    uint16_t freed_blocks = 0;

    /* 清除页缓存（虽然我们不用 address_space，安全调用无害） */
    truncate_inode_pages_final(&inode->i_data);

    /* 仅当文件/目录已被删除（nlink==0）时才回收磁盘资源 */
    if (inode->i_nlink == 0) {
        /* 读取磁盘 inode 获取 i_block[] 指针 */
        bh = sb_bread(sb, EXT2_SIM_INODE_BLOCK(inode->i_ino));
        if (bh) {
            raw = (struct ext2_sim_inode_disk *)(bh->b_data
                  + EXT2_SIM_INODE_OFFSET(inode->i_ino));

            /* 释放所有直接块（12 个） */
            for (logical = 0; logical < EXT2_SIM_DIRECT_BLOCKS; logical++) {
                block_rel = le16_to_cpu(raw->i_block[logical]);
                if (block_rel != 0) {
                    ext2_sim_bfree(sb, block_rel);
                    raw->i_block[logical] = 0;
                    freed_blocks++;
                }
            }

            /* TODO: Phase 9 — 释放间接块 (i_block[12..14]) */

            raw->i_blocks = 0;
            raw->i_size   = 0;
            mark_buffer_dirty(bh);
            brelse(bh);
        }

        /* 释放 inode 本身 */
        ext2_sim_ifree(sb, inode->i_ino);

        printk(KERN_INFO "ext2sim: evict: ino=%lu freed %u data blocks + inode\n",
               inode->i_ino, freed_blocks);
    }

    clear_inode(inode);
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
