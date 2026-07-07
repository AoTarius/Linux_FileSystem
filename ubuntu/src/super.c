// SPDX-License-Identifier: GPL-2.0
/*
 * super.c — 挂载/卸载/统计 & 模块注册
 *           使用 fs_context API（适配内核 v5.4+，v7.x 必需）
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/fs_context.h>
#include <linux/time.h>
#include <linux/statfs.h>
#include "ext2_sim_fs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AoTarius");
MODULE_DESCRIPTION("EXT2 Simple File System Kernel Module");
MODULE_ALIAS_FS("ext2sim");

/* ── 前向声明 ──────────────────────────────────────────────── */

static int ext2_sim_format_disk(struct super_block *sb);

/* ── fs_context 操作 ──────────────────────────────────────── */

static int ext2_sim_get_tree(struct fs_context *fc)
{
    return get_tree_bdev(fc, ext2_sim_fill_super);
}

static const struct fs_context_operations ext2_sim_context_ops = {
    .get_tree = ext2_sim_get_tree,
};

static int ext2_sim_init_fs_context(struct fs_context *fc)
{
    fc->ops = &ext2_sim_context_ops;
    return 0;
}

/* ── 文件系统类型注册 ──────────────────────────────────────── */

struct file_system_type ext2_sim_fs_type = {
    .owner           = THIS_MODULE,
    .name            = "ext2sim",
    .init_fs_context = ext2_sim_init_fs_context,
    .kill_sb         = kill_block_super,
};

/* ── 模块入口 / 出口 ───────────────────────────────────────── */

static int __init ext2_sim_init(void)
{
    int ret;

    ret = register_filesystem(&ext2_sim_fs_type);
    if (ret)
        printk(KERN_ERR "ext2sim: register_filesystem failed (%d)\n", ret);
    else
        printk(KERN_INFO "ext2sim: module loaded\n");
    return ret;
}

static void __exit ext2_sim_exit(void)
{
    unregister_filesystem(&ext2_sim_fs_type);
    printk(KERN_INFO "ext2sim: module unloaded\n");
}

module_init(ext2_sim_init);
module_exit(ext2_sim_exit);

/* ═════════════════════════════════════════════════════════════
 *  磁盘格式化 — 首次挂载时自动初始化文件系统
 * ═════════════════════════════════════════════════════════════ */

