# EXT2 File System — Ubuntu 内核模块版本

在 Linux VFS（虚拟文件系统）框架下注册的 ext2 文件系统内核模块。直接操作块设备（如 `/dev/loopN`），被系统原生的 `ls`、`cat`、`touch` 等命令调用，而非自建 shell。

## 与 Windows_macOS 版本的关系

两个文件夹是**完全独立的实现**，没有任何代码共享。仅目标一致——都能操作 ext2 格式的磁盘镜像。

```
Windows_macOS/                              ubuntu/（本目录）
─────────────────────────                   ──────────────────────────
实现方式：纯用户态 C 程序                      实现方式：Linux 内核模块 (.ko)
I/O 手段：fread/fwrite 读写普通文件            I/O 手段：sb_bread() 操作块设备
接口：自己写的 shell 循环                     接口：注册到内核 VFS，被系统所有程序调用
输出：printf 到终端                          输出：printk 到 dmesg
─────────────────────────                   ──────────────────────────
         │                                          │
         │     各自独立实现，无代码依赖关系              │
         │                                          │
         └─────── 实现效果等价 ──────────────────────┘
                都能 ls / mkdir / touch / cat / rm ...
                都遵循 ext2 磁盘布局：超级块 → inode → 数据块
```

> **关键区别**：Windows_macOS 是自己写代码解析 ext2 结构、自己写 shell；ubuntu 是通过向 Linux 内核注册回调函数，内核替你调度，系统命令 **不需要你写解析逻辑** 就能直接操作你的文件系统。

## 环境要求

- Ubuntu 22.04 / 24.04（VMware 或实机，不能用 WSL2）
- 内核开发工具链：

```bash
sudo apt install build-essential linux-headers-$(uname -r)
```

## 目录结构（规划）

```
ubuntu/
├── include/                    # 头文件
│   ├── ext2_sim.h              # 磁盘数据结构（__le16 标注，内核规范）
│   └── ext2_sim_fs.h           # 内存结构（sbi、inode_info）
├── src/                        # 源文件
│   ├── super.c                 # mount / unmount / statfs → fill_super / put_super
│   ├── inode.c                 # inode 操作 → lookup, create, mkdir, unlink, rmdir
│   ├── file.c                  # 文件操作 → read, write, readdir
│   ├── balloc.c                # 位图管理 → balloc / bfree / ialloc / ifree
│   └── dir.c                   # 目录项辅助 → find_entry / add_entry / remove_entry
├── Makefile                    # Kbuild（内核模块构建系统）
└── README.md                   # 本文件
```

## 两套实现的技术对比（无代码共享）

| Windows_macOS（用户态自建） | ubuntu（内核 VFS 回调） |
|---|---|
| `FILE *fp` → 全局单例 `ctx` | `struct super_block *sb` → `s_fs_info` 每挂载点独立 |
| `fseek / fread / fwrite` | `sb_bread(sb, block_no)` → `struct buffer_head` |
| `char buf[512]` 栈数组 | `bh->b_data`（buffer_head 管理的页缓存） |
| `printf` | `printk(KERN_INFO ...)` |
| `main()` → 自循环解析命令 | VFS 回调注册 → 被任何进程调用 |
| `ctx.fopen_table[16]` 文件打开表 | 内核 `struct file` 管理，无需自己维护 |
| 自己实现 `user_db` 用户系统 | 宿主 Linux uid/gid，VFS 自动做权限检查 |

## 关键技术点

### 1. 块 I/O：`sb_bread()`

```c
struct buffer_head *bh = sb_bread(sb, block_no);
// 数据在 bh->b_data 中（512 字节）
memcpy(dest, bh->b_data + offset, len);
mark_buffer_dirty(bh);  // 修改后标记脏
brelse(bh);             // 释放引用
```

### 2. VFS 回调注册

```
用户调用:  ls /mnt/ext2
    │
    ▼
VFS:       iterate_dir()
    │
    ▼
我们的:    ext2_sim_readdir()    ← dir.c 里实现
```

```
用户调用:  touch /mnt/ext2/hello
    │
    ▼
VFS:       vfs_create()
    │
    ▼
我们的:    ext2_sim_create()     ← inode.c 里实现
    │
    ▼
           ext2_sim_ialloc()     ← balloc.c 里实现（分配 inode）
           ext2_sim_add_entry()  ← dir.c 里实现（写入目录项）
```

### 3. 需要阅读的内核源码

| 内核文件 | 对应我们做的事 |
|---|---|
| `fs/ext2/super.c` — `ext2_fill_super()` | 挂载流程，读取超级块 → 设置 VFS sb |
| `fs/ext2/inode.c` — `ext2_get_inode()` | 从磁盘 inode 填充 VFS inode |
| `fs/ext2/balloc.c` — `ext2_new_block()` | 位图分配（内核里怎么做块分配） |
| `fs/ext2/namei.c` — `ext2_lookup()` `ext2_create()` | 目录查找和文件创建——VFS 回调的标准写法 |

下载内核源码：`sudo apt install linux-source`

## 开发流程

