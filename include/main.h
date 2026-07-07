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
void cd(const char *tmp);
void mkdir(const char *tmp, int type);
void rmdir(const char *tmp);
void ls(void);

/* 文件操作 */
void touch(const char *tmp, int type);
void del(const char *tmp);
void open_file(const char *tmp);
void close_file(const char *tmp);
void read_file(const char *tmp);
void write_file(const char *tmp);
void mv(const char *src, const char *dst);
void cp(const char *src, const char *dst);
void chmod(const char *mode_str, const char *path);
void chown(const char *user_str, const char *path);

/* 其他 */
void help(void);

#endif // _MAIN_H