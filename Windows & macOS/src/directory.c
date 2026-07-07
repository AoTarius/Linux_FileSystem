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
#include "user.h"

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
        ctx.inode_cache.i_atime = (unsigned int)now;
        ctx.inode_cache.i_mtime = (unsigned int)now;
        ctx.inode_cache.i_ctime = (unsigned int)now;
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
static void format_time_str(char *out, int maxlen, unsigned int timestamp)
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
    unsigned short i, k;
    char block_str[32], time_str[16], mode_str[10];

    /* 表头 — 全部左对齐，列宽与 body 一致 */
    printf("%-15s %-8s %-15s %-10s %-18s %-16s %-12s\n",
           "items", "owner", "type", "mode", "blocks", "mtime", "size");

    inode_read(ctx.current_dir);
    for (i = 0; i < ctx.inode_cache.i_blocks; i++) {
        dir_read(ctx.inode_cache.i_block[i]);
        for (k = 0; k < 32; k++) {
            if (!ctx.dir_cache[k].inode) continue;

            if (ctx.dir_cache[k].file_type == 2) {
                inode_read(ctx.dir_cache[k].inode);

                format_mode_str(mode_str, ctx.inode_cache.i_mode);
                format_block_str(block_str, sizeof(block_str),
                                 ctx.inode_cache.i_block,
                                 ctx.inode_cache.i_blocks);
                format_time_str(time_str, sizeof(time_str),
                                ctx.inode_cache.i_mtime);

                /* . 和 .. 不显示大小 */
                {
                    char size_str[16];
                    if (!strcmp(ctx.dir_cache[k].name, ".") ||
                        !strcmp(ctx.dir_cache[k].name, ".."))
                        sprintf(size_str, "----");
                    else
                        sprintf(size_str, "%u bytes",
                                (unsigned int)ctx.inode_cache.i_size);

                    printf("%-15s %-8s %-15s %-10s %-18s %-16s %-12s\n",
                           ctx.dir_cache[k].name,
                           user_name_by_uid(ctx.inode_cache.i_uid),
                           "<DIR>",
                           mode_str, block_str, time_str, size_str);
                }
            } else if (ctx.dir_cache[k].file_type == 1) {
                inode_read(ctx.dir_cache[k].inode);

                format_mode_str(mode_str, ctx.inode_cache.i_mode);
                format_block_str(block_str, sizeof(block_str),
                                 ctx.inode_cache.i_block,
                                 ctx.inode_cache.i_blocks);
                format_time_str(time_str, sizeof(time_str),
                                ctx.inode_cache.i_mtime);

                {
                    char size_str[16];
                    sprintf(size_str, "%u bytes",
                            (unsigned int)ctx.inode_cache.i_size);

                    printf("%-15s %-8s %-15s %-10s %-18s %-16s %-12s\n",
                           ctx.dir_cache[k].name,
                           user_name_by_uid(ctx.inode_cache.i_uid),
                           "<FILE>",
                           mode_str, block_str, time_str, size_str);
                }
            }
        }
        inode_read(ctx.current_dir);
    }
}

/* ================================================================
 * 路径字符串操作（内部辅助）
 * ================================================================ */

/* 从 ctx.current_path 的末尾剥离最后一个目录组件。
 * 例: "~/t1/t2/" → "~/t1/" 并更新 ctx.current_dirlen */
static void path_strip_last(void)
{
    int len = (int)strlen(ctx.current_path);
    if (len <= 2) return;  /* 已经是 "~/" */

    ctx.current_path[len - 1] = '\0';          /* 去掉末尾 '/' */
    char *slash = strrchr(ctx.current_path, '/');
    if (slash) {
        *(slash + 1) = '\0';                   /* 截断到上一个 '/' 之后 */
        if (slash == ctx.current_path + 1) {
            ctx.current_dirlen = 0;            /* 回到根目录 */
        } else {
            /* 计算父目录名长度 */
            char *prev = slash - 1;
            while (prev > ctx.current_path && *prev != '/') prev--;
            if (*prev == '/') prev++;
            ctx.current_dirlen = (unsigned short)(slash - prev);
        }
    }
}

/* 向 ctx.current_path 追加一个目录组件。
 * 例: "~/t1/" + "t2" → "~/t1/t2/" */
static void path_append(const char *name)
{
    strcat(ctx.current_path, name);
    strcat(ctx.current_path, "/");
    ctx.current_dirlen = (unsigned short)strlen(name);
}

/* ================================================================
 * 多级目录导航
 * ================================================================ */

/*
 * 按多级路径导航到目标目录。
 * 支持 ~ 绝对路径、相对路径、. 和 .. 组件。
 * 成功返回 0 并更新 ctx.current_dir / ctx.current_path；
 * 失败返回 -1（ctx 状态未定义，调用者应回滚）。
 * 不打印错误信息（由调用者负责）。
 */
int dir_navigate(const char *path)
{
    char buf[256];
    char *p, *start;
    unsigned short i, j, k;

    strcpy(buf, path);
    p = buf;

    /* 处理 ~ 开头的绝对路径 */
    if (*p == '~') {
        ctx.current_dir = 1;
        strcpy(ctx.current_path, "~/");
        ctx.current_dirlen = 0;
        p++;
        if (*p == '/') p++;          /* 跳过 ~/ */
        if (*p == '\0') return 0;    /* 只有 ~ 或 ~/ → 根目录 */
    }

    /* 逐组件导航 */
    while (*p) {
        /* 跳过前导 '/' */
        while (*p == '/') p++;
        if (*p == '\0') break;

        /* 标记当前组件的起始位置 */
        start = p;
        while (*p && *p != '/') p++;

        {
            char saved = *p;
            *p = '\0';               /* 临时截断为单个组件名 */

            if (!strcmp(start, ".")) {
                /* 当前目录 — 无操作 */
            } else if (!strcmp(start, "..")) {
                if (ctx.current_dir != 1) {
                    if (dir_lookup("..", 2, &i, &j, &k)) {
                        ctx.current_dir = i;
                        path_strip_last();
                    }
                }
            } else {
                if (dir_lookup(start, 2, &i, &j, &k)) {
                    ctx.current_dir = i;
                    path_append(start);
                } else {
                    *p = saved;
                    return -1;       /* 组件未找到 */
                }
            }

            *p = saved;              /* 恢复 '/' 或 '\0' */
        }

        if (*p) p++;                 /* 跳过 '/' 分隔符 */
    }

    return 0;
}

/* ================================================================
 * 用户命令：cd
 * ================================================================ */

void cd(const char *path)
{
    unsigned short saved_dir = ctx.current_dir;
    char saved_path[256];
    strcpy(saved_path, ctx.current_path);

    if (dir_navigate(path) != 0) {
        /* 导航失败 — 回滚到原始状态 */
        ctx.current_dir = saved_dir;
        strcpy(ctx.current_path, saved_path);
        printf("The directory %s not exists!\n", path);
    }
}
