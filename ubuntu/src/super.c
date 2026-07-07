// SPDX-License-Identifier: GPL-2.0
/*
 * super.c — 挂载/卸载/统计 & 模块注册
 *           使用 fs_context API（适配内核 v5.4+，v7.x 必需）
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/fs_context.h>
#include "ext2_sim_fs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AoTarius");
MODULE_DESCRIPTION("EXT2 Simple File System Kernel Module");
MODULE_ALIAS_FS("ext2sim");

/* ── fs_context 操作（替代 mount_bdev）───────────────────── */

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

/* ── 文件系统类型注册 ────────────────────────────────────── */

struct file_system_type ext2_sim_fs_type = {
    .owner           = THIS_MODULE,
    .name            = "ext2sim",
    .init_fs_context = ext2_sim_init_fs_context,
    .kill_sb         = kill_block_super,
};

/* ── 模块入口 / 出口 ─────────────────────────────────────── */

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

/* ── fill_super — 挂载入口（TODO：CLAUDE.md § 4.1.1）────── */

int ext2_sim_fill_super(struct super_block *sb, void *data, int silent)
{
    /* TODO: Phase 2 */
    printk(KERN_ERR "ext2sim: fill_super not implemented\n");
    return -EINVAL;
}

void ext2_sim_put_super(struct super_block *sb)
{
    /* TODO: Phase 2 */
}

int ext2_sim_statfs(struct dentry *dentry, struct kstatfs *buf)
{
    /* TODO: Phase 2 */
    return -ENOSYS;
}

struct inode *ext2_sim_alloc_inode(struct super_block *sb)
{
    /* TODO: Phase 2 */
    return NULL;
}

void ext2_sim_free_inode(struct inode *inode)
{
    /* TODO: Phase 2 */
}
