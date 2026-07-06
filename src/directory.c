/*
 * directory.c — 层 2：目录项操作
 *
 * dir_lookup : 在当前目录中搜索文件/目录
 * dir_entry_init : 为新文件/目录初始化 inode（分配块、设置 . 和 ..）
 * dir_list   : 列出当前目录内容 (ls)
 * file_is_open : 检查 inode 是否在打开表中
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "fs_context.h"
#include "ext2_constants.h"
#include "disk_io.h"
#include "bitmap.h"
#include "directory.h"

/* ---- 目录查找 ---- */

int dir_lookup(const char *name, int file_type,
               unsigned short *inode_no,
               unsigned short *block_no,
               unsigned short *entry_no)
{
    unsigned short j, k;

    inode_read(ctx.current_dir);
    for (j = 0; j < ctx.inode_cache.i_blocks; j++) {
        dir_read(ctx.inode_cache.i_block[j]);
        k = 0;
        while (k < 32) {
            if (!ctx.dir_cache[k].inode
                || ctx.dir_cache[k].file_type != file_type
                || strcmp(ctx.dir_cache[k].name, name))
            {
                k++;
            } else {
                *inode_no = ctx.dir_cache[k].inode;
                *block_no = j;
                *entry_no  = k;
                return 1;
            }
        }
    }
    return 0;
}

/* ---- 新条目初始化 ---- */

void dir_entry_init(unsigned short ino, unsigned short name_len, int type)
{
    inode_read(ino);

    if (type == 2) {  /* 目录 */
        ctx.inode_cache.i_size = 32;
        ctx.inode_cache.i_blocks = 1;
        ctx.inode_cache.i_block[0] = balloc();
        ctx.dir_cache[0].inode = ino;
        ctx.dir_cache[1].inode = ctx.current_dir;
        ctx.dir_cache[0].name_len = name_len;
        ctx.dir_cache[1].name_len = ctx.current_dirlen;
        ctx.dir_cache[0].file_type = ctx.dir_cache[1].file_type = 2;

        for (type = 2; type < 32; type++)
            ctx.dir_cache[type].inode = 0;

        strcpy(ctx.dir_cache[0].name, ".");
        strcpy(ctx.dir_cache[1].name, "..");
        dir_write(ctx.inode_cache.i_block[0]);

        ctx.inode_cache.i_mode = DEFAULT_DIR_MODE;
    } else {  /* 普通文件 */
        ctx.inode_cache.i_size = 0;
        ctx.inode_cache.i_blocks = 0;
        ctx.inode_cache.i_mode = DEFAULT_FILE_MODE;
    }
    /* 设置文件所有者信息 */
    ctx.inode_cache.i_uid = ctx.current_uid;
    ctx.inode_cache.i_gid = ctx.current_gid;

    /* 设置时间戳 — 创建时 atime / mtime / ctime 均为当前时间 */
    {
        time_t now = time(NULL);
        ctx.inode_cache.i_atime = (unsigned long)now;
        ctx.inode_cache.i_mtime = (unsigned long)now;
        ctx.inode_cache.i_ctime = (unsigned long)now;
    }

    inode_write(ino);
}

/* ---- 打开表查询 ---- */

int file_is_open(unsigned short ino)
{
    unsigned short i = 0;
    while (i < 16) {
        if (ctx.fopen_table[i] == ino)
            return 1;
        i++;
    }
    return 0;
}

/* ---- 列目录 (ls) ---- */

/*
 * 格式化数据块号字符串，用于"物理地址"列显示。
 * 无数据块（i_blocks==0）时输出 "----"；
 * 有数据块时输出逗号分隔的绝对磁盘块号。
 */
static void format_block_str(char *out, int maxlen,
                             const unsigned short *blocks,
                             unsigned short count)
{
    unsigned short b;
    if (count == 0) {
        strcpy(out, "----");
        return;
    }
    out[0] = '\0';
    for (b = 0; b < count; b++) {
        char num[8];
        /* 绝对磁盘块号 = DATA_BLOCK/BLOCK_SIZE + 相对索引 */
        sprintf(num, "%s%u", b > 0 ? "," : "",
                (unsigned)(blocks[b] + DATA_BLOCK / BLOCK_SIZE));
        strncat(out, num, maxlen - strlen(out) - 1);
    }
}

/*
 * 格式化权限为 9 字符 rwxrwxrwx 字符串。
 *   mode & S_IRUSR → 'r' else '-',  mode & S_IWUSR → 'w' else '-', ...
 */
static void format_mode_str(char *out, unsigned short mode)
{
    out[0] = (mode & S_IRUSR) ? 'r' : '-';
    out[1] = (mode & S_IWUSR) ? 'w' : '-';
    out[2] = (mode & S_IXUSR) ? 'x' : '-';
    out[3] = (mode & S_IRGRP) ? 'r' : '-';
    out[4] = (mode & S_IWGRP) ? 'w' : '-';
    out[5] = (mode & S_IXGRP) ? 'x' : '-';
    out[6] = (mode & S_IROTH) ? 'r' : '-';
    out[7] = (mode & S_IWOTH) ? 'w' : '-';
    out[8] = (mode & S_IXOTH) ? 'x' : '-';
    out[9] = '\0';
}

