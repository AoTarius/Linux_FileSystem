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

/* ================================================================
 * 间接块寻址 — 逻辑块号 → 物理块号
 * ================================================================ */

/*
 * 将文件的逻辑块号映射为物理块号。
 * 若 allocate=1 且逻辑块超出已分配范围，则自动分配间接块。
 * 调用前需确保 ctx.inode_cache 已载入目标 inode。
 */
unsigned short get_file_block(unsigned short ino,
                                     unsigned int logical,
                                     int allocate)
{
    unsigned short phys;

    /* 直接块 */
    if (logical < DIRECT_BLOCKS) {
        if (allocate && ctx.inode_cache.i_block[logical] == 0) {
            ctx.inode_cache.i_block[logical] = balloc();
            ctx.inode_cache.i_blocks++;
            inode_write(ino);
        }
        return ctx.inode_cache.i_block[logical];
    }

    /* ---- 一级间接块: i_block[12], 可寻址 256 个数据块 ---- */
    if (logical < DIRECT_BLOCKS + INDIRECT_PTRS) {
        unsigned short idx = logical - DIRECT_BLOCKS;

        /* 确保间接块存在 */
        if (ctx.inode_cache.i_block[SINGLE_INDIRECT] == 0) {
            if (!allocate) return 0;
            ctx.inode_cache.i_block[SINGLE_INDIRECT] = balloc();
            ctx.inode_cache.i_blocks++;
            /* 清零间接块 */
            data_read(ctx.inode_cache.i_block[SINGLE_INDIRECT]);
            memset(ctx.data_buf, 0, BLOCK_SIZE);
            data_write(ctx.inode_cache.i_block[SINGLE_INDIRECT]);
            inode_write(ino);
        }

        /* 从间接块中读取指针 */
        data_read(ctx.inode_cache.i_block[SINGLE_INDIRECT]);
        phys = ((unsigned short *)ctx.data_buf)[idx];

        if (phys == 0 && allocate) {
            phys = balloc();
            ((unsigned short *)ctx.data_buf)[idx] = phys;
            data_write(ctx.inode_cache.i_block[SINGLE_INDIRECT]);
            ctx.inode_cache.i_blocks++;
            inode_write(ino);
        }

        return phys;
    }

    /* ---- 二级间接块: i_block[13], 可寻址 256 个一级间接块 (256² 数据块) ---- */
    if (logical < DIRECT_BLOCKS + INDIRECT_PTRS + INDIRECT_PTRS * INDIRECT_PTRS) {
        unsigned short idx = logical - DIRECT_BLOCKS - INDIRECT_PTRS;
        unsigned short dbl_idx = idx / INDIRECT_PTRS;  /* 目标一级间接块索引 */
        unsigned short sgl_idx = idx % INDIRECT_PTRS;  /* 一级间接块内偏移 */
        unsigned short dbl_ptrs[INDIRECT_PTRS];        /* 栈上保存二级间接块 */

        /* 确保二级间接块存在 */
        if (ctx.inode_cache.i_block[DOUBLE_INDIRECT] == 0) {
            if (!allocate) return 0;
            ctx.inode_cache.i_block[DOUBLE_INDIRECT] = balloc();
            ctx.inode_cache.i_blocks++;
            data_read(ctx.inode_cache.i_block[DOUBLE_INDIRECT]);
            memset(ctx.data_buf, 0, BLOCK_SIZE);
            data_write(ctx.inode_cache.i_block[DOUBLE_INDIRECT]);
            inode_write(ino);
        }

        /* 读取二级间接块并保存到栈（后续 data_read 会覆盖 ctx.data_buf） */
        data_read(ctx.inode_cache.i_block[DOUBLE_INDIRECT]);
        memcpy(dbl_ptrs, ctx.data_buf, BLOCK_SIZE);

        /* 确保目标一级间接块存在 */
        if (dbl_ptrs[dbl_idx] == 0) {
            if (!allocate) return 0;
            dbl_ptrs[dbl_idx] = balloc();
            ctx.inode_cache.i_blocks++;
            /* 清零新一级间接块 */
            data_read(dbl_ptrs[dbl_idx]);
            memset(ctx.data_buf, 0, BLOCK_SIZE);
            data_write(dbl_ptrs[dbl_idx]);
            /* 写回更新的二级间接块 */
            memcpy(ctx.data_buf, dbl_ptrs, BLOCK_SIZE);
            data_write(ctx.inode_cache.i_block[DOUBLE_INDIRECT]);
            inode_write(ino);
        }

        /* 从目标一级间接块中读取数据块指针 */
        data_read(dbl_ptrs[dbl_idx]);
        phys = ((unsigned short *)ctx.data_buf)[sgl_idx];

        if (phys == 0 && allocate) {
            phys = balloc();
            ((unsigned short *)ctx.data_buf)[sgl_idx] = phys;
            data_write(dbl_ptrs[dbl_idx]);
            ctx.inode_cache.i_blocks++;
            inode_write(ino);
        }

        return phys;
    }

    /* ---- 三级间接块: i_block[14], 可寻址 256 个二级间接块 (256³ 数据块) ---- */
    {
        unsigned short idx = logical - DIRECT_BLOCKS - INDIRECT_PTRS
                             - INDIRECT_PTRS * INDIRECT_PTRS;
        unsigned short tpl_idx = idx / (INDIRECT_PTRS * INDIRECT_PTRS);
        unsigned short dbl_idx = (idx / INDIRECT_PTRS) % INDIRECT_PTRS;
        unsigned short sgl_idx = idx % INDIRECT_PTRS;
        unsigned short tpl_ptrs[INDIRECT_PTRS];  /* 保存三级间接块 */
        unsigned short dbl_ptrs[INDIRECT_PTRS];  /* 保存二级间接块 */

        /* 确保三级间接块存在 */
        if (ctx.inode_cache.i_block[TRIPLE_INDIRECT] == 0) {
            if (!allocate) return 0;
            ctx.inode_cache.i_block[TRIPLE_INDIRECT] = balloc();
            ctx.inode_cache.i_blocks++;
            data_read(ctx.inode_cache.i_block[TRIPLE_INDIRECT]);
            memset(ctx.data_buf, 0, BLOCK_SIZE);
            data_write(ctx.inode_cache.i_block[TRIPLE_INDIRECT]);
            inode_write(ino);
        }

        /* 读取三级间接块并保存 */
        data_read(ctx.inode_cache.i_block[TRIPLE_INDIRECT]);
        memcpy(tpl_ptrs, ctx.data_buf, BLOCK_SIZE);

        /* 确保目标二级间接块存在 */
        if (tpl_ptrs[tpl_idx] == 0) {
            if (!allocate) return 0;
            tpl_ptrs[tpl_idx] = balloc();
            ctx.inode_cache.i_blocks++;
            data_read(tpl_ptrs[tpl_idx]);
            memset(ctx.data_buf, 0, BLOCK_SIZE);
            data_write(tpl_ptrs[tpl_idx]);
            /* 写回更新的三级间接块 */
            memcpy(ctx.data_buf, tpl_ptrs, BLOCK_SIZE);
            data_write(ctx.inode_cache.i_block[TRIPLE_INDIRECT]);
            inode_write(ino);
        }

        /* 读取目标二级间接块并保存 */
        data_read(tpl_ptrs[tpl_idx]);
        memcpy(dbl_ptrs, ctx.data_buf, BLOCK_SIZE);

        /* 确保目标一级间接块存在 */
        if (dbl_ptrs[dbl_idx] == 0) {
            if (!allocate) return 0;
            dbl_ptrs[dbl_idx] = balloc();
            ctx.inode_cache.i_blocks++;
            data_read(dbl_ptrs[dbl_idx]);
            memset(ctx.data_buf, 0, BLOCK_SIZE);
            data_write(dbl_ptrs[dbl_idx]);
            /* 写回更新的二级间接块 */
            memcpy(ctx.data_buf, dbl_ptrs, BLOCK_SIZE);
            data_write(tpl_ptrs[tpl_idx]);
            inode_write(ino);
        }

        /* 从目标一级间接块中读取数据块指针 */
        data_read(dbl_ptrs[dbl_idx]);
        phys = ((unsigned short *)ctx.data_buf)[sgl_idx];

        if (phys == 0 && allocate) {
            phys = balloc();
            ((unsigned short *)ctx.data_buf)[sgl_idx] = phys;
            data_write(dbl_ptrs[dbl_idx]);
            ctx.inode_cache.i_blocks++;
            inode_write(ino);
        }

        return phys;
    }
}

