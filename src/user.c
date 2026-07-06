/*
 * user.c — 用户管理子系统实现
 *
 * 用户数据持久化在虚拟磁盘的 USER_AREA 区域：
 *   Block 0: [user_count(2B) | padding(510B)]
 *   Block 1-8: user_account[0..31] (32 条目 × 128B = 4096B)
 *
 * 依赖：ctx.fp 由 context.c 管理，此处直接使用 fseek/fread/fwrite。
 */

#include <stdio.h>
#include <string.h>
#include "ext2_constants.h"
#include "fs_context.h"
#include "disk_io.h"
#include "user.h"

/* ---- 用户数据库运行时状态 ---- */
struct user_account user_db[MAX_USERS];
int user_count = 0;
int current_user_idx = -1;

/* ---- 内部常量 ---- */
#define USER_ACCOUNT_SIZE  128               /* sizeof(struct user_account) */
#define USER_HEADER_BLOCK  0                  /* 第 0 块: 头部 (user_count) */
#define USER_DATA_START    1                  /* 第 1 块起: 用户数据 */

/* ---- 辅助：计算用户区域在磁盘上的字节偏移 ---- */
static unsigned long user_block_offset(int blk)
{
    return (unsigned long)USER_AREA_START + (unsigned long)blk * BLOCK_SIZE;
}

/* ---- 从磁盘加载用户数据库 ---- */
int user_load(void)
{
    unsigned short count;
    int i, blk;
    unsigned long offset;
    char buf[512];

    /* 读取头部块获取 user_count */
    offset = user_block_offset(USER_HEADER_BLOCK);
    fseek(ctx.fp, offset, SEEK_SET);
    if (fread(buf, 512, 1, ctx.fp) != 1)
        return -1;

    count = (unsigned short)((unsigned char)buf[0] | ((unsigned char)buf[1] << 8));

    if (count == 0 || count > MAX_USERS) {
        /* 用户区域未初始化 */
        user_count = 0;
        current_user_idx = -1;
        return -1;
    }

    user_count = count;

    /* 逐块读取用户数据 */
    for (i = 0; i < (int)count; i++) {
        int entry_offset = i * USER_ACCOUNT_SIZE;
        blk = USER_DATA_START + entry_offset / BLOCK_SIZE;
        int within_blk = entry_offset % BLOCK_SIZE;

        offset = user_block_offset(blk);
        fseek(ctx.fp, offset, SEEK_SET);
        if (fread(buf, BLOCK_SIZE, 1, ctx.fp) != 1)
            return -1;

        memcpy(&user_db[i], buf + within_blk, USER_ACCOUNT_SIZE);
    }

    current_user_idx = -1;
    return 0;
}

/* ---- 保存用户数据库到磁盘 ---- */
int user_save(void)
{
    int i, blk;
    unsigned long offset;
    char buf[512];
    unsigned short count = (unsigned short)user_count;

    /* 确保用户区域块在位图中标记为已使用 */
    {
        int base_blk = DATA_BLOCK_COUNTS - USER_AREA_BLOCKS;
        block_bmp_read();
        for (i = 0; i < USER_AREA_BLOCKS; i++) {
            int blk_no = base_blk + i;
            int byte_idx = blk_no / 8;
            int bit_idx  = blk_no % 8;
            ctx.block_bmp[byte_idx] |= (unsigned char)(128 >> bit_idx);
        }
        block_bmp_write();
    }

    /* 写入头部块 */
    offset = user_block_offset(USER_HEADER_BLOCK);
    fseek(ctx.fp, offset, SEEK_SET);
    memset(buf, 0, 512);
    buf[0] = (char)(count & 0xFF);
    buf[1] = (char)((count >> 8) & 0xFF);
    fwrite(buf, 512, 1, ctx.fp);
    fflush(ctx.fp);

    /* 逐块写入用户数据 */
    for (i = 0; i < (int)count; i++) {
        int entry_offset = i * USER_ACCOUNT_SIZE;
        blk = USER_DATA_START + entry_offset / BLOCK_SIZE;
        int within_blk = entry_offset % BLOCK_SIZE;

        /* 读出现有块内容（保留同一块中其他用户数据） */
        offset = user_block_offset(blk);
        fseek(ctx.fp, offset, SEEK_SET);
        if (fread(buf, BLOCK_SIZE, 1, ctx.fp) != 1)
            memset(buf, 0, BLOCK_SIZE);

        memcpy(buf + within_blk, &user_db[i], USER_ACCOUNT_SIZE);

        fseek(ctx.fp, offset, SEEK_SET);
        fwrite(buf, BLOCK_SIZE, 1, ctx.fp);
        fflush(ctx.fp);
    }

    return 0;
}

