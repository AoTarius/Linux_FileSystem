#ifndef _FS_CONTEXT_H
#define _FS_CONTEXT_H

#include <stdio.h>
#include "ext2_types.h"

/*
 * fs_context — 文件系统全局上下文
 *
 * 定义于 context.c，通过 extern 供所有内部模块直接访问。
 * 外部模块（main.c / shell.c）通过 main.h 的 API 函数间接使用。
 *
 * Phase 3 ✅: ctx 为全局单例，各模块通过 extern 直接访问
 */
struct fs_context {
    FILE *fp;                       // 虚拟磁盘文件指针

    /* 磁盘元数据缓冲区 */
    struct super_block sb;          // 超级块
    struct group_desc gd;           // 组描述符
    unsigned char block_bmp[512];   // 块位图 (512B = 4096 bits)
    unsigned char inode_bmp[512];   // inode 位图

    /* 缓存区 */
    struct inode inode_cache;       // inode 缓存（单条目）
    struct dir_entry dir_cache[32]; // 目录项缓存（一个块 = 32 条）
    char data_buf[512];             // 数据块读写缓冲
    char write_buf[4096];           // 文件写入临时缓冲

    /* 运行时状态 */
    unsigned short last_alloc_inode; // 最近分配的 inode 号
    unsigned short last_alloc_block; // 最近分配的数据块号
    unsigned short current_dir;      // 当前目录 inode 号
    unsigned short current_dirlen;   // 当前目录名长度
    short fopen_table[16];           // 文件打开表（存 inode 号）
    char current_path[256];          // 当前路径字符串

    /* 用户会话 */
    unsigned short current_uid;      // 当前登录用户的 uid
    unsigned short current_gid;      // 当前登录用户的 gid
    char current_user[32];           // 当前登录用户名
    int logged_in;                   // 是否已登录 (1=是, 0=否)
};

/* 全局单例 — 定义于 context.c */
extern struct fs_context ctx;

/* 上下文生命周期 */
int  fs_init(void);
void fs_shutdown(void);

/* 公开访问器 */
const char *get_current_path(void);
void check_disk(void);
void format(void);

#endif // _FS_CONTEXT_H
