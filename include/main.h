#ifndef _MAIN_H
#define _MAIN_H

/*
 * main.h — 公共命令接口
 *
 * 供 shell.c 调用的所有用户命令。
 * 上下文管理函数（fs_init / fs_shutdown / get_current_path / format / check_disk）
 * 在 fs_context.h 中声明。
 */

/* 目录操作 */
void cd(char tmp[9]);
void mkdir(char tmp[9], int type);
void rmdir(char tmp[9]);
void ls(void);

/* 文件操作 */
void cat(char tmp[9], int type);
void del(char tmp[9]);
void open_file(char tmp[9]);
void close_file(char tmp[9]);
void read_file(char tmp[9]);
void write_file(char tmp[9]);

/* 其他 */
void help(void);

#endif // _MAIN_H