#ifndef _EXT2_CONSTANTS_H
#define _EXT2_CONSTANTS_H

/*
 * ═══════════════════════════════════════════════════════════════
 *  命名约定 / Naming Convention
 * ═══════════════════════════════════════════════════════════════
 *
 *  _SIZE        = 字节大小  (如 SB_SIZE=32, INODE_SIZE=64)
 *  _COUNTS      = 条目/块数量 (如 INODE_TABLE_COUNTS=4096, DATA_BLOCK_COUNTS=4096)
 *  _BLOCKS      = 块数       (如 USER_AREA_BLOCKS=10, BLOCKS_PER_GROUP=4612)
 *  无后缀宏      = 磁盘字节偏移 (如 GDT_START=512, DATA_BLOCK=264192)
 *
 *  ⚠️ 绝对不要把 _COUNTS/_BLOCKS 直接当字节偏移用！
 *     转换公式: byte_offset = BASE + count * BLOCK_SIZE
 *     错误示例:  BASE + count          ← 块数和字节混加，必出 bug
 *     正确示例:  BASE + count * 512    ← 或 count * BLOCK_SIZE
 * ═══════════════════════════════════════════════════════════════
 */

#define VOLUME_NAME     "EXT2FS"   // 卷名
#define BLOCK_SIZE      512        // 块大小 (字节)

#define DISK_SIZE       4612       // 磁盘总块数 (_COUNTS 语义)

#define DISK_START      0          // 磁盘起始字节偏移
#define SB_SIZE         32         // 超级块大小 (字节)

#define GD_SIZE         32         // 组描述符大小 (字节)
#define GDT_START       (0+512)    // 组描述符起始字节偏移 (= 块1)

#define BLOCK_BITMAP    (512+512)  // 块位图起始字节偏移 (= 块2)
#define INODE_BITMAP    (1024+512) // inode 位图起始字节偏移 (= 块3)

#define INODE_TABLE     (1536+512) // inode 表起始字节偏移 (= 块4)
#define INODE_SIZE      64         // 每个 inode 大小 (字节)
#define INODE_TABLE_COUNTS 4096    // inode 条目数 (_COUNTS 语义)

#define DATA_BLOCK      (263680+512)      // 数据块起始字节偏移 (= 块516)
#define DATA_BLOCK_COUNTS   4096          // 数据块数量 (_COUNTS 语义)

#define BLOCKS_PER_GROUP    4612          // 每组块数 (_BLOCKS 语义)

/* ---- 用户管理区域（磁盘末尾的预留数据块）---- */
#define USER_AREA_BLOCKS    10            // 用户区域块数 (_BLOCKS 语义)
#define USER_AREA_START     (DATA_BLOCK + (DATA_BLOCK_COUNTS - USER_AREA_BLOCKS) * BLOCK_SIZE)
                                          // 起始字节偏移 = 数据区基址 + (可分配块数 - 保留块数) × 块大小

/* ---- Unix rwxrwxrwx 权限位 ---- */
#define S_IRUSR  0400   /* owner read    */
#define S_IWUSR  0200   /* owner write   */
#define S_IXUSR  0100   /* owner execute */
#define S_IRGRP  0040   /* group read    */
#define S_IWGRP  0020   /* group write   */
#define S_IXGRP  0010   /* group execute */
#define S_IROTH  0004   /* other read    */
#define S_IWOTH  0002   /* other write   */
#define S_IXOTH  0001   /* other execute */

#define DEFAULT_DIR_MODE   0755   /* rwxr-xr-x */
#define DEFAULT_FILE_MODE  0644   /* rw-r--r-- */

/* 从 i_mode 提取权限类型：owner / group / other */
#define PERM_OWNER(mode, bit)  ((mode) & (bit))
#define PERM_GROUP(mode, bit)  ((mode) & ((bit) >> 3))
#define PERM_OTHER(mode, bit)  ((mode) & ((bit) >> 6))

#endif // _EXT2_CONSTANTS_H