/*
 * 释放文件所有数据块（包括间接块）。
 * 调用前需确保 ctx.inode_cache 已载入目标 inode。
 */
void free_file_blocks(unsigned short ino)
{
    unsigned short i;

    /* 释放直接块 */
    for (i = 0; i < DIRECT_BLOCKS; i++) {
        if (ctx.inode_cache.i_block[i]) {
            bfree(ctx.inode_cache.i_block[i]);
            ctx.inode_cache.i_block[i] = 0;
        }
    }

    /* 释放一级间接块及其指向的所有数据块 */
    if (ctx.inode_cache.i_block[SINGLE_INDIRECT]) {
        unsigned short *ptrs;
        data_read(ctx.inode_cache.i_block[SINGLE_INDIRECT]);
        ptrs = (unsigned short *)ctx.data_buf;
        for (i = 0; i < INDIRECT_PTRS; i++) {
            if (ptrs[i]) {
                bfree(ptrs[i]);
                ptrs[i] = 0;
            }
        }
        bfree(ctx.inode_cache.i_block[SINGLE_INDIRECT]);
        ctx.inode_cache.i_block[SINGLE_INDIRECT] = 0;
    }

    /* 释放二级间接块及其所有子树（一级间接块 → 数据块） */
    if (ctx.inode_cache.i_block[DOUBLE_INDIRECT]) {
        unsigned short j, k;
        unsigned short dbl_saved[INDIRECT_PTRS];
        data_read(ctx.inode_cache.i_block[DOUBLE_INDIRECT]);
        memcpy(dbl_saved, ctx.data_buf, BLOCK_SIZE);
        for (j = 0; j < INDIRECT_PTRS; j++) {
            if (dbl_saved[j]) {
                unsigned short *sgl_ptrs;
                data_read(dbl_saved[j]);
                sgl_ptrs = (unsigned short *)ctx.data_buf;
                for (k = 0; k < INDIRECT_PTRS; k++) {
                    if (sgl_ptrs[k]) {
                        bfree(sgl_ptrs[k]);
                    }
                }
                bfree(dbl_saved[j]);
            }
        }
        bfree(ctx.inode_cache.i_block[DOUBLE_INDIRECT]);
        ctx.inode_cache.i_block[DOUBLE_INDIRECT] = 0;
    }

    /* 释放三级间接块及其所有子树（二级间接 → 一级间接 → 数据块） */
    if (ctx.inode_cache.i_block[TRIPLE_INDIRECT]) {
        unsigned short j, k, l;
        unsigned short tpl_saved[INDIRECT_PTRS];
        data_read(ctx.inode_cache.i_block[TRIPLE_INDIRECT]);
        memcpy(tpl_saved, ctx.data_buf, BLOCK_SIZE);
        for (j = 0; j < INDIRECT_PTRS; j++) {
            if (tpl_saved[j]) {
                unsigned short dbl_saved[INDIRECT_PTRS];
                data_read(tpl_saved[j]);
                memcpy(dbl_saved, ctx.data_buf, BLOCK_SIZE);
                for (k = 0; k < INDIRECT_PTRS; k++) {
                    if (dbl_saved[k]) {
                        unsigned short *sgl_ptrs;
                        data_read(dbl_saved[k]);
                        sgl_ptrs = (unsigned short *)ctx.data_buf;
                        for (l = 0; l < INDIRECT_PTRS; l++) {
                            if (sgl_ptrs[l]) {
                                bfree(sgl_ptrs[l]);
                            }
                        }
                        bfree(dbl_saved[k]);
                    }
                }
                bfree(tpl_saved[j]);
            }
        }
        bfree(ctx.inode_cache.i_block[TRIPLE_INDIRECT]);
        ctx.inode_cache.i_block[TRIPLE_INDIRECT] = 0;
    }

    ctx.inode_cache.i_blocks = 0;
    ctx.inode_cache.i_size   = 0;
    ctx.inode_cache.i_dtime  = (unsigned int)time(NULL);
    inode_write(ino);
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
            ctx.inode_cache.i_mtime = (unsigned int)now;
            ctx.inode_cache.i_ctime = (unsigned int)now;
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
        free_file_blocks(i);
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
            ctx.inode_cache.i_mtime = (unsigned int)now;
            ctx.inode_cache.i_ctime = (unsigned int)now;
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
            {
                unsigned short total_blocks =
                    (unsigned short)((ctx.inode_cache.i_size + BLOCK_SIZE - 1)
                                     / BLOCK_SIZE);
                for (flag = 0; flag < total_blocks; flag++) {
                    unsigned short phys = get_file_block(
                        ctx.dir_cache[k].inode, flag, 0);
                    if (phys == 0) break;
                    data_read(phys);
                    unsigned short bytes =
                        (unsigned short)(ctx.inode_cache.i_size - flag * 512);
                    if (bytes > 512) bytes = 512;
                    for (t = 0; t < bytes; t++)
                        printf("%c", ctx.data_buf[t]);
                }
                if (flag == 0)
                    printf("The file %s is empty!\n", name);
                else
                    printf("\n");
            }

            /* 更新访问时间 */
            ctx.inode_cache.i_atime = (unsigned int)time(NULL);
            inode_write(ctx.dir_cache[k].inode);
        } else {
            printf("The file %s has not been opened!\n", name);
        }
    } else {
        printf("The file %s not exists!\n", name);
    }
}

