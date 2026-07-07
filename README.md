# EXT2 File System — 多平台实现

基于 ext2 文件系统核心逻辑（超级块、inode、位图、目录项、间接块寻址），分别以**用户态模拟**和**内核模块**两种方式实现的跨平台项目。

## 目录结构

```
Linux_FileSystem/
├── Windows & macOS/          # 用户态模拟版本
│   ├── include/              # ext2 数据结构 & API 头文件
│   ├── src/                  # 源文件（2,800+ 行 C）
│   │   ├── disk_io.c         # 层 0: 物理 I/O（fread/fwrite → ./Ext2）
│   │   ├── bitmap.c          # 层 1: 位图分配 / 回收
│   │   ├── directory.c       # 层 2: 目录查找、创建、列出
│   │   ├── file_ops.c        # 层 2: 文件读写、间接块寻址
│   │   ├── file_cmd.c        # 层 3: cp / mv / chmod / chown
│   │   ├── context.c         # 磁盘创建 / 挂载 / 格式化
│   │   ├── user.c            # 用户管理（/etc/passwd 模拟）
│   │   ├── shell.c           # 交互式命令解析
│   │   └── main.c            # 入口
│   ├── Makefile              # GCC/Clang 构建
│   └── README.md             # 详细文档
│
├── ubuntu/                   # [待开发] Linux 内核模块版本
│   └── ...                   # VFS 注册 → 操作真块设备
│
├── .vscode/                  # VS Code IntelliSense 配置
├── .gitignore
└── README.md                 # 本文件
```

## 两个版本的关系

| | Windows & macOS | ubuntu（内核模块） |
|---|---|---|
| **运行层级** | 用户态 | 内核态 |
| **磁盘操作** | `fread`/`fwrite` → 普通文件 `./Ext2` | `sb_bread()` → 块设备 `/dev/loop0` |
| **接口** | 自己的 shell 循环 | Linux VFS（`ls`、`cat` 等系统命令直接可用） |
| **共享逻辑** | ✅ | ✅ 位图、inode、目录项、间接块寻址完全一致 |

两个版本共享相同的 ext2 磁盘格式（超级块布局、inode 结构、目录项格式），同一份 `./Ext2` 虚拟磁盘在不同版本间理论上可以互通。

## 磁盘布局

```
+-----------+-----------+-----------+-----------+-----------+-----------+
| Super     | Group     | Block     | Inode     | Inode     | Data      |
| Block     | Desc      | Bitmap    | Bitmap    | Table     | Blocks    |
| (512B)    | (512B)    | (512B)    | (512B)    | (256KB)   | (~2MB)    |
+-----------+-----------+-----------+-----------+-----------+-----------+
Block 0      Block 1     Block 2     Block 3     Block 4-515   Block 516+
```

- **块大小**: 512 bytes
- **总容量**: ~2.3 MB
- **最大文件**: 134 KB（12 直接块 + 256 一级间接块）
- **最大 inode**: 4096
- **最大文件名**: 8 字符

## 快速开始

各子目录下阅读对应的 README。用户态版本只需 `make run` 即可体验。
