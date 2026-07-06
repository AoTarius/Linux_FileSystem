/*
 * main.c — 程序入口
 *
 * 初始化文件系统 → 进入交互 shell → 安全关闭。
 */

#include "fs_context.h"

extern void shell_run(void);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    fs_init();
    shell_run();
    fs_shutdown();

    return 0;
}