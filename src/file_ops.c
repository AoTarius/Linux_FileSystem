/*
 * file_ops.c — 层 2-3：文件操作 & 创建/删除/打开/关闭/读写
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "fs_context.h"
#include "ext2_constants.h"
#include "disk_io.h"
#include "bitmap.h"
#include "directory.h"
#include "file_ops.h"

/*
 * 权限检查 — 基于 uid/gid 的 user/group/other 三级判断。
 * root (uid==0) 绕过所有权限检查。
 * 返回 1=允许, 0=拒绝。
 */
static int check_read_perm(unsigned short mode,
                           unsigned short file_uid, unsigned short file_gid)
{
    if (ctx.current_uid == 0) return 1;  /* root 可读一切 */
    if (ctx.current_uid == file_uid)
        return (mode & S_IRUSR) ? 1 : 0;
    if (ctx.current_gid == file_gid)
        return (mode & S_IRGRP) ? 1 : 0;
    return (mode & S_IROTH) ? 1 : 0;
}

static int check_write_perm(unsigned short mode,
                            unsigned short file_uid, unsigned short file_gid)
{
    if (ctx.current_uid == 0) return 1;  /* root 可写一切 */
    if (ctx.current_uid == file_uid)
        return (mode & S_IWUSR) ? 1 : 0;
    if (ctx.current_gid == file_gid)
        return (mode & S_IWGRP) ? 1 : 0;
    return (mode & S_IWOTH) ? 1 : 0;
}

/* ---- 创建文件/目录（统一入口） ---- */

void file_create(const char *name, int type)
{
    unsigned short tmpno, i, j, k, flag;

    inode_read(ctx.current_dir);
    if (!dir_lookup(name, type, &i, &j, &k)) {
        if (ctx.inode_cache.i_size == 4096) {
            printf("Directory has no room to be alloced!\n");
            return;
        }
        flag = 1;
        if (ctx.inode_cache.i_size != ctx.inode_cache.i_blocks * 512) {
            i = 0;
            while (flag && i < ctx.inode_cache.i_blocks) {
                dir_read(ctx.inode_cache.i_block[i]);
                j = 0;
                while (j < 32) {
                    if (ctx.dir_cache[j].inode == 0) {
                        flag = 0;
                        break;
                    }
                    j++;
                }
                i++;
            }
            tmpno = ctx.dir_cache[j].inode = ialloc();
            ctx.dir_cache[j].name_len = strlen(name);
            ctx.dir_cache[j].file_type = type;
            strcpy(ctx.dir_cache[j].name, name);
            dir_write(ctx.inode_cache.i_block[i - 1]);
        } else {
            ctx.inode_cache.i_block[ctx.inode_cache.i_blocks] = balloc();
            ctx.inode_cache.i_blocks++;
            dir_read(ctx.inode_cache.i_block[ctx.inode_cache.i_blocks - 1]);
            tmpno = ctx.dir_cache[0].inode = ialloc();
            ctx.dir_cache[0].name_len = strlen(name);
            ctx.dir_cache[0].file_type = type;
            strcpy(ctx.dir_cache[0].name, name);
            for (flag = 1; flag < 32; flag++)
                ctx.dir_cache[flag].inode = 0;
            dir_write(ctx.inode_cache.i_block[ctx.inode_cache.i_blocks - 1]);
        }
        ctx.inode_cache.i_size += 16;
        /* 目录内容变更 — 更新目录的 mtime / ctime */
        {
            time_t now = time(NULL);
            ctx.inode_cache.i_mtime = (unsigned long)now;
            ctx.inode_cache.i_ctime = (unsigned long)now;
        }
        inode_write(ctx.current_dir);
        dir_entry_init(tmpno, strlen(name), type);
    } else {
        printf("%s has already existed!\n", type == 2 ? "Directory" : "File");
    }
}

/* ---- 删除文件 ---- */

void file_delete(const char *name)
{
    unsigned short i, j, k, m, n, flag;

    m = 0;
    flag = (unsigned short)dir_lookup(name, 1, &i, &j, &k);
    if (flag) {
        /* 清除打开表 */
        flag = 0;
        while (ctx.fopen_table[flag] != ctx.dir_cache[k].inode && flag < 16)
            flag++;
        if (flag < 16)
            ctx.fopen_table[flag] = 0;

        inode_read(i);
        while (m < ctx.inode_cache.i_blocks)
            bfree(ctx.inode_cache.i_block[m++]);
        ctx.inode_cache.i_blocks = 0;
        ctx.inode_cache.i_size = 0;
        ctx.inode_cache.i_dtime = (unsigned long)time(NULL);
        inode_write(i);
        ifree(i);

        /* 更新父目录 */
        inode_read(ctx.current_dir);
        dir_read(ctx.inode_cache.i_block[j]);
        ctx.dir_cache[k].inode = 0;
        dir_write(ctx.inode_cache.i_block[j]);
        ctx.inode_cache.i_size -= 16;

        /* 压缩因删除而全空的数据块 */
        m = 1;
        while (m < ctx.inode_cache.i_blocks) {
            flag = n = 0;
            dir_read(ctx.inode_cache.i_block[m]);
            while (n < 32) {
                if (!ctx.dir_cache[n].inode) flag++;
                n++;
            }
            if (flag == 32) {
                bfree(ctx.inode_cache.i_block[m]);
                ctx.inode_cache.i_blocks--;
                while (m < ctx.inode_cache.i_blocks) {
                    ctx.inode_cache.i_block[m] = ctx.inode_cache.i_block[m + 1];
                    m++;
                }
            }
        }
        /* 目录内容变更 — 更新目录的 mtime / ctime */
        {
            time_t now = time(NULL);
            ctx.inode_cache.i_mtime = (unsigned long)now;
            ctx.inode_cache.i_ctime = (unsigned long)now;
        }

        inode_write(ctx.current_dir);
    } else {
        printf("The file %s not exists!\n", name);
    }
}

