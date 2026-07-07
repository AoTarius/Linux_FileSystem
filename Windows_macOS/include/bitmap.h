#ifndef _BITMAP_H
#define _BITMAP_H

/*
 * bitmap.h — 层 1：块位图 & inode 位图分配
 */

/* 数据块分配 / 释放，返回块号 */
unsigned short balloc(void);
void bfree(unsigned short block_no);

/* inode 分配 / 释放，返回 inode 号 */
unsigned short ialloc(void);
void ifree(unsigned short inode_no);

#endif // _BITMAP_H