/* ---- cat — 读取并打印文件全部内容（自动 open/close） ---- */

void file_cat(const char *name)
{
    unsigned short i, j, k, t, fd_idx;
    unsigned short total_blocks, block_idx;
    int need_close = 0;

    if (!dir_lookup(name, 1, &i, &j, &k)) {
        printf("cat: %s: No such file or directory\n", name);
        return;
    }

    /* 如果文件未打开，临时打开 */
    if (!file_is_open(ctx.dir_cache[k].inode)) {
        fd_idx = 0;
        while (ctx.fopen_table[fd_idx]) fd_idx++;
        ctx.fopen_table[fd_idx] = ctx.dir_cache[k].inode;
        need_close = 1;
    }

    inode_read(ctx.dir_cache[k].inode);
    if (!check_read_perm(ctx.inode_cache.i_mode,
                         ctx.inode_cache.i_uid,
                         ctx.inode_cache.i_gid)) {
        printf("cat: %s: Permission denied\n", name);
        goto cleanup;
    }

    total_blocks = (unsigned short)((ctx.inode_cache.i_size + BLOCK_SIZE - 1)
                                     / BLOCK_SIZE);
    for (block_idx = 0; block_idx < total_blocks; block_idx++) {
        unsigned short phys = get_file_block(
            ctx.dir_cache[k].inode, block_idx, 0);
        if (phys == 0) break;
        data_read(phys);
        unsigned short bytes =
            (unsigned short)(ctx.inode_cache.i_size - block_idx * 512);
        if (bytes > 512) bytes = 512;
        for (t = 0; t < bytes; t++)
            printf("%c", ctx.data_buf[t]);
    }
    if (block_idx > 0)
        printf("\n");

    /* 更新访问时间 */
    ctx.inode_cache.i_atime = (unsigned int)time(NULL);
    inode_write(ctx.dir_cache[k].inode);

cleanup:
    if (need_close) {
        fd_idx = 0;
        while (ctx.fopen_table[fd_idx] != ctx.dir_cache[k].inode) fd_idx++;
        ctx.fopen_table[fd_idx] = 0;
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
                unsigned short ino = ctx.dir_cache[k].inode;
                /* 释放多余块（如果文件之前更大） */
                {
                    unsigned short old_total =
                        (unsigned short)((ctx.inode_cache.i_size + BLOCK_SIZE - 1)
                                         / BLOCK_SIZE);
                    while (old_total > need_blocks) {
                        old_total--;
                        unsigned short phys = get_file_block(ino, old_total, 0);
                        if (phys) bfree(phys);
                    }
                }
                /* 分配/写入块 */
                j = 0;
                while (j < need_blocks) {
                    unsigned short phys = get_file_block(ino, j, 1);
                    if (j != need_blocks - 1) {
                        data_read(phys);
                        memcpy(ctx.data_buf, ctx.write_buf + j * BLOCK_SIZE,
                               BLOCK_SIZE);
                        data_write(phys);
                    } else {
                        data_read(phys);
                        memcpy(ctx.data_buf, ctx.write_buf + j * BLOCK_SIZE,
                               length - j * BLOCK_SIZE);
                        ctx.inode_cache.i_size = length;
                        data_write(phys);
                    }
                    j++;
                }
                /* 更新修改时间和 inode 变更时间 */
                {
                    time_t now = time(NULL);
                    ctx.inode_cache.i_mtime = (unsigned int)now;
                    ctx.inode_cache.i_ctime = (unsigned int)now;
                }
                inode_write(ino);
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

/* ---- 追加写入文件（保留原有内容） ---- */

void file_append(const char *name)
{
    unsigned short i, j, k, ino;
    unsigned short size = 0, new_len, old_size;
    unsigned short first_block, first_offset, buf_pos;

    if (!dir_lookup(name, 1, &i, &j, &k)) {
        printf("The file %s does not exist!\n", name);
        return;
    }
    if (!file_is_open(ctx.dir_cache[k].inode)) {
        printf("The file %s has not been opened!\n", name);
        return;
    }

    ino = ctx.dir_cache[k].inode;
    inode_read(ino);

    if (!check_write_perm(ctx.inode_cache.i_mode,
                          ctx.inode_cache.i_uid,
                          ctx.inode_cache.i_gid)) {
        printf("Permission denied: cannot write %s\n", name);
        return;
    }

    old_size = (unsigned short)ctx.inode_cache.i_size;

    /* 读入新内容 */
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
    new_len = (unsigned short)strlen(ctx.write_buf);

    /* 追加起点：旧数据末尾 */
    first_block = old_size / BLOCK_SIZE;
    first_offset = old_size % BLOCK_SIZE;

    /* 逐块写入新数据 */
    buf_pos = 0;
    j = first_block;
    while (buf_pos < new_len) {
        unsigned short phys = get_file_block(ino, j, 1);
        unsigned short copy_start = (j == first_block) ? first_offset : 0;
        unsigned short space = BLOCK_SIZE - copy_start;
        unsigned short to_copy = (new_len - buf_pos < space)
                                 ? (new_len - buf_pos) : space;

        data_read(phys);
        memcpy(ctx.data_buf + copy_start, ctx.write_buf + buf_pos, to_copy);
        data_write(phys);

        buf_pos += to_copy;
        j++;
    }

    /* 更新 inode */
    ctx.inode_cache.i_size = old_size + new_len;
    {
        time_t now = time(NULL);
        ctx.inode_cache.i_mtime = (unsigned int)now;
        ctx.inode_cache.i_ctime = (unsigned int)now;
    }
    inode_write(ino);
}

/* ================================================================
 * 删除目录（含递归删除子项）
 * 放在 file_ops.c 以避免与 directory.c 的循环依赖
 * ================================================================ */

void rmdir(const char *tmp)
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
            ctx.inode_cache.i_mtime = (unsigned int)now;
            ctx.inode_cache.i_ctime = (unsigned int)now;
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
                strcpy(ctx.current_path, "~/");
                ctx.current_dir = 1;
                rmdir(tmp);
            }
        }
    }
}