/* ---- 打开文件 ---- */

void file_open(const char *name)
{
    unsigned short i, j, k, flag;

    if (dir_lookup(name, 1, &i, &j, &k)) {
        if (file_is_open(ctx.dir_cache[k].inode)) {
            printf("The file %s has opened!\n", name);
        } else {
            flag = 0;
            while (ctx.fopen_table[flag]) flag++;
            ctx.fopen_table[flag] = ctx.dir_cache[k].inode;
            printf("File %s opened!\n", name);
        }
    } else {
        printf("The file %s does not exist!\n", name);
    }
}

/* ---- 关闭文件 ---- */

void file_close(const char *name)
{
    unsigned short i, j, k, flag;

    if (dir_lookup(name, 1, &i, &j, &k)) {
        if (file_is_open(ctx.dir_cache[k].inode)) {
            flag = 0;
            while (ctx.fopen_table[flag] != ctx.dir_cache[k].inode) flag++;
            ctx.fopen_table[flag] = 0;
            printf("File %s closed!\n", name);
        } else {
            printf("The file %s has not been opened!\n", name);
        }
    } else {
        printf("The file %s does not exist!\n", name);
    }
}

/* ---- 读取文件 ---- */

void file_read(const char *name)
{
    unsigned short flag, i, j, k, t;

    if (dir_lookup(name, 1, &i, &j, &k)) {
        if (file_is_open(ctx.dir_cache[k].inode)) {
            inode_read(ctx.dir_cache[k].inode);
            if (!check_read_perm(ctx.inode_cache.i_mode,
                                ctx.inode_cache.i_uid,
                                ctx.inode_cache.i_gid)) {
                printf("Permission denied: cannot read %s\n", name);
                return;
            }
            for (flag = 0; flag < ctx.inode_cache.i_blocks; flag++) {
                data_read(ctx.inode_cache.i_block[flag]);
                unsigned short bytes = ctx.inode_cache.i_size - flag * 512;
                if (bytes > 512) bytes = 512;
                for (t = 0; t < bytes; t++)
                    printf("%c", ctx.data_buf[t]);
            }
            if (flag == 0)
                printf("The file %s is empty!\n", name);
            else
                printf("\n");

            /* 更新访问时间 */
            ctx.inode_cache.i_atime = (unsigned long)time(NULL);
            inode_write(ctx.dir_cache[k].inode);
        } else {
            printf("The file %s has not been opened!\n", name);
        }
    } else {
        printf("The file %s not exists!\n", name);
    }
}

/* ---- 写入文件（覆盖写） ---- */

void file_write(const char *name)
{
    unsigned short i, j, k, size = 0, need_blocks, length;

    if (dir_lookup(name, 1, &i, &j, &k)) {
        if (file_is_open(ctx.dir_cache[k].inode)) {
            inode_read(ctx.dir_cache[k].inode);
            if (!check_write_perm(ctx.inode_cache.i_mode,
                                 ctx.inode_cache.i_uid,
                                 ctx.inode_cache.i_gid)) {
                printf("Permission denied: cannot write %s\n", name);
                return;
            }
            fflush(stdin);
            while (1) {
                ctx.write_buf[size] = (char)getchar();
                if (ctx.write_buf[size] == '#') {
                    ctx.write_buf[size] = '\0';
                    break;
                }
                if (size >= 4095) {
                    printf("Sorry,the max size of a file is 4KB!\n");
                    break;
                }
                size++;
            }
            length = (size >= 4095) ? 4096 : (unsigned short)strlen(ctx.write_buf);
            need_blocks = length / 512;
            if (length % 512) need_blocks++;

            if (need_blocks < 9) {
                if (ctx.inode_cache.i_blocks <= need_blocks) {
                    while (ctx.inode_cache.i_blocks < need_blocks) {
                        ctx.inode_cache.i_block[ctx.inode_cache.i_blocks] = balloc();
                        ctx.inode_cache.i_blocks++;
                    }
                } else {
                    while (ctx.inode_cache.i_blocks > need_blocks) {
                        bfree(ctx.inode_cache.i_block[ctx.inode_cache.i_blocks - 1]);
                        ctx.inode_cache.i_blocks--;
                    }
                }
                j = 0;
                while (j < need_blocks) {
                    if (j != need_blocks - 1) {
                        data_read(ctx.inode_cache.i_block[j]);
                        memcpy(ctx.data_buf, ctx.write_buf + j * BLOCK_SIZE, BLOCK_SIZE);
                        data_write(ctx.inode_cache.i_block[j]);
                    } else {
                        data_read(ctx.inode_cache.i_block[j]);
                        memcpy(ctx.data_buf, ctx.write_buf + j * BLOCK_SIZE,
                               length - j * BLOCK_SIZE);
                        ctx.inode_cache.i_size = length;
                        data_write(ctx.inode_cache.i_block[j]);
                    }
                    j++;
                }
                /* 更新修改时间和 inode 变更时间 */
                {
                    time_t now = time(NULL);
                    ctx.inode_cache.i_mtime = (unsigned long)now;
                    ctx.inode_cache.i_ctime = (unsigned long)now;
                }
                inode_write(ctx.dir_cache[k].inode);
            } else {
                printf("Sorry,the max size of a file is 4KB!\n");
            }
        } else {
            printf("The file %s has not opened!\n", name);
        }
    } else {
        printf("The file %s does not exist!\n", name);
    }
}

