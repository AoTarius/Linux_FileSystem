#ifndef _DISK_IO_H
#define _DISK_IO_H

/*
 * disk_io.h — 层 0：磁盘物理 I/O
 *
 * 所有读写函数操作 ctx 中的缓冲区，读写 ./Ext2 虚拟磁盘。
 * ctx.fp 由 context.c 管理，此处不负责打开/关闭。
 */

/* 超级块 */
void sb_read(void);
void sb_write(void);

/* 组描述符 */
void gd_read(void);
void gd_write(void);

/* 块位图 */
void block_bmp_read(void);
void block_bmp_write(void);

/* inode 位图 */
void inode_bmp_read(void);
void inode_bmp_write(void);

/* inode 条目（按 inode 号读写） */
void inode_read(unsigned short ino);
void inode_write(unsigned short ino);

/* 目录块（按数据块号读写 dir_cache） */
void dir_read(unsigned short block_no);
void dir_write(unsigned short block_no);

/* 数据块（按数据块号读写 data_buf） */
void data_read(unsigned short block_no);
void data_write(unsigned short block_no);

#endif // _DISK_IO_H