static int ext2_sim_format_disk(struct super_block *sb)
{
    struct buffer_head *bh;
    struct ext2_sim_super_block_disk *sb_disk;
    struct ext2_sim_group_desc_disk *gd;
    struct ext2_sim_inode_disk *root_inode;
    struct ext2_sim_dir_entry_disk *de;
    time64_t now;
    int i;

    printk(KERN_INFO "ext2sim: formatting disk (auto-mkfs)\n");

    now = ktime_get_real_seconds();

    /* 1. 写超级块 (块 0) */
    bh = sb_bread(sb, EXT2_SIM_SB_BLOCK);
    if (!bh)
        return -EIO;
    memset(bh->b_data, 0, EXT2_SIM_BLOCK_SIZE);
    sb_disk = (struct ext2_sim_super_block_disk *)bh->b_data;
    memcpy(sb_disk->s_volume_name, "EXT2FS", 6);
    sb_disk->s_disk_size         = cpu_to_le16(EXT2_SIM_TOTAL_BLOCKS);
    sb_disk->s_blocks_per_group  = cpu_to_le16(EXT2_SIM_TOTAL_BLOCKS);
    sb_disk->s_size_per_block    = cpu_to_le16(EXT2_SIM_BLOCK_SIZE);
    sb_disk->s_free_blocks_count = cpu_to_le16(EXT2_SIM_DATA_BLOCK_COUNTS - 1);
    sb_disk->s_free_inodes_count = cpu_to_le16(EXT2_SIM_TOTAL_INODES - 1);
    mark_buffer_dirty(bh);
    brelse(bh);

    /* 2. 写组描述符 (块 1) */
    bh = sb_bread(sb, EXT2_SIM_GDT_BLOCK);
    if (!bh)
        return -EIO;
    memset(bh->b_data, 0, EXT2_SIM_BLOCK_SIZE);
    gd = (struct ext2_sim_group_desc_disk *)bh->b_data;
    memcpy(gd->bg_volume_name, "EXT2FS", 6);
    gd->bg_block_bitmap      = cpu_to_le16(EXT2_SIM_BLOCK_BMP_BLOCK);
    gd->bg_inode_bitmap      = cpu_to_le16(EXT2_SIM_INODE_BMP_BLOCK);
    gd->bg_inode_table       = cpu_to_le16(EXT2_SIM_INODE_TABLE_START);
    gd->bg_free_blocks_count = cpu_to_le16(EXT2_SIM_DATA_BLOCK_COUNTS - 1);
    gd->bg_free_inodes_count = cpu_to_le16(EXT2_SIM_TOTAL_INODES - 1);
    gd->bg_used_dirs_count   = cpu_to_le16(1);
    mark_buffer_dirty(bh);
    brelse(bh);

    /* 3. 初始化块位图 (块 2) — bit 0 = 1（根目录数据块已占用） */
    bh = sb_bread(sb, EXT2_SIM_BLOCK_BMP_BLOCK);
    if (!bh)
        return -EIO;
    memset(bh->b_data, 0, EXT2_SIM_BLOCK_SIZE);
    bh->b_data[0] = 0x80;  /* bit 0 = 1 */
    mark_buffer_dirty(bh);
    brelse(bh);

    /* 4. 初始化 inode 位图 (块 3) — bit 0 = 1（根 inode 已占用） */
    bh = sb_bread(sb, EXT2_SIM_INODE_BMP_BLOCK);
    if (!bh)
        return -EIO;
    memset(bh->b_data, 0, EXT2_SIM_BLOCK_SIZE);
    bh->b_data[0] = 0x80;  /* bit 0 = 1 */
    mark_buffer_dirty(bh);
    brelse(bh);

    /* 5. 初始化根 inode (inode #1) */
    bh = sb_bread(sb, EXT2_SIM_INODE_BLOCK(EXT2_SIM_ROOT_INO));
    if (!bh)
        return -EIO;
    root_inode = (struct ext2_sim_inode_disk *)(bh->b_data
        + EXT2_SIM_INODE_OFFSET(EXT2_SIM_ROOT_INO));
    memset(root_inode, 0, sizeof(*root_inode));
    root_inode->i_mode        = cpu_to_le16(S_IFDIR | EXT2_SIM_DEFAULT_DIR_MODE);
    root_inode->i_blocks      = cpu_to_le16(1);
    root_inode->i_uid         = cpu_to_le16(0);
    root_inode->i_gid         = cpu_to_le16(0);
    root_inode->i_links_count = cpu_to_le16(2);
    root_inode->i_size        = cpu_to_le32(EXT2_SIM_BLOCK_SIZE);
    root_inode->i_atime       = cpu_to_le32((__u32)now);
    root_inode->i_ctime       = cpu_to_le32((__u32)now);
    root_inode->i_mtime       = cpu_to_le32((__u32)now);
    root_inode->i_dtime       = 0;
    root_inode->i_block[0]    = cpu_to_le16(0);  /* 数据区第 0 块 = 绝对块 516 */
    for (i = 1; i < 15; i++)
        root_inode->i_block[i] = 0;
    mark_buffer_dirty(bh);
    brelse(bh);

    /* 6. 初始化根目录数据块 (绝对块 516) */
    bh = sb_bread(sb, EXT2_SIM_DATA_BLOCK_START);
    if (!bh)
        return -EIO;
    memset(bh->b_data, 0, EXT2_SIM_BLOCK_SIZE);

    /* 条目 0: "." */
    de = (struct ext2_sim_dir_entry_disk *)bh->b_data;
    de->inode     = cpu_to_le16(EXT2_SIM_ROOT_INO);
    de->rec_len   = cpu_to_le16(16);
    de->name_len  = cpu_to_le16(1);
    de->file_type = EXT2_SIM_FT_DIR;
    de->name[0]   = '.';

    /* 条目 1: ".." */
    de = (struct ext2_sim_dir_entry_disk *)(bh->b_data + 16);
    de->inode     = cpu_to_le16(EXT2_SIM_ROOT_INO);
    de->rec_len   = cpu_to_le16(EXT2_SIM_BLOCK_SIZE - 16);
    de->name_len  = cpu_to_le16(2);
    de->file_type = EXT2_SIM_FT_DIR;
    de->name[0]   = '.';
    de->name[1]   = '.';

    mark_buffer_dirty(bh);
    brelse(bh);

    printk(KERN_INFO "ext2sim: disk formatted successfully\n");
    return 0;
}