/* ================================================================
 * 删除目录（含递归删除子项）
 * 放在 file_ops.c 以避免与 directory.c 的循环依赖
 * ================================================================ */

void rmdir(char tmp[9])
{
    unsigned short i, j, k, flag;
    unsigned short m, n;

    if (!strcmp(tmp, "..") || !strcmp(tmp, ".")) {
        printf("The directory can not be deleted!\n");
        return;
    }
    if (!dir_lookup(tmp, 2, &i, &j, &k)) {
        printf("Directory to be deleted not exists!\n");
        return;
    }

    inode_read(ctx.dir_cache[k].inode);
    if (ctx.inode_cache.i_size == 32) {  /* 空目录 */
        ctx.inode_cache.i_size = 0;
        ctx.inode_cache.i_blocks = 0;
        bfree(ctx.inode_cache.i_block[0]);

        inode_read(ctx.current_dir);
        dir_read(ctx.inode_cache.i_block[j]);
        ifree(ctx.dir_cache[k].inode);
        ctx.dir_cache[k].inode = 0;
        dir_write(ctx.inode_cache.i_block[j]);
        ctx.inode_cache.i_size -= 16;

        m = 1;
        flag = 0;
        while (flag < 32 && m < ctx.inode_cache.i_blocks) {
            flag = n = 0;
            dir_read(ctx.inode_cache.i_block[m]);
            while (n < 32) {
                if (!ctx.dir_cache[n].inode) flag++;
                n++;
            }
            if (flag == 32) {
                bfree(ctx.inode_cache.i_block[m]);
                ctx.inode_cache.i_blocks--;
                while (m < ctx.inode_cache.i_blocks) {
                    ctx.inode_cache.i_block[m] = ctx.inode_cache.i_block[m + 1];
                    m++;
                }
            }
        }
        /* 目录内容变更 — 更新目录的 mtime / ctime */
        {
            time_t now = time(NULL);
            ctx.inode_cache.i_mtime = (unsigned long)now;
            ctx.inode_cache.i_ctime = (unsigned long)now;
        }

        inode_write(ctx.current_dir);
    } else {  /* 非空目录：递归删除 */
        int l;
        for (l = 0; l < (int)ctx.inode_cache.i_blocks; l++) {
            int m2;
            dir_read(ctx.inode_cache.i_block[l]);
            for (m2 = 0; m2 < 32; m2++) {
                if (!strcmp(ctx.dir_cache[m2].name, ".") ||
                    !strcmp(ctx.dir_cache[m2].name, "..") ||
                    ctx.dir_cache[m2].inode == 0)
                    continue;
                if (ctx.dir_cache[m2].file_type == 2) {
                    strcpy(ctx.current_path, tmp);
                    ctx.current_dir = i;
                    rmdir(ctx.dir_cache[m2].name);
                } else if (ctx.dir_cache[m2].file_type == 1) {
                    file_delete(ctx.dir_cache[m2].name);
                }
            }
            if (ctx.inode_cache.i_size == 32) {
                strcpy(ctx.current_path, "/");
                ctx.current_dir = 1;
                rmdir(tmp);
            }
        }
    }
}

/* ================================================================
 * 向后兼容包装（保持 main.h 中声明的旧 API 不变）
 * ================================================================ */

void cat(char tmp[9], int type)   { file_create(tmp, type); }
void mkdir(char tmp[9], int type) { file_create(tmp, type); }
void del(char tmp[9])             { file_delete(tmp); }
void open_file(char tmp[9])       { file_open(tmp); }
void close_file(char tmp[9])      { file_close(tmp); }
void read_file(char tmp[9])       { file_read(tmp); }
void write_file(char tmp[9])      { file_write(tmp); }
void ls(void)                     { dir_list(); }
