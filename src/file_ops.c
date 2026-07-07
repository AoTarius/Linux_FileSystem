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
static unsigned short get_file_block(unsigned short ino,
                                     unsigned short logical,
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

    /* 一级间接块 */
    {
        unsigned short idx = logical - DIRECT_BLOCKS;
        if (idx >= INDIRECT_PTRS) {
            /* 超出本系统支持范围 (二级/三级未实现，磁盘2MB用不到) */
            return 0;
        }

        /* 确保间接块存在 */
        if (ctx.inode_cache.i_block[SINGLE_INDIRECT] == 0) {
            if (!allocate) return 0;
            ctx.inode_cache.i_block[SINGLE_INDIRECT] = balloc();
            ctx.inode_cache.i_blocks++;
            /* 清零间接块 */
            {
                unsigned short z;
                data_read(ctx.inode_cache.i_block[SINGLE_INDIRECT]);
                for (z = 0; z < BLOCK_SIZE; z++)
                    ctx.data_buf[z] = 0;
                data_write(ctx.inode_cache.i_block[SINGLE_INDIRECT]);
            }
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
}

/*
 * 释放文件所有数据块（包括间接块）。
 * 调用前需确保 ctx.inode_cache 已载入目标 inode。
 */
static void free_file_blocks(unsigned short ino)
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

    /* 二级/三级间接块：本磁盘仅 2MB，实际不会用到，预留框架 */
    if (ctx.inode_cache.i_block[DOUBLE_INDIRECT]) {
        bfree(ctx.inode_cache.i_block[DOUBLE_INDIRECT]);
        ctx.inode_cache.i_block[DOUBLE_INDIRECT] = 0;
    }
    if (ctx.inode_cache.i_block[TRIPLE_INDIRECT]) {
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
 * mv — 移动/重命名文件或目录
 *
 * 支持的操作：
 *   mv f1 f2       同目录重命名
 *   mv f1 dir1/    将 f1 移入目录 dir1（保持原名）
 *   mv dir1/f1 f2  跨目录移动 + 重命名
 *   mv dir1  dir2  移动目录（自动修正 .. 条目）
 *
 * 不支持：目录移到自身子目录下（无循环检测）
 * ================================================================ */

void mv(const char *src, const char *dst)
{
    char src_buf[256], dst_buf[256];
    unsigned short saved_dir, src_parent_ino;
    char saved_path[256];
    unsigned short src_ino, src_type, src_block, src_entry;
    const char *src_name;

    /* ---- 保存当前状态（最后恢复） ---- */
    saved_dir = ctx.current_dir;
    strcpy(saved_path, ctx.current_path);

    /* ============================================================
     * 阶段 1：定位并移除源条目
     * ============================================================ */
    strcpy(src_buf, src);
    {
        char *slash = strrchr(src_buf, '/');
        if (slash) {
            *slash = '\0';
            if (src_buf[0] == '\0') {
                ctx.current_dir = 1;
            } else if (dir_navigate(src_buf) != 0) {
                printf("mv: cannot stat '%s': No such file or directory\n", src);
                goto restore;
            }
            src_name = slash + 1;
        } else {
            src_name = src_buf;
        }
    }

    /* 在源父目录中查找 */
    if (dir_lookup(src_name, 2, &src_ino, &src_block, &src_entry)) {
        src_type = 2;                       /* 目录 */
    } else if (dir_lookup(src_name, 1, &src_ino, &src_block, &src_entry)) {
        src_type = 1;                       /* 普通文件 */
    } else {
        printf("mv: cannot stat '%s': No such file or directory\n", src);
        goto restore;
    }

    if (!strcmp(src_name, ".") || !strcmp(src_name, "..")) {
        printf("mv: cannot move '%s'\n", src_name);
        goto restore;
    }

    src_parent_ino = ctx.current_dir;       /* 记录源父目录，用于 .. 更新判断 */

    /* 从源父目录中移除条目 */
    {
        ctx.dir_cache[src_entry].inode = 0;
        dir_write(ctx.inode_cache.i_block[src_block]);

        ctx.inode_cache.i_size -= 16;

        /* 压缩因删除而全空的数据块（避免碎片） */
        {
            unsigned short m = 1;
            while (m < ctx.inode_cache.i_blocks) {
                unsigned short n, empty = 0;
                dir_read(ctx.inode_cache.i_block[m]);
                for (n = 0; n < 32; n++) {
                    if (!ctx.dir_cache[n].inode) empty++;
                }
                if (empty == 32) {
                    bfree(ctx.inode_cache.i_block[m]);
                    ctx.inode_cache.i_blocks--;
                    while (m < ctx.inode_cache.i_blocks) {
                        ctx.inode_cache.i_block[m] = ctx.inode_cache.i_block[m + 1];
                        m++;
                    }
                } else {
                    m++;
                }
            }
        }

        {
            time_t now = time(NULL);
            ctx.inode_cache.i_mtime = (unsigned int)now;
            ctx.inode_cache.i_ctime = (unsigned int)now;
        }
        inode_write(ctx.current_dir);
    }

    /* ============================================================
     * 阶段 2：确定目标目录与文件名
     * ============================================================ */

    /* 先恢复到原始目录 — 目标路径从用户视角解析 */
    ctx.current_dir = saved_dir;
    strcpy(ctx.current_path, saved_path);

    strcpy(dst_buf, dst);
    {
        const char *final_name;
        const char *name_part;
        unsigned short dst_parent_ino;

        {
            char *slash = strrchr(dst_buf, '/');
            if (slash) {
                name_part = slash + 1;      /* 先保存 basename，再截断 */
                *slash = '\0';
                if (dst_buf[0] == '\0') {
                    ctx.current_dir = 1;
                } else if (dir_navigate(dst_buf) != 0) {
                    printf("mv: cannot create '%s': "
                           "No such file or directory\n", dst);
                    goto restore;
                }
            } else {
                name_part = dst_buf;
            }
        }

        /* 判断目标名称 */
        if (name_part[0] == '\0') {
            /* 路径以 / 结尾（如 mv f1 dir1/）→ 直接移入，保持原名 */
            final_name = src_name;
        } else {
            unsigned short d_ino, d_blk, d_ent;
            if (dir_lookup(name_part, 2, &d_ino, &d_blk, &d_ent)) {
                /* 目标是目录 → 将源移入其中，保持原名 */
                ctx.current_dir = d_ino;
                final_name = src_name;
            } else {
                /* 目标不是目录 → 作为新文件名 */
                final_name = name_part;
            }
        }

        dst_parent_ino = ctx.current_dir;

        /* ============================================================
         * 阶段 3：处理目标冲突 & 写入新条目
         * ============================================================ */

        /* 若目标位置已存在同名文件：删除它（覆盖） */
        {
            unsigned short ov_ino, ov_blk, ov_ent;
            if (dir_lookup(final_name, 1, &ov_ino, &ov_blk, &ov_ent)) {
                inode_read(ov_ino);
                free_file_blocks(ov_ino);
                ifree(ov_ino);

                /* 从父目录中删除该条目 */
                inode_read(ctx.current_dir);
                dir_read(ctx.inode_cache.i_block[ov_blk]);
                ctx.dir_cache[ov_ent].inode = 0;
                dir_write(ctx.inode_cache.i_block[ov_blk]);
                ctx.inode_cache.i_size -= 16;
                {
                    time_t now = time(NULL);
                    ctx.inode_cache.i_mtime = (unsigned int)now;
                    ctx.inode_cache.i_ctime = (unsigned int)now;
                }
                inode_write(ctx.current_dir);
            }
        }

        /* 检查目标名是否是已存在的目录（不能覆盖目录） */
        {
            unsigned short d_ino, d_blk, d_ent;
            if (dir_lookup(final_name, 2, &d_ino, &d_blk, &d_ent)) {
                printf("mv: cannot overwrite directory '%s'\n", final_name);
                goto restore;
            }
        }

        /* ---- 在目标目录中插入新条目 ---- */
        {
            unsigned short flag = 1, i = 0, j = 0;

            inode_read(ctx.current_dir);

            if (ctx.inode_cache.i_size != ctx.inode_cache.i_blocks * 512) {
                /* 存在未满的块 — 在其中找空槽 */
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
                i--;    /* 回退到找到的块 */
            } else {
                /* 所有块已满 — 分配新块 */
                ctx.inode_cache.i_block[ctx.inode_cache.i_blocks] = balloc();
                ctx.inode_cache.i_blocks++;
                i = ctx.inode_cache.i_blocks - 1;
                dir_read(ctx.inode_cache.i_block[i]);
                j = 0;
                for (flag = 1; flag < 32; flag++)
                    ctx.dir_cache[flag].inode = 0;
            }

            /* 填入条目 — 复用源 inode，更新名称 */
            ctx.dir_cache[j].inode     = src_ino;
            ctx.dir_cache[j].name_len  = (unsigned short)strlen(final_name);
            ctx.dir_cache[j].file_type = (char)src_type;
            strcpy(ctx.dir_cache[j].name, final_name);
            dir_write(ctx.inode_cache.i_block[i]);

            ctx.inode_cache.i_size += 16;
            {
                time_t now = time(NULL);
                ctx.inode_cache.i_mtime = (unsigned int)now;
                ctx.inode_cache.i_ctime = (unsigned int)now;
            }
            inode_write(ctx.current_dir);

            /* ---- 目录跨父移动：修正 .. 条目 ---- */
            if (src_type == 2 && src_parent_ino != dst_parent_ino) {
                inode_read(src_ino);
                dir_read(ctx.inode_cache.i_block[0]);
                ctx.dir_cache[1].inode = dst_parent_ino;
                dir_write(ctx.inode_cache.i_block[0]);
            }
        }
    }

restore:
    ctx.current_dir = saved_dir;
    strcpy(ctx.current_path, saved_path);
}

/* ================================================================
 * cp — 复制文件
 *
 * 行为：
 *   cp f1 f2       复制为 f2（若 f2 已存在则覆盖）
 *   cp f1 dir1/    复制到目录 dir1 内（保持原名）
 *   cp dir1/f1 f2  跨目录复制
 *
 * 限制：
 *   - 仅支持普通文件（目录需 -r，未实现）
 *   - 单文件最大 4KB（受限于 8 个直接块）
 * ================================================================ */

void cp(const char *src, const char *dst)
{
    char src_buf[256], dst_buf[256];
    unsigned short saved_dir;
    char saved_path[256];
    unsigned short src_ino, src_block, src_entry;
    unsigned short src_mode;
    unsigned int src_size;
    const char *src_name;                    /* 源文件名，供阶段 2 复用 */

    /* ---- 保存当前状态 ---- */
    saved_dir = ctx.current_dir;
    strcpy(saved_path, ctx.current_path);

    /* ============================================================
     * 阶段 1：读取源文件
     * ============================================================ */
    strcpy(src_buf, src);
    {
        char *slash = strrchr(src_buf, '/');
        if (slash) {
            *slash = '\0';
            if (src_buf[0] == '\0') {
                ctx.current_dir = 1;
            } else if (dir_navigate(src_buf) != 0) {
                printf("cp: cannot stat '%s': No such file or directory\n", src);
                goto restore;
            }
            src_name = slash + 1;
        } else {
            src_name = src_buf;
        }

        /* 查找源文件（仅普通文件，不支持目录） */
        if (!dir_lookup(src_name, 1, &src_ino, &src_block, &src_entry)) {
            /* 检查是否是目录 */
            if (dir_lookup(src_name, 2, &src_ino, &src_block, &src_entry)) {
                printf("cp: -r not specified; omitting directory '%s'\n",
                       src_name);
            } else {
                printf("cp: cannot stat '%s': No such file or directory\n",
                       src);
            }
            goto restore;
        }
    }

    /* 读取源文件全部数据到 write_buf */
    {
        inode_read(src_ino);
        src_mode   = ctx.inode_cache.i_mode;
        src_size   = ctx.inode_cache.i_size;

        if (src_size > 0) {
            unsigned short b, total;
            total = (unsigned short)((src_size + BLOCK_SIZE - 1) / BLOCK_SIZE);
            for (b = 0; b < total; b++) {
                unsigned short phys = get_file_block(src_ino, b, 0);
                data_read(phys);
                memcpy(ctx.write_buf + b * BLOCK_SIZE,
                       ctx.data_buf, BLOCK_SIZE);
            }
        }
    }

    /* ============================================================
     * 阶段 2：定位目标目录与文件名
     * ============================================================ */

    /* 恢复到原始目录 */
    ctx.current_dir = saved_dir;
    strcpy(ctx.current_path, saved_path);

    strcpy(dst_buf, dst);
    {
        const char *final_name;
        const char *name_part;

        {
            char *slash = strrchr(dst_buf, '/');
            if (slash) {
                name_part = slash + 1;      /* 先保存，再截断 */
                *slash = '\0';
                if (dst_buf[0] == '\0') {
                    ctx.current_dir = 1;
                } else if (dir_navigate(dst_buf) != 0) {
                    printf("cp: cannot create '%s': "
                           "No such file or directory\n", dst);
                    goto restore;
                }
            } else {
                name_part = dst_buf;
            }
        }

        /* 确定最终文件名 */
        if (name_part[0] == '\0') {
            /* 路径以 / 结尾 → 移入目录，保持源名 */
            final_name = src_name;
        } else {
            unsigned short d_ino, d_blk, d_ent;
            if (dir_lookup(name_part, 2, &d_ino, &d_blk, &d_ent)) {
                /* 目标是目录 → 移入，保持源名 */
                ctx.current_dir = d_ino;
                final_name = src_name;
            } else {
                final_name = name_part;
            }
        }

        /* ============================================================
         * 阶段 3：冲突处理 & 创建副本
         * ============================================================ */

        /* 覆盖已存在的文件 */
        {
            unsigned short ov_ino, ov_blk, ov_ent;
            if (dir_lookup(final_name, 1, &ov_ino, &ov_blk, &ov_ent)) {
                inode_read(ov_ino);
                free_file_blocks(ov_ino);
                ifree(ov_ino);

                inode_read(ctx.current_dir);
                dir_read(ctx.inode_cache.i_block[ov_blk]);
                ctx.dir_cache[ov_ent].inode = 0;
                dir_write(ctx.inode_cache.i_block[ov_blk]);
                ctx.inode_cache.i_size -= 16;
                {
                    time_t now = time(NULL);
                    ctx.inode_cache.i_mtime = (unsigned int)now;
                    ctx.inode_cache.i_ctime = (unsigned int)now;
                }
                inode_write(ctx.current_dir);
            }
        }

        /* 不能覆盖目录 */
        {
            unsigned short d_ino, d_blk, d_ent;
            if (dir_lookup(final_name, 2, &d_ino, &d_blk, &d_ent)) {
                printf("cp: cannot overwrite directory '%s' "
                       "with non-directory\n", final_name);
                goto restore;
            }
        }

        /* ---- 创建目标文件（空文件） ---- */
        file_create(final_name, 1);

        /* ---- 找到新文件 inode，写入数据 ---- */
        {
            unsigned short new_ino, new_blk, new_ent;
            if (!dir_lookup(final_name, 1, &new_ino, &new_blk, &new_ent)) {
                printf("cp: internal error: file not found after create\n");
                goto restore;
            }

            inode_read(new_ino);

            /* 设置权限（从源复制 mode，uid/gid 使用当前用户） */
            ctx.inode_cache.i_mode = src_mode;
            ctx.inode_cache.i_uid  = ctx.current_uid;
            ctx.inode_cache.i_gid  = ctx.current_gid;

            if (src_size > 0) {
                unsigned short need_blocks, b;
                unsigned int remaining;

                need_blocks = (unsigned short)((src_size + BLOCK_SIZE - 1)
                                               / BLOCK_SIZE);
                ctx.inode_cache.i_size = src_size;

                remaining = src_size;
                for (b = 0; b < need_blocks; b++) {
                    unsigned short phys = get_file_block(new_ino, b, 1);
                    unsigned short chunk = (remaining > BLOCK_SIZE)
                                           ? BLOCK_SIZE
                                           : (unsigned short)remaining;
                    data_read(phys);
                    memcpy(ctx.data_buf,
                           ctx.write_buf + b * BLOCK_SIZE, chunk);
                    data_write(phys);
                    remaining -= chunk;
                }
            } else {
                ctx.inode_cache.i_blocks = 0;
                ctx.inode_cache.i_size   = 0;
            }

            /* 时间戳 — 新文件使用当前时间 */
            {
                time_t now = time(NULL);
                ctx.inode_cache.i_atime = (unsigned int)now;
                ctx.inode_cache.i_mtime = (unsigned int)now;
                ctx.inode_cache.i_ctime = (unsigned int)now;
            }

            inode_write(new_ino);
        }
    }

restore:
    ctx.current_dir = saved_dir;
    strcpy(ctx.current_path, saved_path);
}

/* ================================================================
 * 向后兼容包装（保持 main.h 中声明的旧 API 不变）
 *
 * mkdir / cat 支持多级路径：mkdir t3/t4 会先进入 t3 再创建 t4。
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

void cat(const char *tmp, int type)       { file_create_path(tmp, type); }
void mkdir(const char *tmp, int type)     { file_create_path(tmp, type); }
void del(const char *tmp)                 { file_delete(tmp); }
void open_file(const char *tmp)           { file_open(tmp); }
void close_file(const char *tmp)          { file_close(tmp); }
void read_file(const char *tmp)           { file_read(tmp); }
void write_file(const char *tmp)          { file_write(tmp); }
void ls(void)                             { dir_list(); }