/* ═════════════════════════════════════════════════════════════
 *  fill_super — 挂载入口
 * ═════════════════════════════════════════════════════════════ */

int ext2_sim_fill_super(struct super_block *sb, struct fs_context *fc)
{
    struct ext2_sim_sb_info *sbi;
    struct ext2_sim_super_block_disk *sb_disk;
    struct inode *root_inode;
    int ret;

    /* 设置块大小 */
    sb->s_blocksize = EXT2_SIM_BLOCK_SIZE;
    sb->s_blocksize_bits = 9;  /* 512 = 2^9 */

    /* 分配 sbi */
    sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
    if (!sbi)
        return -ENOMEM;
    sb->s_fs_info = sbi;  /* put_super 负责释放 */

    /* 读取超级块 */
    sbi->s_sbh = sb_bread(sb, EXT2_SIM_SB_BLOCK);
    if (!sbi->s_sbh) {
        printk(KERN_ERR "ext2sim: unable to read superblock\n");
        ret = -EIO;
        goto failed;
    }

    sb_disk = (struct ext2_sim_super_block_disk *)sbi->s_sbh->b_data;

    /* 检查是否已格式化（s_size_per_block != 512 → 自动格式化） */
    if (le16_to_cpu(sb_disk->s_size_per_block) != EXT2_SIM_BLOCK_SIZE) {
        brelse(sbi->s_sbh);
        sbi->s_sbh = NULL;

        ret = ext2_sim_format_disk(sb);
        if (ret)
            goto failed;

        sbi->s_sbh = sb_bread(sb, EXT2_SIM_SB_BLOCK);
        if (!sbi->s_sbh) {
            printk(KERN_ERR "ext2sim: unable to read superblock after format\n");
            ret = -EIO;
            goto failed;
        }
        sb_disk = (struct ext2_sim_super_block_disk *)sbi->s_sbh->b_data;
    }

    /* 读取组描述符 */
    sbi->s_gdbh = sb_bread(sb, EXT2_SIM_GDT_BLOCK);
    if (!sbi->s_gdbh) {
        printk(KERN_ERR "ext2sim: unable to read group descriptor\n");
        ret = -EIO;
        goto failed;
    }

    /* 读取块位图 */
    sbi->s_bbh = sb_bread(sb, EXT2_SIM_BLOCK_BMP_BLOCK);
    if (!sbi->s_bbh) {
        printk(KERN_ERR "ext2sim: unable to read block bitmap\n");
        ret = -EIO;
        goto failed;
    }

    /* 读取 inode 位图 */
    sbi->s_ibh = sb_bread(sb, EXT2_SIM_INODE_BMP_BLOCK);
    if (!sbi->s_ibh) {
        printk(KERN_ERR "ext2sim: unable to read inode bitmap\n");
        ret = -EIO;
        goto failed;
    }

    /* 设置 SBI 初始分配游标 */
    sbi->s_last_alloc_block = EXT2_SIM_DATA_BLOCK_START;
    sbi->s_last_alloc_inode = EXT2_SIM_ROOT_INO;

    /* 设置 VFS super_block */
    sb->s_magic    = 0xEF53;
    sb->s_maxbytes = (loff_t)EXT2_SIM_DATA_BLOCK_COUNTS * EXT2_SIM_BLOCK_SIZE;
    sb->s_op       = &ext2_sim_sops;

    /* 读取根 inode */
    root_inode = ext2_sim_iget(sb, EXT2_SIM_ROOT_INO);
    if (IS_ERR(root_inode)) {
        printk(KERN_ERR "ext2sim: unable to read root inode (err=%ld)\n",
               PTR_ERR(root_inode));
        ret = PTR_ERR(root_inode);
        goto failed;
    }

    /* 创建根 dentry */
    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) {
        printk(KERN_ERR "ext2sim: unable to create root dentry\n");
        ret = -ENOMEM;
        goto failed;
    }

    printk(KERN_INFO "ext2sim: mounted vol=%.16s total_blocks=%u free_blocks=%u\n",
           sb_disk->s_volume_name,
           le16_to_cpu(sb_disk->s_disk_size),
           le16_to_cpu(sb_disk->s_free_blocks_count));

    return 0;

