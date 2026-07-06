/*
 * shell.c — 交互循环 & 命令解析
 */

#include <stdio.h>
#include <string.h>
#include "main.h"
#include "fs_context.h"

void shell_run(void)
{
    char command[10], temp[9];

    while (1) {
        printf("%s]#", get_current_path());
        scanf("%s", command);

        if (!strcmp(command, "cd")) {
            scanf("%s", temp);
            cd(temp);
        } else if (!strcmp(command, "mkdir")) {
            scanf("%s", temp);
            mkdir(temp, 2);
        } else if (!strcmp(command, "touch")) {
            scanf("%s", temp);
            cat(temp, 1);
        } else if (!strcmp(command, "rmdir")) {
            scanf("%s", temp);
            rmdir(temp);
        } else if (!strcmp(command, "rm")) {
            scanf("%s", temp);
            del(temp);
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
        } else if (!strcmp(command, "quit")) {
            break;
        } else {
            printf("No this Command,Please check!\n");
        }
        getchar();
    }
}
