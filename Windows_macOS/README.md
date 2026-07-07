# EXT2 File System Simulator — Windows & macOS 用户态版本

用 C 语言实现的用户态 ext2 文件系统模拟器。使用单个普通文件 `./Ext2` 作为虚拟磁盘，提供交互式 shell 操作。

## 快速开始

```bash
make           # 编译
make run       # 编译并运行
make clean     # 清理编译产物
make distclean # 清理编译产物 + 虚拟磁盘（彻底重置）
```

首次运行自动创建虚拟磁盘，默认用户 `root` / `root`。

## 环境要求

- GCC / Clang
- Windows (MinGW / WSL) 或 macOS

## 原理

```
┌─────────────────────────┐
│  shell.c (交互命令)      │  ← 你在这里
├─────────────────────────┤
│  file_ops / directory   │  ← ext2 逻辑（inode、目录、权限）
├─────────────────────────┤
│  bitmap.c               │  ← 位图分配回收
├─────────────────────────┤
│  disk_io.c              │  ← fread/fwrite 读写 ./Ext2 普通文件
└─────────────────────────┘
         │
    ./Ext2（一个普通文件，模拟磁盘）
```

## 与 Linux 内核版本的关系

本文件夹是**用户态模拟版本**，底层用 `fread`/`fwrite` 操作普通文件。对应的 Linux 内核模块版本（通过 VFS 注册，操作真块设备）在并列的 `ubuntu/` 目录中。两个版本共享相同的 ext2 核心逻辑（位图、inode、目录结构），仅 I/O 层和接口层不同。

## 命令列表

| 命令 | 说明 |
|------|------|
| `ls` | 列出目录 |
| `cd` | 切换目录 |
| `mkdir` | 创建目录 |
| `touch` | 创建文件 |
| `open` / `close` | 打开 / 关闭文件 |
| `read` | 读取文件 |
| `write` / `write >>` | 覆盖 / 追加写入 |
| `cat` | 打印文件内容 |
| `cp` / `mv` | 复制 / 移动 |
| `rm` / `rmdir` | 删除文件 / 目录 |
| `chmod` / `chown` | 修改权限 / 所有者 |
| `useradd` / `passwd` / `login` | 用户管理 |
| `ckdisk` / `format` / `volname` | 磁盘工具 |
| `help` | 全部命令 |
| `quit` | 退出 |

输入 `help` 查看完整用法。
