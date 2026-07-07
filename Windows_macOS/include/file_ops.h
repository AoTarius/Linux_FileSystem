#ifndef _FILE_OPS_H
#define _FILE_OPS_H

/*
 * file_ops.h — 层 2：文件操作（open / close / read / write / delete / create）
 */

void file_create(const char *name, int type);
void file_delete(const char *name);
void file_open(const char *name);
void file_close(const char *name);
void file_read(const char *name);
void file_write(const char *name);

/* 间接块寻址（供 cp/mv 等命令使用） */
unsigned short get_file_block(unsigned short ino, unsigned int logical,
                              int allocate);
void free_file_blocks(unsigned short ino);

#endif // _FILE_OPS_H