/* ================================================================
 * 向后兼容包装（保持 main.h 中声明的旧 API 不变）
 *
 * mkdir / touch 支持多级路径：mkdir t3/t4 会先进入 t3 再创建 t4。
 * ================================================================ */

/*
 * 路径感知的文件/目录创建。
 * 若 path 含 '/' 分隔符，先导航到父目录，创建完毕后再恢复原目录。
 */
static void file_create_path(const char *path, int type)
{
    char buf[256];
    char *last_slash;
    const char *name;
    unsigned short saved_dir;
    char saved_path[256];

    strcpy(buf, path);
    last_slash = strrchr(buf, '/');

    if (last_slash != NULL) {
        /* 路径含目录分隔符 — 分离父目录与文件名 */
        *last_slash = '\0';
        name = last_slash + 1;

        if (name[0] == '\0') {
            printf("Invalid path: %s\n", path);
            return;
        }

        /* 保存当前状态 */
        saved_dir = ctx.current_dir;
        strcpy(saved_path, ctx.current_path);

        /* 导航到父目录 */
        if (buf[0] == '\0') {
            /* "/name" 形式 — 在根目录创建 */
            ctx.current_dir = 1;
            strcpy(ctx.current_path, "~/");
            ctx.current_dirlen = 0;
        } else {
            if (dir_navigate(buf) != 0) {
                printf("The directory %s not exists!\n", buf);
                /* 恢复原目录 */
                ctx.current_dir = saved_dir;
                strcpy(ctx.current_path, saved_path);
                return;
            }
        }

        /* 在父目录中创建 */
        file_create(name, type);

        /* 恢复原目录 */
        ctx.current_dir = saved_dir;
        strcpy(ctx.current_path, saved_path);
    } else {
        /* 单层名称 — 在当前目录创建（原有行为） */
        file_create(path, type);
    }
}

void touch(const char *tmp, int type)     { file_create_path(tmp, type); }
void mkdir(const char *tmp, int type)     { file_create_path(tmp, type); }
void del(const char *tmp)                 { file_delete(tmp); }
void open_file(const char *tmp)           { file_open(tmp); }
void close_file(const char *tmp)          { file_close(tmp); }
void read_file(const char *tmp)           { file_read(tmp); }
void write_file(const char *tmp)          { file_write(tmp); }
void append(const char *tmp)              { file_append(tmp); }
void cat(const char *tmp)                 { file_cat(tmp); }
void ls(void)                             { dir_list(); }
