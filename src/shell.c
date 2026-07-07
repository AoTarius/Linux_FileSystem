/*
 * shell.c — 交互循环 & 命令解析
 */

#include <stdio.h>
#include <string.h>
#include "main.h"
#include "fs_context.h"
#include "user.h"

void help(void)
{
    printf("\n========== 命令列表 / Commands ==========\n\n");
    printf("  ls                 列出当前目录内容\n");
    printf("  cd <路径>          切换目录 (支持多级路径)\n");
    printf("  mkdir <路径>       创建目录\n");
    printf("  touch <路径>       创建普通文件\n");
    printf("  cat <文件>         打印文件内容\n");
    printf("  cp <源> <目标>     复制文件\n");
    printf("  mv <源> <目标>     移动/重命名文件或目录\n");
    printf("  chmod <八进制> <文件>  修改文件权限\n");
    printf("  chown <用户> <文件>   修改文件所有者\n");
    printf("  open <文件>        打开文件\n");
    printf("  close <文件>       关闭文件\n");
    printf("  read <文件>        读取已打开的文件\n");
    printf("  write <文件>       覆盖写入 (以#结束)\n");
    printf("  write >> <文件>    追加写入 (以#结束)\n");
    printf("  rm <文件>          删除文件\n");
    printf("  rmdir <目录>       删除空目录\n");
    printf("  format             格式化磁盘\n");
    printf("  ckdisk             查看磁盘状态\n");
    printf("  volname [名称]     读取/修改卷标\n");
    printf("  whoami             查看当前用户\n");
    printf("  useradd <用户>     创建新用户 (root)\n");
    printf("  passwd             修改密码\n");
    printf("  login              切换用户\n");
    printf("  help               显示此帮助\n");
    printf("  quit               退出\n");
    printf("\n==========================================\n\n");
}

void shell_run(void)
{
    char command[10], temp[256], temp2[256];

    /* ---- Login ---- */
    if (user_login() != 0) {
        printf("Login failed. Exiting.\n");
        return;
    }

    /* ---- Main loop ---- */
    while (1) {
        printf("[%s@ %s]%c", ctx.current_user, get_current_path(),
               ctx.current_uid == 0 ? '#' : '$');
        scanf("%s", command);

        if (!strcmp(command, "cd")) {
            scanf("%s", temp);
            cd(temp);
        } else if (!strcmp(command, "help")) {
            help();
        } else if (!strcmp(command, "mkdir")) {
            scanf("%s", temp);
            mkdir(temp, 2);
        } else if (!strcmp(command, "touch")) {
            scanf("%s", temp);
            touch(temp, 1);
        } else if (!strcmp(command, "cat")) {
            scanf("%s", temp);
            cat(temp);
        } else if (!strcmp(command, "rmdir")) {
            scanf("%s", temp);
            rmdir(temp);
        } else if (!strcmp(command, "rm")) {
            scanf("%s", temp);
            del(temp);
        } else if (!strcmp(command, "mv")) {
            scanf("%s %s", temp, temp2);
            mv(temp, temp2);
        } else if (!strcmp(command, "cp")) {
            scanf("%s %s", temp, temp2);
            cp(temp, temp2);
        } else if (!strcmp(command, "chmod")) {
            scanf("%s %s", temp, temp2);
            chmod(temp, temp2);
        } else if (!strcmp(command, "chown")) {
            scanf("%s %s", temp, temp2);
            chown(temp, temp2);
        } else if (!strcmp(command, "open")) {
            scanf("%s", temp);
            open_file(temp);
        } else if (!strcmp(command, "close")) {
            scanf("%s", temp);
            close_file(temp);
        } else if (!strcmp(command, "read")) {
            scanf("%s", temp);
            read_file(temp);
        } else if (!strcmp(command, "write")) {
            scanf("%s", temp);
            if (!strcmp(temp, ">>")) {
                scanf("%s ", temp2);
                append(temp2);
            } else {
                scanf(" ");
                write_file(temp);
            }
        } else if (!strcmp(command, "ls")) {
            ls();
        } else if (!strcmp(command, "format")) {
            char tempch;
            printf("Format will erase all the data in the Disk\n");
            printf("Are you sure?y/n:\n");
            fflush(stdin);
            scanf(" %c", &tempch);
            if (tempch == 'Y' || tempch == 'y')
                format();
            else
                printf("Format Disk canceled\n");
        } else if (!strcmp(command, "ckdisk")) {
            check_disk();
        } else if (!strcmp(command, "volname")) {
            int c = getchar();
            if (c == '\n') {
                ungetc(c, stdin);
                volname(NULL);
            } else {
                ungetc(c, stdin);
                scanf("%15s", temp);
                volname(temp);
            }
        } else if (!strcmp(command, "whoami")) {
            printf("User: %s  (uid=%d, gid=%d)\n",
                   ctx.current_user, ctx.current_uid, ctx.current_gid);
        } else if (!strcmp(command, "useradd")) {
            scanf("%s", temp);
            printf("Password for %s: ", temp);
            scanf("%31s", temp2);
            user_add(temp, temp2);
        } else if (!strcmp(command, "passwd")) {
            char oldpass[32], newpass[32];
            if (ctx.current_uid != 0) {
                printf("Old password: ");
                scanf("%31s", oldpass);
            } else {
                oldpass[0] = '\0';
            }
            printf("New password: ");
            scanf("%31s", newpass);
            user_passwd(oldpass, newpass);
        } else if (!strcmp(command, "login")) {
            /* 重新登录（切换用户） */
            user_login();
        } else if (!strcmp(command, "quit")) {
            user_save();  /* 退出前保存用户数据 */
            break;
        } else {
            printf("No this Command,Please check!\n");
            while (getchar() != '\n');     /* 跳过本行剩余内容 */
            continue;
        }
        getchar();
    }
}
