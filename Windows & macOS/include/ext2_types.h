#ifndef _EXT2_TYPES_H
#define _EXT2_TYPES_H

struct super_block // 32 B
{
    char sb_volume_name[16];            // 文件系统名
    unsigned short sb_disk_size;        // 磁盘总大小
    unsigned short sb_blocks_per_group; // 每组中的块数
    unsigned short sb_size_per_block;   // 块大小
    unsigned short sb_free_blocks_count; // 空闲块总数
    unsigned short sb_free_inodes_count; // 空闲 inode 总数
    char sb_pad[6];                     // 填充
};

struct group_desc // 32 B
{
    char bg_volume_name[16];       // 文件系统名
    unsigned short bg_block_bitmap;    // 块位图起始块号
    unsigned short bg_inode_bitmap;    // inode位图起始块号
    unsigned short bg_inode_table;     // inode表起始块号
    unsigned short bg_free_blocks_count; // 本组中空闲块个数
    unsigned short bg_free_inodes_count; // 本组中空闲inode个数
    unsigned short bg_used_dirs_count;   // 本组中分配的目录的节点数
    char bg_pad[4];                // 填充(0xff)
};

struct inode // 64 B
{
    unsigned short i_mode;         // 文件类型及访问权限
    unsigned short i_blocks;       // 文件占用的数据块数
    unsigned short i_uid;          // 文件拥有者标识号
    unsigned short i_gid;          // 文件用户组标识号
    unsigned short i_links_count;  // 文件硬链接计数
    unsigned short i_flags;        // 打开文件的方式
    unsigned int i_size;           // 文件或目录大小(单位 byte)
    unsigned int i_atime;          // 访问时间
    unsigned int i_ctime;          // 创建时间
    unsigned int i_mtime;          // 修改时间
    unsigned int i_dtime;          // 删除时间
    unsigned short i_block[15];    // 块指针: [0..11]直接 [12]一级间接 [13]二级 [14]三级
    char i_pad[2];                 // 填充(0xff)
};

struct dir_entry // 16B
{
    unsigned short inode;          // inode节点号
    unsigned short rec_len;        // 目录项长度
    unsigned short name_len;       // 文件名长度
    char file_type;                // 文件类型(1 普通文件 2 目录.. )
    char name[9];                  // 文件名
};

#endif // _EXT2_TYPES_H
