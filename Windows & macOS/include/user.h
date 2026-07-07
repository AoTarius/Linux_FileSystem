#ifndef _USER_H
#define _USER_H

/*
 * user.h — 用户管理子系统
 *
 * 模拟 Linux 的 /etc/passwd 和 /etc/shadow。
 * 用户数据持久化存储在虚拟磁盘的 USER_AREA 区域（最后 10 个数据块）。
 * 初始化时自动创建默认 root 用户 (uid=0, 密码="root")。
 */

#define MAX_USERS       32        /* 最大用户数 */
#define USERNAME_LEN    32        /* 用户名最大长度 */
#define PASSWORD_LEN    32        /* 密码最大长度 */

/*
 * 用户账户结构 (128 bytes)
 * 32 用户 × 128B = 4096B = 8 块，另 2 块作为头部/预留
 */
struct user_account {
    char username[USERNAME_LEN];   /* 用户名 */
    char password[PASSWORD_LEN];   /* 密码 (明文，模拟 /etc/shadow) */
    unsigned short uid;            /* 用户 ID */
    unsigned short gid;            /* 组 ID */
    char home[60];                 /* 主目录路径 */
};

/* ---- 用户数据库运行时状态 ---- */
extern struct user_account user_db[MAX_USERS];
extern int user_count;
extern int current_user_idx;       /* 当前登录用户在 user_db 中的索引 */

/* ---- 用户管理 API ---- */
int  user_load(void);              /* 从磁盘加载用户数据库 */
int  user_save(void);              /* 保存用户数据库到磁盘 */
int  user_login(void);             /* 交互式登录认证 */
int  user_auth(const char *name, const char *pass);  /* 验证用户名密码 */
int  user_add(const char *name, const char *pass);   /* 添加新用户 (root only) */
int  user_passwd(const char *oldpass, const char *newpass); /* 修改当前用户密码 */
int  user_find_by_name(const char *name, unsigned short *uid,
                       unsigned short *gid);         /* 按用户名查找 uid/gid */
const char *user_name_by_uid(unsigned short uid);    /* 按 uid 反查用户名 */
void user_init_default(void);      /* 初始化默认 root 用户 */

#endif /* _USER_H */
