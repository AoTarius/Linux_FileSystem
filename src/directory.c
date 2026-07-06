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

        ctx.inode_cache.i_mode = 01006;
    } else {  /* 普通文件 */
        ctx.inode_cache.i_size = 0;
        ctx.inode_cache.i_blocks = 0;
        ctx.inode_cache.i_mode = 0407;
    }
    /* 设置文件所有者信息 */
    ctx.inode_cache.i_uid = ctx.current_uid;
    ctx.inode_cache.i_gid = ctx.current_gid;
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

void dir_list(void)
{
    unsigned short i, j, k, type_flag;

    printf("items          type           mode           size\n");
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
                switch (ctx.inode_cache.i_mode & 7) {
                    case 1: printf("____x"); break;
                    case 2: printf("__w__"); break;
                    case 3: printf("__w_x"); break;
                    case 4: printf("r____"); break;
                    case 5: printf("r___x"); break;
                    case 6: printf("r_w__"); break;
                    case 7: printf("r_w_x"); break;
                }
                if (type_flag != 2)
                    printf("          ----");
                else
                    printf("          %4ld bytes", ctx.inode_cache.i_size);
            } else if (ctx.dir_cache[k].file_type == 1) {
                inode_read(ctx.dir_cache[k].inode);
                j = 0;
                while (j++ < 15 - ctx.dir_cache[k].name_len) printf(" ");
                printf("<FILE>         ");
                switch (ctx.inode_cache.i_mode & 7) {
                    case 1: printf("____x"); break;
                    case 2: printf("__w__"); break;
                    case 3: printf("__w_x"); break;
                    case 4: printf("r____"); break;
                    case 5: printf("r___x"); break;
                    case 6: printf("r_w__"); break;
                    case 7: printf("r_w_x"); break;
                }
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