/* ---- 初始化默认 root 用户 ---- */
void user_init_default(void)
{
    memset(user_db, 0, sizeof(user_db));
    user_count = 1;

    strcpy(user_db[0].username, "root");
    strcpy(user_db[0].password, "root");
    user_db[0].uid = 0;
    user_db[0].gid = 0;
    strcpy(user_db[0].home, "/");

    current_user_idx = -1;
}

/* ---- 验证用户名密码 ---- */
int user_auth(const char *name, const char *pass)
{
    int i;
    for (i = 0; i < user_count; i++) {
        if (!strcmp(user_db[i].username, name)) {
            if (!strcmp(user_db[i].password, pass)) {
                current_user_idx = i;
                /* 设置会话上下文 */
                ctx.current_uid = user_db[i].uid;
                ctx.current_gid = user_db[i].gid;
                strcpy(ctx.current_user, user_db[i].username);
                ctx.logged_in = 1;
                return 0;  /* 成功 */
            }
            return 1;  /* 密码错误 */
        }
    }
    return 2;  /* 用户不存在 */
}

/* ---- 交互式登录 ---- */
int user_login(void)
{
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];
    int attempts = 0;
    int rc;

    /* 首次加载或新建默认用户 */
    if (user_load() != 0) {
        printf("User database not found. Creating default root user...\n");
        user_init_default();
        user_save();
        printf("Default user created: root / root\n");
    } else {
        printf("User database loaded. %d user(s) found.\n", user_count);
    }

    printf("\n");
    printf("========================================\n");
    printf("  EXT2 File System Simulator - Login\n");
    printf("========================================\n");

    /* 列出已有用户，方便知道填什么 */
    {
        int u;
        printf("Available users:\n");
        for (u = 0; u < user_count; u++)
            printf("  %s (uid=%d)\n", user_db[u].username, user_db[u].uid);
        printf("\n");
    }

    while (attempts < 3) {
        printf("Username: ");
        if (scanf("%31s", username) != 1) {
            printf("\nLogin aborted.\n");
            return -1;
        }
        printf("Password: ");
        if (scanf("%31s", password) != 1) {
            printf("\nLogin aborted.\n");
            return -1;
        }

        rc = user_auth(username, password);
        if (rc == 0) {
            printf("\nWelcome, %s!\n\n", username);
            return 0;
        } else if (rc == 1) {
            printf("Incorrect password. Please try again.\n\n");
        } else {
            printf("User '%s' does not exist. Please try again.\n\n", username);
        }
        attempts++;
    }

    printf("Too many failed attempts. Exiting.\n");
    return -1;
}

/* ---- 添加新用户（仅 root 可调用） ---- */
int user_add(const char *name, const char *pass)
{
    int i;

    /* 仅 root (uid=0) 可添加用户 */
    if (ctx.current_uid != 0) {
        printf("Permission denied. Only root can add users.\n");
        return -1;
    }

    if (user_count >= MAX_USERS) {
        printf("Maximum number of users (%d) reached.\n", MAX_USERS);
        return -1;
    }

    /* 检查用户名是否已存在 */
    for (i = 0; i < user_count; i++) {
        if (!strcmp(user_db[i].username, name)) {
            printf("User '%s' already exists.\n", name);
            return -1;
        }
    }

    /* 分配 uid (简单递增) */
    {
        unsigned short max_uid = 0;
        for (i = 0; i < user_count; i++) {
            if (user_db[i].uid > max_uid)
                max_uid = user_db[i].uid;
        }
        i = user_count;
        memset(&user_db[i], 0, USER_ACCOUNT_SIZE);
        strcpy(user_db[i].username, name);
        strcpy(user_db[i].password, pass);
        user_db[i].uid = max_uid + 1;
        user_db[i].gid = user_db[i].uid;  /* 用户私有组 */
        user_db[i].home[0] = '/';
        user_db[i].home[1] = 'h';
        user_db[i].home[2] = 'o';
        user_db[i].home[3] = 'm';
        user_db[i].home[4] = 'e';
        user_db[i].home[5] = '/';
        strncpy(user_db[i].home + 6, name, 53);
        user_db[i].home[59] = '\0';
        user_count++;
    }

    user_save();
    printf("User '%s' added successfully (uid=%d).\n",
           name, user_db[user_count - 1].uid);
    return 0;
}

/* ---- 修改当前用户密码 ---- */
int user_passwd(const char *oldpass, const char *newpass)
{
    if (current_user_idx < 0 || current_user_idx >= user_count) {
        printf("No user is currently logged in.\n");
        return -1;
    }

    /* root 可跳过旧密码验证 */
    if (ctx.current_uid != 0) {
        if (strcmp(user_db[current_user_idx].password, oldpass)) {
            printf("Incorrect old password.\n");
            return -1;
        }
    }

    strcpy(user_db[current_user_idx].password, newpass);
    user_save();
    printf("Password changed successfully.\n");
    return 0;
}
