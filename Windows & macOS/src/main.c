/*
 * main.c — 程序入口
 *
 * 初始化文件系统 → 用户登录 → 交互 shell → 保存用户数据 → 安全关闭。
 */

#include "fs_context.h"
#include "user.h"

extern void shell_run(void);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    fs_init();
    shell_run();
    user_save();   /* 退出前保存用户数据到磁盘 */
    fs_shutdown();

    return 0;
}