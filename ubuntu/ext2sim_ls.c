/*
 * ext2sim_ls.c — 列出 ext2 文件系统内容（含物理块地址）
 *
 * 直接读取磁盘镜像，遍历目录项和 inode，显示物理块号。
 * 输出格式与 Windows_macOS 版本一致。
 *
 * 编译:  gcc -o ext2sim_ls ext2sim_ls.c
 * 使用:  ./ext2sim_ls              # 自动探测 ext2sim 设备，列出根目录
 *         ./ext2sim_ls /dev/loop0  # 手动指定设备
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

/* ── 磁盘几何常量 ───────────────────────────────────────────── */

#define BLOCK_SIZE              512
#define DATA_BLOCK_START        516
#define DATA_BLOCK_COUNTS       4096
#define TOTAL_INODES            4096
#define ROOT_INO                1
#define MAX_NAME_LEN            8
#define ENTRY_SIZE              16
#define ENTRY_PER_BLOCK         32

#define INODE_TABLE_START       4
#define INODE_SIZE              64
#define INODES_PER_BLOCK        8

#define DIRECT_BLOCKS           12
#define INDIRECT_PTRS           256       /* 512B / 2B */

/* ── 磁盘结构体 ─────────────────────────────────────────────── */

struct disk_inode {
    uint16_t i_mode;
    uint16_t i_blocks;
    uint16_t i_uid;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint16_t i_flags;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_block[15];
    uint8_t  i_pad[2];
};

struct disk_dirent {
    uint16_t inode;
    uint16_t rec_len;
    uint16_t name_len;
    uint8_t  file_type;
    uint8_t  name[9];
};

struct user_account {
    char     username[32];
    char     password[32];
    uint16_t uid;
    uint16_t gid;
    char     home[60];
};

/* ── 全局 ───────────────────────────────────────────────────── */

static int fd;
static struct user_account users[32];
static int user_count;

/* ── 磁盘读取辅助 ──────────────────────────────────────────── */

static int read_block(int blk_abs, void *buf)
{
    if (lseek(fd, (off_t)blk_abs * BLOCK_SIZE, SEEK_SET) < 0)
        return -1;
    return (read(fd, buf, BLOCK_SIZE) == BLOCK_SIZE) ? 0 : -1;
}

static int read_inode(int ino, struct disk_inode *ino_buf)
{
    int blk = INODE_TABLE_START + (ino - 1) / INODES_PER_BLOCK;
    int off = ((ino - 1) % INODES_PER_BLOCK) * INODE_SIZE;
    unsigned char buf[BLOCK_SIZE];

    if (read_block(blk, buf) != 0)
        return -1;
    memcpy(ino_buf, buf + off, INODE_SIZE);
    return 0;
}

/* ── 块号格式化：相对块号 → 绝对块号，逗号分隔 ─────────────── */

static void fmt_blocks(const struct disk_inode *ino, char *out, int maxlen)
{
    int i, pos = 0;
    int found = 0;

    if (ino->i_blocks == 0) {
        snprintf(out, maxlen, "----");
        return;
    }

    out[0] = '\0';

    /* 直接块 */
    for (i = 0; i < DIRECT_BLOCKS && i < (int)ino->i_blocks; i++) {
        uint16_t rel = ino->i_block[i];
        if (rel > 0 || ino->i_size > 0) {
            pos += snprintf(out + pos, maxlen - pos,
                           "%s%u", found++ ? "," : "",
                           (unsigned)(DATA_BLOCK_START + rel));
        }
    }

    /* 如果还有未列出的块（间接块），追加 +N */
    if ((int)ino->i_blocks > DIRECT_BLOCKS) {
        int remaining = (int)ino->i_blocks - found;
        if (remaining > 0) {
            pos += snprintf(out + pos, maxlen - pos,
                           "%s+%u indirect", found ? "," : "",
                           (unsigned)remaining);
        }
    }

    if (found == 0 && ino->i_blocks > 0)
        snprintf(out, maxlen, "indirect");
}

/* ── 权限字符串：rwxrwxrwx ──────────────────────────────────── */

static void fmt_mode(uint16_t mode, char *out)
{
    static const char bits[] = "rwxrwxrwx";
    int i;
    for (i = 0; i < 9; i++)
        out[i] = (mode & (0400 >> i)) ? bits[i] : '-';
    out[9] = '\0';
}

/* ── 时间格式化 ────────────────────────────────────────────── */

static void fmt_time(uint32_t ts, char *out, int maxlen)
{
    time_t t = (time_t)ts;
    struct tm *tm = localtime(&t);
    if (tm)
        strftime(out, maxlen, "%Y-%m-%d %H:%M", tm);
    else
        snprintf(out, maxlen, "----");
}

/* ── 用户名查表 ────────────────────────────────────────────── */