```bash
# 1. 创建虚拟磁盘镜像
dd if=/dev/zero of=./Ext2 bs=512 count=4612

# 2. 挂载为循环设备（用普通文件模拟块设备）
LOOP=$(sudo losetup -f)          # 自动获取空闲 loop 设备
sudo losetup $LOOP ./Ext2       # 绑定

# 3. 加载内核模块并挂载（模块内部应实现 mkfs，首次挂载自动格式化）
sudo insmod ext2_sim.ko
sudo mount -t ext2sim $LOOP /mnt/ext2

# 5. 用系统命令操作
ls /mnt/ext2
cat /mnt/ext2/readme.txt

# 6. 卸载
sudo umount /mnt/ext2
sudo rmmod ext2_sim
sudo losetup -d $LOOP

# 7. 查看内核日志
dmesg | tail -50
```

## 完整体验流程（当前实现进度：阶段 1-8 ✅）

以下脚本从零开始搭建文件系统并演示所有已实现功能。
建议逐段复制到终端执行，观察每一步输出。

### 第一步：搭建

```bash
# 在 ubuntu/ 目录下执行
cd /home/hollow/Linux_FileSystem/ubuntu

# 清理（如果之前有残留）
sudo umount /mnt/ext2 2>/dev/null
sudo rmmod ext2_sim 2>/dev/null
LOOP=$(losetup -n -O NAME -l | head -1 2>/dev/null) && sudo losetup -d $LOOP 2>/dev/null

# 编译
make

# 创建 2MB 磁盘镜像
dd if=/dev/zero of=./test.img bs=512 count=4612

# 获取空闲 loop 设备并绑定
LOOP=$(sudo losetup -f)
echo "Using loop device: $LOOP"
sudo losetup $LOOP ./test.img

# 加载模块 + 挂载（首次自动格式化）
sudo insmod ext2_sim.ko
sudo mount -t ext2sim $LOOP /mnt/ext2

# 开放权限方便体验
sudo chmod 777 /mnt/ext2
```

### 第二步：文件操作

```bash
# 查看空目录
ls -la /mnt/ext2

# 创建空文件(文件名有长度限制，需要短于8字符)
touch /mnt/ext2/readme
ls -la /mnt/ext2

# 写入内容
echo "Hello EXT2 File System!" > /mnt/ext2/readme
cat /mnt/ext2/readme

# 覆盖写入
echo "Hello again!" > /mnt/ext2/readme
cat /mnt/ext2/readme

# 追加写入
echo "This is line two." >> /mnt/ext2/readme
cat /mnt/ext2/readme

# 查看属性（inode 号、权限、时间戳、链接数）
stat /mnt/ext2/readme
```

### 第三步：目录操作

```bash
# 创建目录 + 嵌套
mkdir /mnt/ext2/docs
mkdir /mnt/ext2/docs/sub
touch /mnt/ext2/docs/sub/note.txt

# 查看目录树
ls -laR /mnt/ext2

# 拒绝删除非空目录
rmdir /mnt/ext2/docs       # 应报错 "Directory not empty"
```

### 第四步：删除与空间回收

```bash
# 查看当前磁盘使用
df /mnt/ext2

# 删除文件
rm /mnt/ext2/docs/sub/note.txt
rm /mnt/ext2/readme

# 删除空目录
rmdir /mnt/ext2/docs/sub
rmdir /mnt/ext2/docs

# 空间已被回收
df /mnt/ext2
ls -la /mnt/ext2
```

### 第五步：大数据块读写

```bash
# 写入跨越多个数据块的文件（3 × 512 字节）
dd if=/dev/urandom of=/mnt/ext2/bigfile bs=512 count=3

# 验证数据完整性
dd if=/mnt/ext2/bigfile of=/tmp/readback.bin bs=512 count=3
cmp /mnt/ext2/bigfile /tmp/readback.bin && echo "DATA MATCH ✓"

# 查看文件大小（1536 字节，Blocks=3）
stat /mnt/ext2/bigfile
```

### 第六步：持久化验证

```bash
# 卸载
# cd ~ 如果在mnt目录内就先出来
sudo umount /mnt/ext2

# 重新挂载（使用同一个 loop 设备 $LOOP）
sudo mount -t ext2sim $LOOP /mnt/ext2

# 数据全部还在
ls -la /mnt/ext2
cat /mnt/ext2/bigfile | wc -c    # 应显示 1536
```

### 第七步：查看内核日志 & 清理

```bash
# 查看模块运行日志
sudo dmesg | grep ext2sim: | tail -20

# 完全清理
sudo umount /mnt/ext2
sudo rmmod ext2_sim
sudo losetup -d $LOOP
```

### 功能清单速查

| 功能 | 命令 | 状态 |
|------|------|:---:|
| 挂载（自动格式化） | `mount -t ext2sim` | ✅ |
| 列出目录 | `ls` | ✅ |
| 创建文件 | `touch` | ✅ |
| 写入文件 | `echo > file` | ✅ |
| 读取文件 | `cat` | ✅ |
| 追加写入 | `echo >> file` | ✅ |
| 创建目录 | `mkdir` | ✅ |
| 删除文件 | `rm` | ✅ |
| 删除空目录 | `rmdir` | ✅ |
| 非空目录保护 | `rmdir nonempty/` | ✅ |
| 文件属性 | `stat` | ✅ |
| 磁盘空间 | `df` | ✅ |
| 卸载重挂载持久化 | `umount` → `mount` | ✅ |
| 删除后空间回收 | `rm` → `df` | ✅ |
| 大文件读写 (>512B) | `dd` → `cmp` | ✅ |
| 大文件 (>6KB) | — | ⏳ 阶段 9 |
