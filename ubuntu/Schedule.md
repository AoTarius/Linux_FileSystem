# Schedule — ubuntu 内核模块开发计划

| 阶段 | 状态 | 文件 | 内容 | 交付产物 | 验证方式 |
|:---:|:---:|------|------|------|------|
| **1** | ⏳ 待完成 | `Makefile`<br>`ext2_sim_disk.h`<br>`ext2_sim_fs.h` | 项目骨架：Kbuild 编译系统、磁盘数据结构（`__le16`标注）、内存结构（`sbi`、`inode_info`）、所有宏定义 | 三个文件编译通过 (`make` 零错误) | `make` → 生成 `ext2_sim.o`（未链接的空模块） |
| **2** | ⏳ 待完成 | `super.c` | 模块入口/出口、`fill_super`（读超级块→校验→构建VFS sb→读根inode→`d_make_root`）、`put_super`（释放bh→kfree sbi）、`alloc_inode`/`free_inode`、`statfs` | 可加载模块 + 可挂载 | `insmod` → `mount` → `ls /mnt/ext2` 看到 `.` 和 `..` |
| **3** | ⏳ 待完成 | `balloc.c` | `balloc`（扫描块位图→置位→递减计数）、`bfree`（清零→递增计数）、`ialloc`（扫描inode位图→置位）、`ifree`（清零→递增计数） | 完整的位图分配/释放 | 被步骤 4、5 间接验证（创建文件不崩溃即通过） |
| **4** | ⏳ 待完成 | `inode.c` | `iget`（从磁盘读inode→填充VFS inode→设置`i_op`/`i_fop`）、`write_inode`（VFS inode→磁盘）、`lookup`（目录查找→`d_splice_alias`）、`create`（ialloc→初始化磁盘inode→add_entry→`d_instantiate_new`） | 可创建文件 | `touch /mnt/ext2/f1` → `ls /mnt/ext2` 看到 f1 |
| **5** | ⏳ 待完成 | `dir.c` | `find_entry`（遍历目录块→按名匹配）、`add_entry`（找空槽→填入→mark_dirty）、`remove_entry`（定位→inode置零→mark_dirty） | 完整的目录操作 | `mkdir /mnt/ext2/d1` → `ls /mnt/ext2` 看到 d1 |
| **6** | ⏳ 待完成 | `file.c` | `file_read`（逻辑块→物理块→`sb_bread`→`copy_to_user`）、`file_write`（`copy_from_user`→`sb_bread`→扩大i_size）、`readdir`（遍历目录块→`dir_emit`）、`getattr`（`generic_fillattr(idmap, ...)`） | 可读写文件 + 目录列表 | `echo hi > /mnt/ext2/f1 && cat /mnt/ext2/f1` → 输出 hi |
| **7** | ⏳ 待完成 | `inode.c` (补充) | `unlink`（目录移除条目→链接数--）、`rmdir`（检查空目录→移除条目→链接数--） | 可删除文件/目录 | `rm /mnt/ext2/f1` → 消失；`rmdir /mnt/ext2/d1` → 消失 |
| **8** | ⏳ 待完成 | `inode.c` (补充) | `evict_inode`（释放所有数据块：直接+一级间接+二级间接+三级间接→ifree） | 删除后空间回收 | `df /mnt/ext2` 删除前后空闲块数正确变化 |
| **9** | ⏳ 待完成 | `file.c` (补充) | `get_block`（逻辑块号→物理块号：直接块12个、一级间接、二级间接、三级间接；支持 `allocate` 自动扩展） | 支持大文件 | `dd if=/dev/urandom of=/mnt/ext2/big bs=512 count=200` → cat 校验 |
| **10** | ⏳ 待完成 | 全部 | 时间戳更新（创建设 atime/ctime/mtime；读更新 atime；写更新 mtime/ctime）、`mkdir` 补充（`.`/`..`初始化+`bg_used_dirs_count`）、完善 `statfs`、卸载再挂载数据持久化 | 功能完整 | 完整操作测试 + `dmesg` 无异常；卸载→挂载→数据仍在 |
| **11** | ⏳ 待完成 | 全部 | 压力验证：100个小文件创建/读取、长文件名边界、32条目满块目录自动扩展、空镜像首次挂载自动格式化、反复挂载卸载 10 次 | 稳定版本 | 全部通过，无 oops/panic/内存泄漏 |