/*
 * 格式化修改时间为 "MM-DD HH:MM" 字符串。
 * 时间戳为 0 时输出 "----"。
 */
static void format_time_str(char *out, int maxlen, unsigned long timestamp)
{
    if (timestamp == 0) {
        strcpy(out, "----");
        return;
    }
    {
        time_t t = (time_t)timestamp;
        struct tm *tm_info = localtime(&t);
        strftime(out, (size_t)maxlen, "%m-%d %H:%M:%S", tm_info);
    }
}

void dir_list(void)
{
    unsigned short i, j, k, type_flag;
    char block_str[32], time_str[16];
    int blk_len, time_len;

    printf("items          type           mode         blocks             mtime            size\n");
    inode_read(ctx.current_dir);
    for (i = 0; i < ctx.inode_cache.i_blocks; i++) {
        dir_read(ctx.inode_cache.i_block[i]);
        for (k = 0; k < 32; k++) {
            if (!ctx.dir_cache[k].inode) continue;

            printf("%s", ctx.dir_cache[k].name);
            if (ctx.dir_cache[k].file_type == 2) {
                inode_read(ctx.dir_cache[k].inode);
                j = 0;
                if (!strcmp(ctx.dir_cache[k].name, "..")) {
                    while (j++ < 13) printf(" ");
                    type_flag = 1;
                } else if (!strcmp(ctx.dir_cache[k].name, ".")) {
                    while (j++ < 14) printf(" ");
                    type_flag = 0;
                } else {
                    while (j++ < 15 - ctx.dir_cache[k].name_len)
                        printf(" ");
                    type_flag = 2;
                }
                printf("<DIR>          ");
                /* ---- 权限列 (9 位 rwxrwxrwx) ---- */
                {
                    char mode_str[10];
                    format_mode_str(mode_str, ctx.inode_cache.i_mode);
                    printf("%s ", mode_str);
                }
                /* ---- 物理地址列 ---- */
                format_block_str(block_str, sizeof(block_str),
                                 ctx.inode_cache.i_block,
                                 ctx.inode_cache.i_blocks);
                blk_len = (int)strlen(block_str);
                j = 0;
                while (j++ < 18 - blk_len) printf(" ");
                printf("%s", block_str);

                /* ---- 修改时间列 ---- */
                format_time_str(time_str, sizeof(time_str),
                                ctx.inode_cache.i_mtime);
                time_len = (int)strlen(time_str);
                j = 0;
                while (j++ < 16 - time_len) printf(" ");
                printf("%s", time_str);

                if (type_flag != 2)
                    printf("          ----");
                else
                    printf("          %4ld bytes", ctx.inode_cache.i_size);
            } else if (ctx.dir_cache[k].file_type == 1) {
                inode_read(ctx.dir_cache[k].inode);
                j = 0;
                while (j++ < 15 - ctx.dir_cache[k].name_len) printf(" ");
                printf("<FILE>         ");
                /* ---- 权限列 (9 位 rwxrwxrwx) ---- */
                {
                    char mode_str[10];
                    format_mode_str(mode_str, ctx.inode_cache.i_mode);
                    printf("%s ", mode_str);
                }
                /* ---- 物理地址列 ---- */
                format_block_str(block_str, sizeof(block_str),
                                 ctx.inode_cache.i_block,
                                 ctx.inode_cache.i_blocks);
                blk_len = (int)strlen(block_str);
                j = 0;
                while (j++ < 18 - blk_len) printf(" ");
                printf("%s", block_str);

                /* ---- 修改时间列 ---- */
                format_time_str(time_str, sizeof(time_str),
                                ctx.inode_cache.i_mtime);
                time_len = (int)strlen(time_str);
                j = 0;
                while (j++ < 16 - time_len) printf(" ");
                printf("%s", time_str);

                printf("          %4ld bytes", ctx.inode_cache.i_size);
            }
            printf("\n");
        }
        inode_read(ctx.current_dir);
    }
}

/* ================================================================
 * 用户命令：cd
 * ================================================================ */

void cd(char tmp[9])
{
    unsigned short i, j, k;

    if (dir_lookup(tmp, 2, &i, &j, &k)) {
        ctx.current_dir = i;
        if (!strcmp(tmp, "..") && ctx.dir_cache[k - 1].name_len) {
            ctx.current_path[strlen(ctx.current_path)
                - ctx.dir_cache[k - 1].name_len - 1] = '\0';
            ctx.current_dirlen = ctx.dir_cache[k].name_len;
        } else if (!strcmp(tmp, ".")) {
            return;
        } else if (strcmp(tmp, "..")) {
            ctx.current_dirlen = strlen(tmp);
            strcat(ctx.current_path, tmp);
            strcat(ctx.current_path, "/");
        }
    } else {
        printf("The directory %s not exists!\n", tmp);
    }
}
