/*
 * bitmap.c — 层 1：块位图 & inode 位图分配 / 回收
 */

#include <stdio.h>
#include "fs_context.h"
#include "disk_io.h"
#include "bitmap.h"

/* ---- 数据块分配 ---- */

unsigned short balloc(void)
{
    unsigned short cur = ctx.last_alloc_block;
    unsigned char con = 128;  /* 1000 0000b */
    int flag = 0;

    if (ctx.gd.bg_free_blocks_count == 0) {
        printf("There is no block to be alloced!\n");
        return 0;
    }
    block_bmp_read();
    cur /= 8;
    while (ctx.block_bmp[cur] == 255) {
        if (cur == 511) cur = 0;
        else cur++;
    }
    while (ctx.block_bmp[cur] & con) {
        con = con / 2;
        flag++;
    }
    ctx.block_bmp[cur] = ctx.block_bmp[cur] + con;
    ctx.last_alloc_block = cur * 8 + flag;

    block_bmp_write();
    ctx.gd.bg_free_blocks_count--;
    gd_write();
    return ctx.last_alloc_block;
}

/* ---- 数据块释放 ---- */

void bfree(unsigned short block_no)
{
    unsigned short idx = block_no / 8;
    block_bmp_read();
    switch (block_no % 8) {
        case 0: ctx.block_bmp[idx] = ctx.block_bmp[idx] & 127;  break;
        case 1: ctx.block_bmp[idx] = ctx.block_bmp[idx] & 191;  break;
        case 2: ctx.block_bmp[idx] = ctx.block_bmp[idx] & 223;  break;
        case 3: ctx.block_bmp[idx] = ctx.block_bmp[idx] & 239;  break;
        case 4: ctx.block_bmp[idx] = ctx.block_bmp[idx] & 247;  break;
        case 5: ctx.block_bmp[idx] = ctx.block_bmp[idx] & 251;  break;
        case 6: ctx.block_bmp[idx] = ctx.block_bmp[idx] & 253;  break;
        case 7: ctx.block_bmp[idx] = ctx.block_bmp[idx] & 254;  break;
    }
    block_bmp_write();
    ctx.gd.bg_free_blocks_count++;
    gd_write();
}

/* ---- inode 分配 ---- */

unsigned short ialloc(void)
{
    unsigned short cur = ctx.last_alloc_inode;
    unsigned char con = 128;
    int flag = 0;

    if (ctx.gd.bg_free_inodes_count == 0) {
        printf("There is no Inode to be alloced!\n");
        return 0;
    }
    inode_bmp_read();
    cur = (cur - 1) / 8;
    while (ctx.inode_bmp[cur] == 255) {
        if (cur == 511) cur = 0;
        else cur++;
    }
    while (ctx.inode_bmp[cur] & con) {
        con = con / 2;
        flag++;
    }
    ctx.inode_bmp[cur] = ctx.inode_bmp[cur] + con;
    ctx.last_alloc_inode = cur * 8 + flag + 1;
    inode_bmp_write();
    ctx.gd.bg_free_inodes_count--;
    gd_write();
    return ctx.last_alloc_inode;
}

/* ---- inode 释放 ---- */

void ifree(unsigned short inode_no)
{
    unsigned short idx = (inode_no - 1) / 8;
    inode_bmp_read();
    switch ((inode_no - 1) % 8) {
        case 0: ctx.inode_bmp[idx] = ctx.inode_bmp[idx] & 127;  break;
        case 1: ctx.inode_bmp[idx] = ctx.inode_bmp[idx] & 191;  break;
        case 2: ctx.inode_bmp[idx] = ctx.inode_bmp[idx] & 223;  break;
        case 3: ctx.inode_bmp[idx] = ctx.inode_bmp[idx] & 239;  break;
        case 4: ctx.inode_bmp[idx] = ctx.inode_bmp[idx] & 247;  break;
        case 5: ctx.inode_bmp[idx] = ctx.inode_bmp[idx] & 251;  break;
        case 6: ctx.inode_bmp[idx] = ctx.inode_bmp[idx] & 253;  break;
        case 7: ctx.inode_bmp[idx] = ctx.inode_bmp[idx] & 254;  break;
    }
    inode_bmp_write();
    ctx.gd.bg_free_inodes_count++;
    gd_write();
}
