/*
 * shell.c — 交互循环 & 命令解析
 */

#include <stdio.h>
#include <string.h>
#include "main.h"
#include "fs_context.h"
#include "user.h"

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
            scanf("%s\n", temp);
            write_file(temp);
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
