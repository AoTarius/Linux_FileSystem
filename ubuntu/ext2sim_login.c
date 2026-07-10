/*
 * ext2sim_login.c — 用户登录程序
 *
 * 直接操作磁盘镜像的用户数据库区域（最后 10 块）。
 * 与内核模块 ext2_sim.ko 配合使用，也与 Windows_macOS 版本兼容。
 *
 * 编译:  gcc -o ext2sim_login ext2sim_login.c
 * 使用:  sudo ./ext2sim_login /dev/loop0
 *
 * 特性:
 *   - root 用户预置（格式化时创建），密码 "root"
 *   - 首次登录不存在的用户名 → 自动注册（密码即首次输入的密码）
 *   - 最多 3 次重试
 *   - 认证通过后 fork + setuid + exec bash
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>

/* ── 磁盘几何常量（与内核模块、Windows_macOS 保持一致）─────────── */

#define BLOCK_SIZE              512
#define DISK_TOTAL_BLOCKS       4612
#define DATA_BLOCK_START        516
#define DATA_BLOCK_COUNTS       4096
#define USER_AREA_BLOCKS        10
#define USER_AREA_START_REL     (DATA_BLOCK_COUNTS - USER_AREA_BLOCKS)  /* 4086 */
#define USER_AREA_START_ABS     (DATA_BLOCK_START + USER_AREA_START_REL) /* 4602 */
#define USER_AREA_BYTE_OFFSET   ((uint64_t)USER_AREA_START_ABS * BLOCK_SIZE)

#define MAX_USERS       32
#define USERNAME_LEN    32
#define PASSWORD_LEN    32
#define USER_ACCOUNT_SIZE 128

/* ── 用户账户结构（与磁盘布局一致）────────────────────────────── */

struct user_account {
    char     username[USERNAME_LEN];
    char     password[PASSWORD_LEN];
    uint16_t uid;
    uint16_t gid;
    char     home[60];
};

/* ── 运行时状态 ─────────────────────────────────────────────── */

static struct user_account users[MAX_USERS];
static int user_count = 0;
static int fd = -1;                     /* 磁盘镜像文件描述符 */

/* ── 加载用户数据库 ─────────────────────────────────────────── */

