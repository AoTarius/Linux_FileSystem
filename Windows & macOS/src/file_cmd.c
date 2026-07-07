/*
 * file_cmd.c — 层 3：文件命令 (cp / mv / chmod / chown)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fs_context.h"
#include "ext2_constants.h"
#include "disk_io.h"
#include "bitmap.h"
#include "directory.h"
#include "file_ops.h"
#include "user.h"

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
 * chmod — 修改文件权限
 *
 * 用法: chmod <八进制模式> <路径>
 * 示例: chmod 755 file   chmod 644 dir1/f1
 * ================================================================ */

void chmod(const char *mode_str, const char *path)
{
    unsigned short new_mode, saved_dir, ino, blk, ent;
    char saved_path[256];
    char path_buf[256];
    const char *name;
    char *endptr;

    /* 解析八进制权限 */
    new_mode = (unsigned short)strtol(mode_str, &endptr, 8);
    if (*endptr != '\0' || new_mode > 07777) {
        printf("chmod: invalid mode: %s\n", mode_str);
        return;
    }

    /* 保存状态 */
    saved_dir = ctx.current_dir;
    strcpy(saved_path, ctx.current_path);

    /* 解析路径 → 父目录 + 文件名 */
    strcpy(path_buf, path);
    {
        char *slash = strrchr(path_buf, '/');
        if (slash) {
            *slash = '\0';
            if (path_buf[0] == '\0') {
                ctx.current_dir = 1;
            } else if (dir_navigate(path_buf) != 0) {
                printf("chmod: cannot access '%s': "
                       "No such file or directory\n", path);
                goto chmod_restore;
            }
            name = slash + 1;
        } else {
            name = path_buf;
        }
    }

    /* 查找文件（先文件，再目录） */
    if (!dir_lookup(name, 1, &ino, &blk, &ent) &&
        !dir_lookup(name, 2, &ino, &blk, &ent)) {
        printf("chmod: cannot access '%s': "
               "No such file or directory\n", path);
        goto chmod_restore;
    }

    /* 权限检查：root 或文件所有者才可 chmod */
    inode_read(ino);
    if (ctx.current_uid != 0 && ctx.current_uid != ctx.inode_cache.i_uid) {
        printf("chmod: permission denied\n");
        goto chmod_restore;
    }

    ctx.inode_cache.i_mode = new_mode;
    ctx.inode_cache.i_ctime = (unsigned int)time(NULL);
    inode_write(ino);

chmod_restore:
    ctx.current_dir = saved_dir;
    strcpy(ctx.current_path, saved_path);
}

/* ================================================================
 * chown — 修改文件所有者
 *
 * 用法: chown <用户名> <路径>
 * 示例: chown alice file   chown root dir1/f1
 * ================================================================ */

void chown(const char *user_str, const char *path)
{
    unsigned short new_uid, new_gid, saved_dir, ino, blk, ent;
    char saved_path[256];
    char path_buf[256];
    const char *name;

    /* 查找用户 */
    if (user_find_by_name(user_str, &new_uid, &new_gid) != 0) {
        printf("chown: invalid user: '%s'\n", user_str);
        return;
    }

    /* 保存状态 */
    saved_dir = ctx.current_dir;
    strcpy(saved_path, ctx.current_path);

    /* 解析路径 */
    strcpy(path_buf, path);
    {
        char *slash = strrchr(path_buf, '/');
        if (slash) {
            *slash = '\0';
            if (path_buf[0] == '\0') {
                ctx.current_dir = 1;
            } else if (dir_navigate(path_buf) != 0) {
                printf("chown: cannot access '%s': "
                       "No such file or directory\n", path);
                goto chown_restore;
            }
            name = slash + 1;
        } else {
            name = path_buf;
        }
    }

    /* 查找文件 */
    if (!dir_lookup(name, 1, &ino, &blk, &ent) &&
        !dir_lookup(name, 2, &ino, &blk, &ent)) {
        printf("chown: cannot access '%s': "
               "No such file or directory\n", path);
        goto chown_restore;
    }

    /* 权限检查：仅 root 可 chown */
    if (ctx.current_uid != 0) {
        printf("chown: permission denied\n");
        goto chown_restore;
    }

    /* 改属主 */
    inode_read(ino);
    ctx.inode_cache.i_uid  = new_uid;
    ctx.inode_cache.i_gid  = new_gid;
    ctx.inode_cache.i_ctime = (unsigned int)time(NULL);
    inode_write(ino);

chown_restore:
    ctx.current_dir = saved_dir;
    strcpy(ctx.current_path, saved_path);
}
