/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ext2_sim_disk.h — 磁盘数据结构定义
 *
 * 磁盘上所有结构的字节级定义。使用 __le16 / __le32 标注小端序，
 * 读写时通过 le16_to_cpu() / cpu_to_le16() 转换。
 *
 * 磁盘布局（与 Windows_macOS 版本兼容）：
 *   块 0:     超级块       (32 bytes)
 *   块 1:     组描述符     (32 bytes)
 *   块 2:     块位图       (512 bytes = 4096 bits)
 *   块 3:     inode 位图   (512 bytes = 4096 bits)
 *   块 4~515: inode 表     (512 blocks × 8 inodes = 4096 inodes)
 *   块 516~4611: 数据块    (4096 blocks × 512 bytes)
 */

#ifndef _EXT2_SIM_DISK_H
#define _EXT2_SIM_DISK_H

#include <linux/types.h>

/* ── 超级块 (32 bytes, 块 0) ─────────────────────────────── */

struct ext2_sim_super_block_disk {
    __u8     s_volume_name[16];      /* 卷标，如 "EXT2FS"            */
    __le16   s_disk_size;            /* 磁盘总块数 = 4612            */
    __le16   s_blocks_per_group;     /* 每组块数 = 4612（单块组）    */
    __le16   s_size_per_block;       /* 每块字节数 = 512            */
    __le16   s_free_blocks_count;    /* 空闲数据块数                 */
    __le16   s_free_inodes_count;    /* 空闲 inode 数               */
    __u8     s_pad[6];               /* 填充至 32 bytes             */
};

/* ── 组描述符 (32 bytes, 块 1) ────────────────────────────── */

struct ext2_sim_group_desc_disk {
    __u8     bg_volume_name[16];      /* 卷标副本                     */
    __le16   bg_block_bitmap;         /* 块位图所在块号 = 2           */
    __le16   bg_inode_bitmap;         /* inode 位图所在块号 = 3       */
    __le16   bg_inode_table;          /* inode 表起始块号 = 4         */
    __le16   bg_free_blocks_count;    /* 本组空闲块数                 */
    __le16   bg_free_inodes_count;    /* 本组空闲 inode 数           */
    __le16   bg_used_dirs_count;      /* 已分配目录数                 */
    __u8     bg_pad[4];               /* 填充至 32 bytes             */
};

/* ── 磁盘 inode (64 bytes, inode 表每条) ───────────────────── */

struct ext2_sim_inode_disk {
    __le16   i_mode;                  /* 文件类型 + 权限位            */
    __le16   i_blocks;                /* 已分配数据块数量             */
    __le16   i_uid;                   /* 所有者 uid                  */
    __le16   i_gid;                   /* 所有者 gid                  */
    __le16   i_links_count;           /* 硬链接数                    */
    __le16   i_flags;                 /* 标志位                      */
    __le32   i_size;                  /* 文件字节大小                 */
    __le32   i_atime;                 /* 最后访问时间 (Unix timestamp) */
    __le32   i_ctime;                 /* 创建 / 元数据变更时间       */
    __le32   i_mtime;                 /* 最后修改时间                 */
    __le32   i_dtime;                 /* 删除时间                    */
    __le16   i_block[15];             /* 数据块指针数组               */
    __u8     i_pad[2];                /* 填充至 64 bytes             */
};

/*
 * i_block 寻址规则：
 *   [0]~[11]  直接块指针 (12 个)
 *   [12]      一级间接块指针 → 块内 256 个 __le16
 *   [13]      二级间接块指针 → 块内 256 个一级间接块指针
 *   [14]      三级间接块指针 → 块内 256 个二级间接块指针
 *   数据块号是相对于数据区起始块（块 516）的偏移
 */

/* ── 目录项 (16 bytes, 数据块内每条) ───────────────────────── */

#define EXT2_SIM_FT_UNKNOWN  0
#define EXT2_SIM_FT_FILE     1
#define EXT2_SIM_FT_DIR      2

struct ext2_sim_dir_entry_disk {
    __le16   inode;                  /* 指向的 inode 号（0 = 空槽）  */
    __le16   rec_len;                /* 目录项总长度                  */
    __le16   name_len;               /* 文件名长度                    */
    __u8     file_type;              /* 1=普通文件, 2=目录            */
    __u8     name[9];                /* 文件名 (最大 8 字符 + '\0')   */
};

/*
 * 每个数据块存 32 条目录项 (512 / 16)。
 * 目录前两条固定为 '.' (指向自己) 和 '..' (指向父目录)。
 * 空条目的 inode 字段为 0。
 */

#endif /* _EXT2_SIM_DISK_H */