static int db_load(void)
{
    unsigned char buf[BLOCK_SIZE];
    unsigned char *p;
    uint64_t offset;
    int i;

    /* 读取头部块 */
    offset = USER_AREA_BYTE_OFFSET;
    if (lseek(fd, offset, SEEK_SET) < 0) {
        perror("lseek user header");
        return -1;
    }
    if (read(fd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
        perror("read user header");
        return -1;
    }

    user_count = (int)(buf[0] | ((uint16_t)buf[1] << 8));

    if (user_count == 0 || user_count > MAX_USERS) {
        fprintf(stderr, "User database not initialized. Please mount first to auto-format.\n");
        return -1;
    }

    /* 逐块读取用户记录 */
    for (i = 0; i < user_count; i++) {
        int entry_offset = i * USER_ACCOUNT_SIZE;
        int blk_in_user = 1 + entry_offset / BLOCK_SIZE;
        int within_blk  = entry_offset % BLOCK_SIZE;

        offset = USER_AREA_BYTE_OFFSET + (uint64_t)blk_in_user * BLOCK_SIZE;
        if (lseek(fd, offset, SEEK_SET) < 0) {
            perror("lseek user data");
            return -1;
        }
        if (read(fd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
            perror("read user data");
            return -1;
        }

        p = buf + within_blk;
        memcpy(users[i].username, p, USERNAME_LEN);
        memcpy(users[i].password, p + 32, PASSWORD_LEN);
        users[i].uid = (uint16_t)(p[64] | ((uint16_t)p[65] << 8));
        users[i].gid = (uint16_t)(p[66] | ((uint16_t)p[67] << 8));
        memcpy(users[i].home, p + 68, 60);
    }

    return 0;
}

/* ── 保存用户数据库 ─────────────────────────────────────────── */

static int db_save(void)
{
    unsigned char buf[BLOCK_SIZE];
    const uint16_t count = (uint16_t)user_count;
    uint64_t offset;
    int i;

    /* 写入头部块 */
    offset = USER_AREA_BYTE_OFFSET;
    if (lseek(fd, offset, SEEK_SET) < 0) {
        perror("lseek user header (write)");
        return -1;
    }
    memset(buf, 0, BLOCK_SIZE);
    buf[0] = (unsigned char)(count & 0xFF);
    buf[1] = (unsigned char)((count >> 8) & 0xFF);
    if (write(fd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
        perror("write user header");
        return -1;
    }

    /* 逐块写回用户记录 */
    for (i = 0; i < user_count; i++) {
        int entry_offset = i * USER_ACCOUNT_SIZE;
        int blk_in_user = 1 + entry_offset / BLOCK_SIZE;
        int within_blk  = entry_offset % BLOCK_SIZE;
        unsigned char *p;

        offset = USER_AREA_BYTE_OFFSET + (uint64_t)blk_in_user * BLOCK_SIZE;
        if (lseek(fd, offset, SEEK_SET) < 0) {
            perror("lseek user data (write)");
            return -1;
        }
        /* 读取现有一整块（保留同块的其他用户） */
        if (read(fd, buf, BLOCK_SIZE) != BLOCK_SIZE)
            memset(buf, 0, BLOCK_SIZE);

        p = buf + within_blk;
        memset(p, 0, USER_ACCOUNT_SIZE);
        strncpy((char *)p,      users[i].username, USERNAME_LEN - 1);
        strncpy((char *)(p+32), users[i].password, PASSWORD_LEN - 1);
        p[64] = (unsigned char)(users[i].uid & 0xFF);
        p[65] = (unsigned char)((users[i].uid >> 8) & 0xFF);
        p[66] = (unsigned char)(users[i].gid & 0xFF);
        p[67] = (unsigned char)((users[i].gid >> 8) & 0xFF);
        strncpy((char *)(p+68), users[i].home, 59);

        if (lseek(fd, offset, SEEK_SET) < 0) {
            perror("lseek user data (write 2)");
            return -1;
        }
        if (write(fd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
            perror("write user data");
            return -1;
        }
    }

    fsync(fd);
    return 0;
}

/* ── 查找用户 ───────────────────────────────────────────────── */

static int find_user(const char *name)
{
    int i;
    for (i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, name) == 0)
            return i;
    }
    return -1;
}

/* ── 添加新用户（分配下一个可用 uid）────────────────────────── */

static void add_user(const char *name, const char *pass)
{
    uint16_t max_uid = 0;
    int i;

    for (i = 0; i < user_count; i++) {
        if (users[i].uid > max_uid)
            max_uid = users[i].uid;
    }

    i = user_count;
    memset(&users[i], 0, sizeof(users[i]));
    strncpy(users[i].username, name, USERNAME_LEN - 1);
    strncpy(users[i].password, pass, PASSWORD_LEN - 1);
    /* 普通用户 uid 从 1000 起（root=0） */
    users[i].uid = (max_uid >= 1000) ? (max_uid + 1) : 1000;
    users[i].gid = users[i].uid;
    strcpy(users[i].home, "/");

    user_count++;
}

/* ── 读取密码（关闭终端回显）────────────────────────────────── */

static int read_password(char *buf, size_t size)
{
    struct termios old, new;
    int n;

    if (tcgetattr(STDIN_FILENO, &old) < 0)
        return -1;
    new = old;
    new.c_lflag &= ~((tcflag_t)ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new) < 0)
        return -1;

    if (fgets(buf, (int)size, stdin) == NULL) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
        return -1;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    printf("\n");

    /* 去掉尾部的换行符 */
    n = (int)strlen(buf);
    if (n > 0 && buf[n - 1] == '\n')
        buf[n - 1] = '\0';

    return 0;
}

/* ── 自动探测 ext2sim 设备 ──────────────────────────────────── */

static char *find_ext2sim_device(void)
{
    static char dev_path[256];
    FILE *fp;
    char line[512];
    const char *mounts = "/proc/mounts";

    fp = fopen(mounts, "r");
    if (!fp) {
        perror("open /proc/mounts");
        return NULL;
    }

    while (fgets(line, sizeof(line), fp)) {
        char dev[256], mnt[256], fstype[64];
        if (sscanf(line, "%255s %255s %63s", dev, mnt, fstype) == 3) {
            if (strcmp(fstype, "ext2sim") == 0) {
                strncpy(dev_path, dev, sizeof(dev_path) - 1);
                dev_path[sizeof(dev_path) - 1] = '\0';
                fclose(fp);
                return dev_path;
            }
        }
    }

    fclose(fp);
    return NULL;
}

/* ── 入口 ───────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];
    char *device;
    int idx;
    int attempt;
    int u;

    /* 获取设备路径：优先用命令行参数，否则自动探测 */
    if (argc >= 2) {
        device = argv[1];
    } else {
        device = find_ext2sim_device();
        if (!device) {
            fprintf(stderr, "No ext2sim filesystem mounted.\n");
            fprintf(stderr, "Usage: %s [device]\n", argv[0]);
            fprintf(stderr, "Example: sudo %s /dev/loop0\n", argv[0]);
            return 1;
        }
        printf("Auto-detected ext2sim device: %s\n", device);
    }

    /* 需要 root 权限才能 setuid 和读写块设备 */
    if (geteuid() != 0) {
        fprintf(stderr, "This program must be run as root (sudo).\n");
        return 1;
    }

    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("open device");
        return 1;
    }

    /* 加载用户数据库 */
    if (db_load() != 0) {
        close(fd);
        return 1;
    }

    /* ── 欢迎界面 ─────────────────────────── */
    printf("\n");
    printf("========================================\n");
    printf("  EXT2 File System Simulator - Login\n");
    printf("========================================\n");
    printf("\nExisting users:\n");
    for (u = 0; u < user_count; u++) {
        printf("  %-16s (uid=%u)\n", users[u].username, users[u].uid);
    }
    printf("\n");

    /* ── 登录循环 ────────────────────────── */
    for (attempt = 0; attempt < 3; attempt++) {
        printf("Login: ");
        fflush(stdout);
        if (fgets(username, USERNAME_LEN, stdin) == NULL) {
            printf("\nLogin aborted.\n");
            close(fd);
            return 1;
        }
        /* 去掉换行符 */
        {
            int n = (int)strlen(username);
            if (n > 0 && username[n - 1] == '\n')
                username[n - 1] = '\0';
        }
        if (username[0] == '\0')
            continue;

        printf("Password: ");
        fflush(stdout);
        if (read_password(password, PASSWORD_LEN) != 0) {
            printf("\nLogin aborted.\n");
            close(fd);
            return 1;
        }

        idx = find_user(username);
        if (idx >= 0) {
            /* 老用户：验证密码 */
            if (strcmp(users[idx].password, password) == 0) {
                break;  /* 成功 */
            }
            printf("Incorrect password.\n\n");
        } else {
            /* 新用户：自动注册 */
            if (user_count >= MAX_USERS) {
                printf("Maximum users (%d) reached. Cannot register.\n\n", MAX_USERS);
                continue;
            }
            add_user(username, password);
            idx = user_count - 1;
            if (db_save() != 0) {
                printf("Failed to save user database.\n");
                close(fd);
                return 1;
            }
            printf("New user registered! Welcome, %s! (uid=%u)\n\n",
                   users[idx].username, users[idx].uid);
            break;
        }
    }

    if (attempt >= 3) {
        printf("Too many failed attempts. Exiting.\n");
        close(fd);
        return 1;
    }

    close(fd);

    printf("Starting shell as %s (uid=%u, gid=%u)...\n",
           users[idx].username, users[idx].uid, users[idx].gid);
    printf("Use 'exit' or Ctrl+D to logout.\n\n");

    /*
     * ⚠️ 先设置环境（此时仍是 root），再 drop 权限。
     * 顺序至关重要：setuid 之后 readlink /proc/self/exe 会失败。
     */

    /* 将 ext2sim 工具目录加到 PATH */
    {
        char self[512], self_dir[512], new_path[1024];
        ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (n > 0) {
            self[n] = '\0';
            char *slash = strrchr(self, '/');
            if (slash) {
                size_t dlen = (size_t)(slash - self);
                memcpy(self_dir, self, dlen);
                self_dir[dlen] = '\0';
                snprintf(new_path, sizeof(new_path),
                         "%s:%s", self_dir, getenv("PATH") ?: "/usr/bin");
                setenv("PATH", new_path, 1);
            }
        }
    }

    setenv("HOME", "/", 1);
    setenv("USER", users[idx].username, 1);

    /*
     * 写临时 bashrc：
     *   ll    → ext2sim_ls（含物理块地址的详细列表）
     *   PS1   → 用实际用户名，不用 \u（\u 查宿主 /etc/passwd，会显示"无名氏"）
     *   ls 保持系统原样不变。
     */
    {
        char tmp_path[] = "/tmp/ext2sim_rc_XXXXXX";
        int rc_fd = mkstemp(tmp_path);
        if (rc_fd >= 0) {
            dprintf(rc_fd,
                "alias ll='ext2sim_ls'\n"
                "export PS1='%s@ext2sim:\\w\\$ '\n",
                users[idx].username
            );
            fchmod(rc_fd, 0644);  /* 让目标用户能读（否则 setuid 后 bash 无权加载） */
            close(rc_fd);
            setenv("HOME", "/", 1);
            setenv("USER", users[idx].username, 1);

            /* 切换到 ext2 文件系统根目录 */
            if (chdir("/mnt/ext2") != 0)
                chdir("/");

            /* drop 权限 */
            if (setgid(users[idx].gid) != 0) {
                perror("setgid");
                return 1;
            }
            if (setuid(users[idx].uid) != 0) {
                perror("setuid");
                return 1;
            }

            execl("/bin/bash", "bash", "--rcfile", tmp_path, NULL);
        }
    }

    /* 回退：mkstemp 失败时走这里 */
    if (chdir("/mnt/ext2") != 0) chdir("/");
    setgid(users[idx].gid);
    setuid(users[idx].uid);
    setenv("PS1", "ext2sim:\\w\\$ ", 1);
    execl("/bin/bash", "bash", "--norc", NULL);

    /* execl 成功则不会到达这里 */
    perror("execl bash");
    return 1;
}