failed:
    /* put_super 由 VFS 自动调用来清理 sbi 中已分配的资源 */
    return ret;
}

/* ═════════════════════════════════════════════════════════════
 *  put_super — 卸载清理
 * ═════════════════════════════════════════════════════════════ */

void ext2_sim_put_super(struct super_block *sb)
{
    struct ext2_sim_sb_info *sbi = EXT2_SIM_SB(sb);

    if (!sbi)
        return;

    brelse(sbi->s_sbh);
    brelse(sbi->s_gdbh);
    brelse(sbi->s_bbh);
    brelse(sbi->s_ibh);

    kfree(sbi);
    sb->s_fs_info = NULL;

    printk(KERN_INFO "ext2sim: unmounted\n");
}

/* ═════════════════════════════════════════════════════════════
 *  alloc_inode / free_inode — VFS inode 生命周期
 * ═════════════════════════════════════════════════════════════ */

struct inode *ext2_sim_alloc_inode(struct super_block *sb)
{
    struct ext2_sim_inode_info *ei;

    ei = kmalloc(sizeof(*ei), GFP_KERNEL);
    if (!ei)
        return NULL;

    return &ei->vfs_inode;
}

void ext2_sim_free_inode(struct inode *inode)
{
    kfree(EXT2_SIM_I(inode));
}

/* ═════════════════════════════════════════════════════════════
 *  statfs — df 命令数据源
 * ═════════════════════════════════════════════════════════════ */

int ext2_sim_statfs(struct dentry *dentry, struct kstatfs *buf)
{
    struct super_block *sb = dentry->d_sb;
    struct ext2_sim_sb_info *sbi = EXT2_SIM_SB(sb);
    struct ext2_sim_group_desc_disk *gd;

    gd = (struct ext2_sim_group_desc_disk *)sbi->s_gdbh->b_data;

    buf->f_type    = 0xEF53;
    buf->f_bsize   = EXT2_SIM_BLOCK_SIZE;
    buf->f_blocks  = EXT2_SIM_DATA_BLOCK_COUNTS;
    buf->f_bfree   = le16_to_cpu(gd->bg_free_blocks_count);
    buf->f_bavail  = buf->f_bfree;
    buf->f_files   = EXT2_SIM_TOTAL_INODES;
    buf->f_ffree   = le16_to_cpu(gd->bg_free_inodes_count);
    buf->f_namelen = EXT2_SIM_NAME_LEN;
    buf->f_fsid    = 0;

    return 0;
}

/* ═════════════════════════════════════════════════════════════
 *  super_operations 注册
 * ═════════════════════════════════════════════════════════════ */

struct super_operations ext2_sim_sops = {
    .alloc_inode = ext2_sim_alloc_inode,
    .free_inode  = ext2_sim_free_inode,
    .write_inode = ext2_sim_write_inode,
    .evict_inode = ext2_sim_evict_inode,
    .put_super   = ext2_sim_put_super,
    .statfs      = ext2_sim_statfs,
};
