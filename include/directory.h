#ifndef _DIRECTORY_H
#define _DIRECTORY_H

/*
 * directory.h — 层 2：目录项操作
 */

/* 在当前目录中搜索文件名为 name、类型为 file_type 的目录项。
 * 找到返回 1 并填充 *inode_no / *block_no / *entry_no；未找到返回 0 */
int dir_lookup(const char *name, int file_type,
               unsigned short *inode_no,
               unsigned short *block_no,
               unsigned short *entry_no);

/* 为新创建的文件或目录初始化 inode（分配数据块、设置 . 和 .. 等） */
void dir_entry_init(unsigned short ino, unsigned short name_len, int type);

/* 检查 inode 是否在文件打开表中 */
int file_is_open(unsigned short ino);

/* 列出当前目录内容 */
void dir_list(void);

/* 多级目录导航。
 * 支持 ~ 绝对路径、相对路径、. 和 .. 组件。
 * 成功返回 0，失败返回 -1。不打印错误信息（由调用者处理）。 */
int dir_navigate(const char *path);

#endif // _DIRECTORY_H
