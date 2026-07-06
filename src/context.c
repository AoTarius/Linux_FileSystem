/*
 * context.c — 全局上下文 & 生命周期管理
 *
 * 定义全局单例 struct fs_context ctx，所有模块通过 extern 访问。
 * 负责：磁盘创建 / 挂载 / 格式化 / 状态查询 / 安全关闭。
 */

#include <stdio.h>
#include <string.h>
#include "ext2_constants.h"
#include "fs_context.h"
#include "disk_io.h"

/* ---- 全局单例 ---- */
struct fs_context ctx;

/* ---- 内部辅助：创建空白虚拟磁盘 ---- */
static void fs_mkfs(void)
{
    int i;

    printf("Creating the ext2 file system\n");
    printf("Please wait ... \n");

    ctx.last_alloc_inode = 1;
    ctx.last_alloc_block = 0;
    for (i = 0; i < 16; i++)
        ctx.fopen_table[i] = 0;
    for (i = 0; i < BLOCK_SIZE; i++)
        ctx.data_buf[i] = 0;

    if (ctx.fp != NULL)
        fclose(ctx.fp);

    ctx.fp = fopen("./Ext2", "w+");
    fseek(ctx.fp, DISK_START, SEEK_SET);
    for (i = 0; i < DISK_SIZE; i++)
        fwrite(ctx.data_buf, BLOCK_SIZE, 1, ctx.fp);

    /* 初始化超级块 */
    sb_read();
    strcpy(ctx.sb.sb_volume_name, VOLUME_NAME);
    ctx.sb.sb_disk_size = DISK_SIZE;
    ctx.sb.sb_blocks_per_group = BLOCKS_PER_GROUP;
    ctx.sb.sb_size_per_block = BLOCK_SIZE;
    sb_write();

    /* 初始化组描述符 */
    gd_read();
    ctx.gd.bg_block_bitmap = BLOCK_BITMAP;
    ctx.gd.bg_inode_bitmap = INODE_BITMAP;
    ctx.gd.bg_inode_table = INODE_TABLE;
    ctx.gd.bg_free_blocks_count = DATA_BLOCK_COUNTS;
    ctx.gd.bg_free_inodes_count = INODE_TABLE_COUNTS;
    ctx.gd.bg_used_dirs_count = 0;
    gd_write();

    block_bmp_read();
    inode_bmp_read();

    /* 初始化根目录 inode */
    ctx.inode_cache.i_mode = 518;
    ctx.inode_cache.i_blocks = 0;
    ctx.inode_cache.i_size = 32;
    ctx.inode_cache.i_atime = 0;
    ctx.inode_cache.i_ctime = 0;
    ctx.inode_cache.i_mtime = 0;
    ctx.inode_cache.i_dtime = 0;

    /* 为根目录分配数据块 — 手动操作位图（避免引入 bitmap 模块依赖） */
    {
        unsigned short cur = ctx.last_alloc_block;
        unsigned char con = 128;
        int flag = 0;
        cur /= 8;
        while (ctx.block_bmp[cur] == 255) {
            if (cur == 511) cur = 0;
            else cur++;
        }
        while (ctx.block_bmp[cur] & con) {
            con = con / 2;
            flag++;
        }
        ctx.block_bmp[cur] = ctx.block_bmp[cur] + con;
        ctx.last_alloc_block = cur * 8 + flag;
        ctx.inode_cache.i_block[0] = ctx.last_alloc_block;
        block_bmp_write();
        ctx.gd.bg_free_blocks_count--;
        gd_write();
    }

    ctx.inode_cache.i_blocks++;

    /* 分配根目录 inode */
    {
        unsigned short cur = ctx.last_alloc_inode;
        unsigned char con = 128;
        int flag = 0;
        cur = (cur - 1) / 8;
        while (ctx.inode_bmp[cur] == 255) {
            if (cur == 511) cur = 0;
            else cur++;
        }
        while (ctx.inode_bmp[cur] & con) {
            con = con / 2;
            flag++;
        }
        ctx.inode_bmp[cur] = ctx.inode_bmp[cur] + con;
        ctx.last_alloc_inode = cur * 8 + flag + 1;
        ctx.current_dir = ctx.last_alloc_inode;
        inode_bmp_write();
        ctx.gd.bg_free_inodes_count--;
        gd_write();
    }

    inode_write(ctx.current_dir);

    /* 初始化根目录的 . 和 .. */
    ctx.dir_cache[0].inode = ctx.dir_cache[1].inode = ctx.current_dir;
    ctx.dir_cache[0].name_len = 0;
    ctx.dir_cache[1].name_len = 0;
    ctx.dir_cache[0].file_type = ctx.dir_cache[1].file_type = 2;
    strcpy(ctx.dir_cache[0].name, ".");
    strcpy(ctx.dir_cache[1].name, "..");
    dir_write(ctx.inode_cache.i_block[0]);

    printf("The ext2 file system has been installed!\n");
    check_disk();
    fclose(ctx.fp);
    ctx.fp = NULL;
}

/* ---- 初始化（挂载）文件系统 ---- */

int fs_init(void)
{
    int need_init = 0;

    ctx.last_alloc_inode = 1;
    ctx.last_alloc_block = 0;
    {
        int i;
        for (i = 0; i < 16; i++)
            ctx.fopen_table[i] = 0;
    }
    strcpy(ctx.current_path, "[root@ /");
    ctx.current_dir = 1;

    ctx.fp = fopen("./Ext2", "r+");
    if (ctx.fp == NULL) {
        printf("The File system does not exist!\n");
        fs_mkfs();
        need_init = 1;
    } else {
        sb_read();
        if (strcmp(ctx.sb.sb_volume_name, VOLUME_NAME)) {
            printf("The File system [%s] is not suppoted yet!\n",
                   ctx.sb.sb_volume_name);
            printf("The File system loaded error!\n");
            fclose(ctx.fp);
            fs_mkfs();
            need_init = 1;
        }
    }

    if (need_init) {
        ctx.fp = fopen("./Ext2", "r+");
        if (ctx.fp == NULL) {
            printf("Fatal: cannot open filesystem after creation!\n");
            return -1;
        }
        sb_read();
        gd_read();
        return 0;
    }

    gd_read();
    return 0;
}

/* ---- 安全关闭 ---- */

void fs_shutdown(void)
{
    if (ctx.fp != NULL) {
        fclose(ctx.fp);
        ctx.fp = NULL;
    }
}

/* ---- 查询 API ---- */

const char *get_current_path(void)
{
    return ctx.current_path;
}

void check_disk(void)
{
    sb_read();
    printf("volume name       : %s\n", ctx.sb.sb_volume_name);
    printf("disk size         : %d(blocks)\n", ctx.sb.sb_disk_size);
    printf("blocks per group  : %d(blocks)\n", ctx.sb.sb_blocks_per_group);
    printf("ext2 file size    : %d(kb)\n",
           ctx.sb.sb_disk_size * ctx.sb.sb_size_per_block / 1024);
    printf("block size        : %d(kb)\n", ctx.sb.sb_size_per_block);
}

/* ---- 格式化 ---- */

void format(void)
{
    fs_mkfs();       /* 重新创建磁盘 */
    fs_init();       /* 重新挂载 */
    /* 注意: fs_mkfs 内部 fclose，fs_init 重新 fopen */
}
