/*
 * disk_io.c — 层 0：磁盘物理 I/O
 *
 * 操作 ./Ext2 虚拟磁盘文件。ctx.fp 由 context.c 管理。
 * 所有 *_read 函数从磁盘加载数据到 ctx 缓冲区；
 * 所有 *_write 函数将 ctx 缓冲区写回磁盘。
 */

#include <stdio.h>
#include "ext2_constants.h"
#include "fs_context.h"
#include "disk_io.h"

/* ================================================================
 * 超级块
 * ================================================================ */

void sb_read(void)
{
    fseek(ctx.fp, DISK_START, SEEK_SET);
    fread(&ctx.sb, SB_SIZE, 1, ctx.fp);
}

void sb_write(void)
{
    fseek(ctx.fp, DISK_START, SEEK_SET);
    fwrite(&ctx.sb, SB_SIZE, 1, ctx.fp);
    fflush(ctx.fp);
}

/* ================================================================
 * 组描述符
 * ================================================================ */

void gd_read(void)
{
    fseek(ctx.fp, GDT_START, SEEK_SET);
    fread(&ctx.gd, GD_SIZE, 1, ctx.fp);
}

void gd_write(void)
{
    fseek(ctx.fp, GDT_START, SEEK_SET);
    fwrite(&ctx.gd, GD_SIZE, 1, ctx.fp);
    fflush(ctx.fp);
}

/* ================================================================
 * 块位图
 * ================================================================ */

void block_bmp_read(void)
{
    fseek(ctx.fp, BLOCK_BITMAP, SEEK_SET);
    fread(ctx.block_bmp, BLOCK_SIZE, 1, ctx.fp);
}

void block_bmp_write(void)
{
    fseek(ctx.fp, BLOCK_BITMAP, SEEK_SET);
    fwrite(ctx.block_bmp, BLOCK_SIZE, 1, ctx.fp);
    fflush(ctx.fp);
}

/* ================================================================
 * inode 位图
 * ================================================================ */

void inode_bmp_read(void)
{
    fseek(ctx.fp, INODE_BITMAP, SEEK_SET);
    fread(ctx.inode_bmp, BLOCK_SIZE, 1, ctx.fp);
}

void inode_bmp_write(void)
{
    fseek(ctx.fp, INODE_BITMAP, SEEK_SET);
    fwrite(ctx.inode_bmp, BLOCK_SIZE, 1, ctx.fp);
    fflush(ctx.fp);
}

/* ================================================================
 * inode 条目（按 inode 号读写，inode 号从 1 开始）
 * ================================================================ */

void inode_read(unsigned short ino)
{
    fseek(ctx.fp, INODE_TABLE + (ino - 1) * INODE_SIZE, SEEK_SET);
    fread(&ctx.inode_cache, INODE_SIZE, 1, ctx.fp);
}

void inode_write(unsigned short ino)
{
    fseek(ctx.fp, INODE_TABLE + (ino - 1) * INODE_SIZE, SEEK_SET);
    fwrite(&ctx.inode_cache, INODE_SIZE, 1, ctx.fp);
    fflush(ctx.fp);
}

/* ================================================================
 * 目录块（按数据块号读写 dir_cache）
 * ================================================================ */

void dir_read(unsigned short block_no)
{
    fseek(ctx.fp, DATA_BLOCK + block_no * BLOCK_SIZE, SEEK_SET);
    fread(ctx.dir_cache, BLOCK_SIZE, 1, ctx.fp);
}

void dir_write(unsigned short block_no)
{
    fseek(ctx.fp, DATA_BLOCK + block_no * BLOCK_SIZE, SEEK_SET);
    fwrite(ctx.dir_cache, BLOCK_SIZE, 1, ctx.fp);
    fflush(ctx.fp);
}

/* ================================================================
 * 数据块（按数据块号读写 data_buf）
 * ================================================================ */

void data_read(unsigned short block_no)
{
    fseek(ctx.fp, DATA_BLOCK + block_no * BLOCK_SIZE, SEEK_SET);
    fread(ctx.data_buf, BLOCK_SIZE, 1, ctx.fp);
}

void data_write(unsigned short block_no)
{
    fseek(ctx.fp, DATA_BLOCK + block_no * BLOCK_SIZE, SEEK_SET);
    fwrite(ctx.data_buf, BLOCK_SIZE, 1, ctx.fp);
    fflush(ctx.fp);
}