static const char *user_name(uint16_t uid)
{
    int i;
    for (i = 0; i < user_count; i++)
        if (users[i].uid == uid)
            return users[i].username;
    return "?";
}

/* ── 加载用户数据库（复用 login 的逻辑）─────────────────────── */

static int load_users(void)
{
    unsigned char buf[BLOCK_SIZE];
    uint64_t user_area_start;
    int i;

    user_area_start = (uint64_t)(DATA_BLOCK_START + DATA_BLOCK_COUNTS - 10)
                    * BLOCK_SIZE;

    lseek(fd, user_area_start, SEEK_SET);
    if (read(fd, buf, BLOCK_SIZE) != BLOCK_SIZE)
        return -1;
    user_count = (int)(buf[0] | ((uint16_t)buf[1] << 8));
    if (user_count < 1 || user_count > 32)
        return -1;

    for (i = 0; i < user_count; i++) {
        int blk = 1 + (i * 128) / BLOCK_SIZE;
        int off = (i * 128) % BLOCK_SIZE;
        unsigned char *p;

        lseek(fd, user_area_start + (uint64_t)blk * BLOCK_SIZE, SEEK_SET);
        read(fd, buf, BLOCK_SIZE);
        p = buf + off;
        memcpy(users[i].username, p, 32);
        users[i].uid = (uint16_t)(p[64] | ((uint16_t)p[65] << 8));
        users[i].gid = (uint16_t)(p[66] | ((uint16_t)p[67] << 8));
    }
    return 0;
}

/* ── 自动探测 ext2sim 设备 ──────────────────────────────────── */

static char *find_device(void)
{
    static char path[256];
    FILE *fp = fopen("/proc/mounts", "r");
    char line[512];

    if (!fp) return NULL;
    while (fgets(line, sizeof(line), fp)) {
        char dev[256], mnt[256], fstype[64];
        if (sscanf(line, "%255s %255s %63s", dev, mnt, fstype) == 3) {
            if (strcmp(fstype, "ext2sim") == 0) {
                strncpy(path, dev, sizeof(path) - 1);
                fclose(fp);
                return path;
            }
        }
    }
    fclose(fp);
    return NULL;
}

/* ── 列出目录内容 ──────────────────────────────────────────── */

static void list_dir(int dir_ino)
{
    struct disk_inode dir_ino_buf;
    unsigned char blk_buf[BLOCK_SIZE];
    struct disk_dirent *de;
    int blk_abs, i, j;

    if (read_inode(dir_ino, &dir_ino_buf) != 0)
        return;

    for (i = 0; i < (int)dir_ino_buf.i_blocks; i++) {
        blk_abs = DATA_BLOCK_START + dir_ino_buf.i_block[i];
        if (read_block(blk_abs, blk_buf) != 0)
            continue;

        for (j = 0; j < ENTRY_PER_BLOCK; j++) {
            char blocks[64], mode[10], time_buf[20];
            struct disk_inode child_ino;
            const char *type;

            de = (struct disk_dirent *)(blk_buf + j * ENTRY_SIZE);
            if (de->inode == 0) continue;
            if (de->name_len == 0) continue;

            if (read_inode(de->inode, &child_ino) != 0)
                continue;

            fmt_blocks(&child_ino, blocks, sizeof(blocks));
            fmt_mode(child_ino.i_mode, mode);
            fmt_time(child_ino.i_mtime, time_buf, sizeof(time_buf));

            type = (de->file_type == 2) ? "<DIR>" : "<FILE>";

            {
                char size_str[16];
                if (de->file_type == 2 &&
                    (de->name[0] == '.' && (de->name_len == 1 ||
                     (de->name_len == 2 && de->name[1] == '.'))))
                    snprintf(size_str, sizeof(size_str), "----");
                else
                    snprintf(size_str, sizeof(size_str), "%u bytes",
                             (unsigned)child_ino.i_size);

                printf("  %-15s %-8s %-7s %-9s %-20s %-16s %s\n",
                       de->name,
                       user_name(child_ino.i_uid),
                       type, mode, blocks, time_buf, size_str);
            }
        }
    }
}

/* ── 入口 ───────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    char *device;

    if (argc >= 2)
        device = argv[1];
    else {
        device = find_device();
        if (!device) {
            fprintf(stderr, "No ext2sim filesystem mounted.\n");
            fprintf(stderr, "Usage: %s [device]\n", argv[0]);
            return 1;
        }
    }

    fd = open(device, O_RDONLY);
    if (fd < 0) {
        perror("open device");
        return 1;
    }

    load_users();

    printf("\n");
    printf("  %-15s %-8s %-7s %-9s %-20s %-16s %s\n",
           "items", "owner", "type", "mode", "blocks", "mtime", "size");
    printf("  %s\n",
           "------------------------------------------------------------------------------------------------------");

    list_dir(ROOT_INO);

    printf("\n");
    close(fd);
    return 0;
}
