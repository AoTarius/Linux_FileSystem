#ifndef _EXT2_CONSTANTS_H
#define _EXT2_CONSTANTS_H

#define VOLUME_NAME     "EXT2FS"   // 卷名
#define BLOCK_SIZE      512        // 块大小
#define DISK_SIZE       4612       // 磁盘总块数

#define DISK_START      0          // 磁盘开始地址
#define SB_SIZE         32         // 超级块大小：32B

#define GD_SIZE         32         // 组描述符大小：32B
#define GDT_START       (0+512)    // 组描述符起始地址

#define BLOCK_BITMAP    (512+512)  // 块位图起始地址
#define INODE_BITMAP    (1024+512) // inode 位图起始地址

#define INODE_TABLE     (1536+512) // inode节点表起始地址 4*512
#define INODE_SIZE      64         // 每个inode的大小：64B
#define INODE_TABLE_COUNTS 4096    // inode entry 数量

#define DATA_BLOCK      (263680+512)   // 数据块起始地址 4*512+4096*64
#define DATA_BLOCK_COUNTS   4096       // 数据块数量

#define BLOCKS_PER_GROUP    4612       // 每组中的块数

#endif // _EXT2_CONSTANTS_H
