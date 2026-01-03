#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include "smartfs_types.h"
#include <sys/types.h>
#include <sys/stat.h>
// [新增] 引入版本管理模块
#include "versioning/version_mgr.h"
#include "versioning/version_utils.h"

// 全局变量
static int disk_fd = -1;
static super_block_t sb;
static const char *disk_path = "test.img";

// =========================================================
// Level 1: 基础磁盘操作 (必须放在最前面)
// =========================================================

// 读取 Inode 信息
void load_inode(uint64_t inode_id, inode_t *inode) {
    off_t offset = sb.inode_area_start * BLOCK_SIZE + inode_id * sizeof(inode_t);
    lseek(disk_fd, offset, SEEK_SET);
    read(disk_fd, inode, sizeof(inode_t));
}

// 保存 Inode 信息
void save_inode(inode_t *inode) {
    off_t offset = sb.inode_area_start * BLOCK_SIZE + inode->inode_id * sizeof(inode_t);
    lseek(disk_fd, offset, SEEK_SET);
    write(disk_fd, inode, sizeof(inode_t));
}

// 保存超级块
void save_superblock() {
    lseek(disk_fd, 0, SEEK_SET);
    write(disk_fd, &sb, sizeof(super_block_t));
}

// 分配新的 Inode
uint64_t allocate_inode() {
    inode_t node;
    // 简单暴力搜索，实际应使用位图
    for (uint64_t i = 1; i < 1024; i++) { 
        load_inode(i, &node);
        if (node.mode == 0) { 
            return i;
        }
    }
    return 0;
}

// 分配新的数据块
uint64_t allocate_block() {
    static uint64_t last_alloc = 0;
    uint64_t start_block = sb.data_area_start;
    
    if (last_alloc == 0) last_alloc = start_block + 1;

    if (sb.free_blocks == 0) return 0;

    sb.free_blocks--;
    save_superblock();
    return last_alloc++; 
}

// =========================================================
// Level 2: 目录与查找助手 (依赖 Level 1)
// =========================================================

// 通用查找函数：在指定的 parent_inode_id 中查找名字为 name 的子项
// 返回子项的 inode_id，找不到返回 0
uint64_t find_entry_in_dir(uint64_t parent_inode_id, const char *name) {
    uint64_t phys_block;
    
    // 1. 确定去哪里读数据
    if (parent_inode_id == 0) {
        // 根目录
        phys_block = sb.data_area_start;
    } else {
        // 子目录：先读 Inode 找到数据块位置
        inode_t parent_inode;
        load_inode(parent_inode_id, &parent_inode);
        phys_block = parent_inode.versions[0].block_list_start_index;
    }

    // 2. 读取目录内容
    char buffer[BLOCK_SIZE];
    smartfs_dir_entry_t *entries = (smartfs_dir_entry_t *)buffer;
    
    off_t offset = phys_block * BLOCK_SIZE;
    lseek(disk_fd, offset, SEEK_SET);
    if (read(disk_fd, entries, BLOCK_SIZE) != BLOCK_SIZE) return 0;

    // 3. 遍历查找
    int max_entries = BLOCK_SIZE / sizeof(smartfs_dir_entry_t);
    for (int i = 0; i < max_entries; i++) {
        if (entries[i].is_valid && strcmp(entries[i].name, name) == 0) {
            return entries[i].inode_no;
        }
    }
    return 0; // 没找到
}

// 在父目录中添加一个文件条目
int add_dir_entry(uint64_t parent_inode_id, const char *name, uint64_t child_inode_id) {
    inode_t parent;
    uint64_t phys_block;

    if (parent_inode_id == 0) {
        phys_block = sb.data_area_start;
    } else {
        load_inode(parent_inode_id, &parent);
        phys_block = parent.versions[0].block_list_start_index;
    }

    char buffer[BLOCK_SIZE]; 
    smartfs_dir_entry_t *entries = (smartfs_dir_entry_t *)buffer;
    
    off_t offset = phys_block * BLOCK_SIZE;
    lseek(disk_fd, offset, SEEK_SET);
    read(disk_fd, entries, BLOCK_SIZE);

    int max_entries = BLOCK_SIZE / sizeof(smartfs_dir_entry_t);
    for (int i = 0; i < max_entries; i++) {
        if (!entries[i].is_valid) {
            strncpy(entries[i].name, name, MAX_FILENAME);
            entries[i].inode_no = child_inode_id;
            entries[i].is_valid = 1;
            
            lseek(disk_fd, offset, SEEK_SET);
            write(disk_fd, entries, BLOCK_SIZE);
            return 0;
        }
    }
    return -ENOSPC; 
}

// 从目录中移除条目
int remove_dir_entry(uint64_t parent_inode_id, const char *name) {
    inode_t parent;
    uint64_t phys_block;

    if (parent_inode_id == 0) {
        phys_block = sb.data_area_start;
    } else {
        load_inode(parent_inode_id, &parent);
        phys_block = parent.versions[0].block_list_start_index;
    }

    char buffer[BLOCK_SIZE]; 
    smartfs_dir_entry_t *entries = (smartfs_dir_entry_t *)buffer;
    
    off_t offset = phys_block * BLOCK_SIZE;
    lseek(disk_fd, offset, SEEK_SET);
    read(disk_fd, entries, BLOCK_SIZE);

    int max_entries = BLOCK_SIZE / sizeof(smartfs_dir_entry_t);
    for (int i = 0; i < max_entries; i++) {
        if (entries[i].is_valid && strcmp(entries[i].name, name) == 0) {
            entries[i].is_valid = 0; 
            entries[i].inode_no = 0;
            memset(entries[i].name, 0, MAX_FILENAME);
            
            lseek(disk_fd, offset, SEEK_SET);
            write(disk_fd, entries, BLOCK_SIZE);
            return 0; 
        }
    }
    return -ENOENT; 
}

// 回收 Inode
void free_inode(uint64_t inode_id) {
    inode_t inode;
    load_inode(inode_id, &inode);
    inode.mode = 0; // 标记为空闲
    save_inode(&inode);
    printf("DEBUG: Inode %lu freed.\n", inode_id);
}

// ---------------------------------------------------------
// 辅助工具：解析路径并找到对应的 Inode ID
// 支持 /file 和 /dir/file
// ---------------------------------------------------------
// [修改] 升级后的路径解析 (支持 @ 版本后缀)
uint64_t resolve_path_to_inode(const char *path) {
    // 1. 先分离版本号
    char real_path[MAX_FILENAME];
    int version_id_dummy;
    parse_version_path(path, real_path, &version_id_dummy); 
    // 注意：这里我们只关心 inode 对应的文件名，具体的 version_id 留给 read/write 处理

    // 2. 解析父目录和文件名 (使用 real_path)
    char full_path[MAX_FILENAME];
    strncpy(full_path, real_path + 1, MAX_FILENAME - 1); // 去掉开头的 /
    full_path[MAX_FILENAME - 1] = '\0';

    char *dir_name = NULL;
    char *file_name = full_path;
    uint64_t search_in = 0; // 默认根目录

    char *slash = strchr(full_path, '/');
    if (slash) {
        *slash = '\0';
        dir_name = full_path;
        file_name = slash + 1;
        
        search_in = find_entry_in_dir(0, dir_name);
        if (search_in == 0) return 0; // 父目录不存在
    }

    return find_entry_in_dir(search_in, file_name);
}

// =========================================================
// Level 3: FUSE 操作实现 (依赖 Level 1 & 2)
// =========================================================

// 1. 获取文件属性 (getattr)
static int smartfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));

    // 根目录特判
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    // 使用我们强大的查找函数
    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    // 读取属性
    inode_t inode;
    load_inode(inode_id, &inode);

    stbuf->st_mode = inode.mode;
    stbuf->st_nlink = inode.link_count; // <--- 从 Inode 读取真实计数！
    stbuf->st_size = inode.versions[0].file_size;
    stbuf->st_uid = inode.uid;
    stbuf->st_gid = inode.gid;
    stbuf->st_mtime = inode.versions[0].timestamp;
    stbuf->st_blocks = (stbuf->st_size + 511) / 512;

    return 0;
}

// 2. 读取目录 (readdir)
static int smartfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi,
                         enum fuse_readdir_flags flags)
{
    (void) offset; (void) fi; (void) flags;

    uint64_t phys_block;

    // 1. 确定目录的数据块在哪里
    if (strcmp(path, "/") == 0) {
        // 情况 A: 根目录
        phys_block = sb.data_area_start;
    } else {
        // 情况 B: 子目录 (例如 /mydir)
        // 解析路径找到该目录的 Inode
        uint64_t inode_id = resolve_path_to_inode(path);
        if (inode_id == 0) return -ENOENT;

        // 读取 Inode 获取数据块位置
        inode_t inode;
        load_inode(inode_id, &inode);
        
        // 确保它是个目录，不是文件
        if (!S_ISDIR(inode.mode)) return -ENOTDIR;

        phys_block = inode.versions[0].block_list_start_index;
    }

    // 2. 读取目录内容
    char buffer[BLOCK_SIZE];
    smartfs_dir_entry_t *entries = (smartfs_dir_entry_t *)buffer;
    
    lseek(disk_fd, phys_block * BLOCK_SIZE, SEEK_SET);
    read(disk_fd, entries, BLOCK_SIZE);

    // 3. 填入 buffer 让 ls 显示
    int max_entries = BLOCK_SIZE / sizeof(smartfs_dir_entry_t);
    for (int i = 0; i < max_entries; i++) {
        if (entries[i].is_valid) {
            // filler 是 FUSE 的回调，把名字告诉 ls
            filler(buf, entries[i].name, NULL, 0, 0);
        }
    }
    return 0;
}

// 3. 创建文件 (create)
static int smartfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) fi;
    printf("DEBUG: Create %s\n", path);
    fflush(stdout);

    char full_path[MAX_FILENAME];
    strncpy(full_path, path + 1, MAX_FILENAME - 1);
    full_path[MAX_FILENAME - 1] = '\0';
    
    char *dir_name = NULL;
    char *file_name = full_path;
    uint64_t parent_inode_id = 0; 

    char *slash = strchr(full_path, '/');
    if (slash) {
        *slash = '\0';
        dir_name = full_path;
        file_name = slash + 1;
        
        parent_inode_id = find_entry_in_dir(0, dir_name);
        if (parent_inode_id == 0) return -ENOENT;
    }

    if (strlen(file_name) > MAX_FILENAME) return -ENAMETOOLONG;

    uint64_t new_inode_id = allocate_inode();
    if (new_inode_id == 0) return -ENOSPC;

    inode_t new_inode;
    memset(&new_inode, 0, sizeof(inode_t));
    new_inode.link_count = 1;
    new_inode.inode_id = new_inode_id;
    new_inode.mode = mode | S_IFREG; 
    new_inode.uid = getuid();
    new_inode.gid = getgid();
    new_inode.latest_version = 1;
    new_inode.versions[0].version_id = 1;
    new_inode.versions[0].timestamp = time(NULL);
    
    save_inode(&new_inode);

    int ret = add_dir_entry(parent_inode_id, file_name, new_inode_id);
    if (ret != 0) return ret;

    return 0;
}

// 4. 写入文件 (write)
// [修改] 集成快照与CoW的 write
// =========================================================
// 智能写入 (Smart Write Integration) - 模块A+B+C 集成版
// =========================================================
static int smartfs_write(const char *path, const char *buf, size_t size,
                        off_t offset, struct fuse_file_info *fi) 
{
    (void) fi;
    printf("DEBUG: smartfs_write path=%s size=%lu offset=%ld\n", path, size, offset);

    // 1. 找到文件的 Inode
    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    // 2. 调用模块 C 的智能写入接口
    // 注意：smart_write 会自动处理去重、压缩、缓存和物理块分配
    // 它返回的是实际写入的字节数
    int written = smart_write((long)inode_id, (long)offset, buf, (int)size);
    
    if (written < 0) {
        printf("ERROR: smart_write failed with code %d\n", written);
        return -EIO;
    }

    // 3. 更新 Inode 元数据 (这是模块 A 的责任)
    inode_t inode;
    load_inode(inode_id, &inode);

    // 获取当前版本 (通常 smart_write 可能会更新最新版本的数据)
    int v_idx = 0; // 简化逻辑：这里我们总是操作第 0 个版本作为“最新版”
                   // 如果你的 smart_write 逻辑更加复杂（自动创建新版本），这里可能需要调整
    
    // 更新文件大小：如果这次写入超出了原来的范围
    if (offset + written > inode.versions[v_idx].file_size) {
        inode.versions[v_idx].file_size = offset + written;
    }

    // 更新修改时间
    inode.versions[v_idx].timestamp = time(NULL);

    // 4. 保存 Inode
    save_inode(&inode);

    printf("DEBUG: Write success. New size: %lu\n", inode.versions[v_idx].file_size);
    return written;
}

// 5. 读取文件 (read)
// [修改] 支持读取历史版本的 read
// =========================================================
// 智能读取 (Smart Read Integration) - 适配压缩与去重
// =========================================================
static int smartfs_read(const char *path, char *buf, size_t size, 
                       off_t offset, struct fuse_file_info *fi) 
{
    (void) fi;
    printf("DEBUG: smartfs_read path=%s size=%lu offset=%ld\n", path, size, offset);

    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    // 直接调用模块 C 的智能读取
    // 它会自动查找物理块、解压数据、拼接内容
    int bytes_read = smart_read((long)inode_id, (long)offset, buf, (int)size);

    return bytes_read;
}

// 6. 删除文件 (unlink)
// 6. 删除文件 (unlink) - 升级版：支持硬链接计数
static int smartfs_unlink(const char *path) {
    printf("DEBUG: Unlink %s\n", path);
    
    // 1. 解析路径
    char full_path[MAX_FILENAME];
    strncpy(full_path, path + 1, MAX_FILENAME - 1);
    full_path[MAX_FILENAME - 1] = '\0';

    char *dir_name = NULL;
    char *file_name = full_path;
    uint64_t parent_id = 0;

    char *slash = strchr(full_path, '/');
    if (slash) {
        *slash = '\0';
        dir_name = full_path;
        file_name = slash + 1;
        parent_id = find_entry_in_dir(0, dir_name);
        if (parent_id == 0) return -ENOENT;
    }

    // 2. 找到目标 Inode
    uint64_t target_id = find_entry_in_dir(parent_id, file_name);
    if (target_id == 0) return -ENOENT;

    // 3. 从目录中移除条目 (名字没了)
    if (remove_dir_entry(parent_id, file_name) != 0) return -ENOENT;

    // 4. 【核心修改】减少链接计数
    inode_t inode;
    load_inode(target_id, &inode);
    
    if (inode.link_count > 0) {
        inode.link_count--;
    }

    if (inode.link_count == 0) {
        // 只有没人引用了，才真正回收
        printf("DEBUG: Link count is 0, freeing inode %lu\n", target_id);
        free_inode(target_id);
    } else {
        // 还有别的文件名指向它，只保存计数更新
        printf("DEBUG: Link count is %u, keeping inode %lu\n", inode.link_count, target_id);
        save_inode(&inode);
    }
    
    return 0;
}

// 7. 修改大小 (truncate)
static int smartfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void) fi;
    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    inode_t inode;
    load_inode(inode_id, &inode);
    inode.versions[0].file_size = size;
    save_inode(&inode);
    return 0;
}

// 8. 修改时间 (utimens)
static int smartfs_utimens(const char *path, const struct timespec tv[2],
                         struct fuse_file_info *fi)
{
    (void) fi;
    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    inode_t inode;
    load_inode(inode_id, &inode);
    if (tv != NULL) {
        inode.versions[0].timestamp = tv[1].tv_sec;
    } else {
        inode.versions[0].timestamp = time(NULL);
    }
    save_inode(&inode);
    return 0;
}

// 9. 创建目录 (mkdir)
static int smartfs_mkdir(const char *path, mode_t mode) {
    printf("DEBUG: Mkdir %s\n", path);
    
    // 解析路径 (暂只支持一级子目录)
    char full_path[MAX_FILENAME];
    strncpy(full_path, path + 1, MAX_FILENAME - 1);
    full_path[MAX_FILENAME - 1] = '\0';
    
    // 这里简化：假设只能在根目录创建子目录
    // 如果支持多级，这里也需要像 create 一样解析 parent
    
    uint64_t new_inode_id = allocate_inode();
    if (new_inode_id == 0) return -ENOSPC;

    inode_t new_inode;
    memset(&new_inode, 0, sizeof(inode_t));
    new_inode.link_count = 2;
    new_inode.inode_id = new_inode_id;
    new_inode.mode = S_IFDIR | mode;
    new_inode.uid = getuid();
    new_inode.gid = getgid();
    new_inode.latest_version = 1;

    uint64_t new_block = allocate_block();
    if (new_block == 0) return -ENOSPC;

    new_inode.versions[0].block_list_start_index = new_block;
    new_inode.versions[0].block_count = 1;
    new_inode.versions[0].file_size = BLOCK_SIZE;

    // 初始化目录内容 (. 和 ..)
    char buffer[BLOCK_SIZE];
    smartfs_dir_entry_t *entries = (smartfs_dir_entry_t *)buffer;
    memset(entries, 0, BLOCK_SIZE);

    strcpy(entries[0].name, ".");
    entries[0].inode_no = new_inode_id;
    entries[0].is_valid = 1;

    strcpy(entries[1].name, "..");
    entries[1].inode_no = 0; 
    entries[1].is_valid = 1;

    lseek(disk_fd, new_block * BLOCK_SIZE, SEEK_SET);
    write(disk_fd, entries, BLOCK_SIZE);

    save_inode(&new_inode);
    
    // 添加到根目录 (目前简化版)
    add_dir_entry(0, full_path, new_inode_id);
    return 0;
}

// 10. 删除目录 (rmdir)
static int smartfs_rmdir(const char *path) {
    printf("DEBUG: Rmdir %s\n", path);
    const char *dirname = path + 1;

    // 查找目录
    uint64_t inode_id = find_entry_in_dir(0, dirname);
    if (inode_id == 0) return -ENOENT;

    // 检查是否为空
    inode_t inode;
    load_inode(inode_id, &inode);
    if (!S_ISDIR(inode.mode)) return -ENOTDIR;

    uint64_t block_idx = inode.versions[0].block_list_start_index;
    char buffer[BLOCK_SIZE];
    smartfs_dir_entry_t *entries = (smartfs_dir_entry_t *)buffer;
    
    lseek(disk_fd, block_idx * BLOCK_SIZE, SEEK_SET);
    read(disk_fd, entries, BLOCK_SIZE);

    int max_entries = BLOCK_SIZE / sizeof(smartfs_dir_entry_t);
    for (int i = 0; i < max_entries; i++) {
        if (entries[i].is_valid) {
            if (strcmp(entries[i].name, ".") != 0 && 
                strcmp(entries[i].name, "..") != 0) {
                return -ENOTEMPTY;
            }
        }
    }

    remove_dir_entry(0, dirname);
    free_inode(inode_id);
    return 0;
}
static int smartfs_link(const char *from, const char *to) {
    printf("DEBUG: Link %s -> %s\n", from, to);
    
    // 1. 找到源文件的 Inode
    uint64_t inode_id = resolve_path_to_inode(from);
    if (inode_id == 0) return -ENOENT;

    // 2. 解析目标路径 (确定要把名字加到哪个目录)
    char full_path[MAX_FILENAME];
    strncpy(full_path, to + 1, MAX_FILENAME - 1);
    full_path[MAX_FILENAME - 1] = '\0';
    
    char *dir_name = NULL;
    char *file_name = full_path;
    uint64_t parent_inode_id = 0; 

    char *slash = strchr(full_path, '/');
    if (slash) {
        *slash = '\0';
        dir_name = full_path;
        file_name = slash + 1;
        parent_inode_id = find_entry_in_dir(0, dir_name);
        if (parent_inode_id == 0) return -ENOENT;
    }

    // 3. 增加 Inode 计数
    inode_t inode;
    load_inode(inode_id, &inode);
    inode.link_count++;
    save_inode(&inode);

    // 4. 在目录中添加新条目 (指向同一个 ID)
    return add_dir_entry(parent_inode_id, file_name, inode_id);
}
static int smartfs_rename(const char *from, const char *to, unsigned int flags) {
    (void) flags; // 忽略 flags
    printf("DEBUG: Rename %s -> %s\n", from, to);

    // 1. 找到源 Inode
    uint64_t inode_id = resolve_path_to_inode(from);
    if (inode_id == 0) return -ENOENT;

    // 2. 解析目标路径 (新爸爸是谁？)
    char to_path_copy[MAX_FILENAME];
    strncpy(to_path_copy, to + 1, MAX_FILENAME - 1);
    
    char *new_dir_name = NULL;
    char *new_file_name = to_path_copy;
    uint64_t new_parent_id = 0;

    char *slash = strchr(to_path_copy, '/');
    if (slash) {
        *slash = '\0';
        new_dir_name = to_path_copy;
        new_file_name = slash + 1;
        new_parent_id = find_entry_in_dir(0, new_dir_name);
        if (new_parent_id == 0) return -ENOENT;
    }

    // 3. 解析源路径 (旧爸爸是谁？为了删除旧条目)
    char from_path_copy[MAX_FILENAME];
    strncpy(from_path_copy, from + 1, MAX_FILENAME - 1);
    char *old_dir_name = NULL;
    char *old_file_name = from_path_copy;
    uint64_t old_parent_id = 0;

    slash = strchr(from_path_copy, '/');
    if (slash) {
        *slash = '\0';
        old_dir_name = from_path_copy;
        old_file_name = slash + 1;
        old_parent_id = find_entry_in_dir(0, old_dir_name);
    }

    // 4. 添加新条目 (指向同一个 inode_id)
    if (add_dir_entry(new_parent_id, new_file_name, inode_id) != 0) {
        return -ENOSPC;
    }

    // 5. 删除旧条目
    remove_dir_entry(old_parent_id, old_file_name);
    
    return 0;
}
static int smartfs_symlink(const char *to, const char *from) {
    printf("DEBUG: Symlink %s -> %s\n", from, to);
    
    // 1. 创建一个新文件 (复用 create 逻辑，但在 create 里很难传内容)
    // 所以这里我们需要手动走一遍 create 流程，但 mode 设置为 S_IFLNK
    
    // 解析 'from' 路径 (这是软链接文件的名字)
    char full_path[MAX_FILENAME];
    strncpy(full_path, from + 1, MAX_FILENAME - 1);
    char *dir_name = NULL;
    char *file_name = full_path;
    uint64_t parent_id = 0;
    
    char *slash = strchr(full_path, '/');
    if (slash) {
        *slash = '\0';
        dir_name = full_path;
        file_name = slash + 1;
        parent_id = find_entry_in_dir(0, dir_name);
    }

    // 分配 Inode
    uint64_t new_inode_id = allocate_inode();
    if (new_inode_id == 0) return -ENOSPC;

    // 初始化 Inode (关键：S_IFLNK)
    inode_t new_inode;
    memset(&new_inode, 0, sizeof(inode_t));
    new_inode.inode_id = new_inode_id;
    new_inode.mode = S_IFLNK | 0777; // 符号链接通常是 777
    new_inode.uid = getuid();
    new_inode.gid = getgid();
    new_inode.link_count = 1;
    new_inode.versions[0].version_id = 1;
    new_inode.versions[0].timestamp = time(NULL);

    // 分配数据块存路径
    uint64_t block_id = allocate_block();
    new_inode.versions[0].block_list_start_index = block_id;
    new_inode.versions[0].block_count = 1;
    size_t path_len = strlen(to);
    new_inode.versions[0].file_size = path_len;

    // 写入目标路径到数据块
    lseek(disk_fd, block_id * BLOCK_SIZE, SEEK_SET);
    write(disk_fd, to, path_len + 1); // +1 把 \0 也写进去

    save_inode(&new_inode);
    
    // 添加到目录
    return add_dir_entry(parent_id, file_name, new_inode_id);
}
static int smartfs_readlink(const char *path, char *buf, size_t size) {
    printf("DEBUG: Readlink %s\n", path);
    
    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    inode_t inode;
    load_inode(inode_id, &inode);

    if (!S_ISLNK(inode.mode)) return -EINVAL;

    uint64_t block_id = inode.versions[0].block_list_start_index;
    
    // 读取数据块
    char disk_buf[BLOCK_SIZE];
    lseek(disk_fd, block_id * BLOCK_SIZE, SEEK_SET);
    read(disk_fd, disk_buf, BLOCK_SIZE);
    
    // 复制到用户 buffer
    strncpy(buf, disk_buf, size - 1);
    buf[size - 1] = '\0';
    
    return 0;
}
static int smartfs_open(const char *path, struct fuse_file_info *fi) {
    (void) path; // <--- 新增：告诉编译器忽略 path
    (void) fi;   // <--- 新增：告诉编译器忽略 fi
    return 0;
}

static int smartfs_statfs(const char *path, struct statvfs *stbuf) {
    (void) path;
    stbuf->f_bsize = BLOCK_SIZE;
    stbuf->f_blocks = sb.total_blocks;
    stbuf->f_bfree = sb.free_blocks;
    stbuf->f_bavail = sb.free_blocks;
    stbuf->f_namemax = MAX_FILENAME;
    // ==========================================
    // 🔴 新增：每次运行 df 命令时，打印监控报表
    // ==========================================
    printf("\n[Monitor] Triggering Storage Report...\n");
    print_storage_report(); // 调用模块 C 的报表函数
    // ==========================================
    return 0;
}

static const struct fuse_operations smartfs_oper = {
    .getattr  = smartfs_getattr,
    .statfs   = smartfs_statfs,
    .readdir  = smartfs_readdir,
    .create   = smartfs_create,
    .open     = smartfs_open,
    .write    = smartfs_write,
    .read     = smartfs_read,
    .utimens  = smartfs_utimens,
    .unlink   = smartfs_unlink,
    .truncate = smartfs_truncate,
    .mkdir    = smartfs_mkdir,
    .rmdir    = smartfs_rmdir,
    .rename     = smartfs_rename,
    .link       = smartfs_link,
    .symlink    = smartfs_symlink,
    .readlink   = smartfs_readlink,
};

// =========================================================
// Main Functions
// =========================================================

int load_superblock() {
    disk_fd = open(disk_path, O_RDWR);
    if (disk_fd < 0) {
        perror("Error opening disk image");
        return -1;
    }

    lseek(disk_fd, 0, SEEK_SET);
    if (read(disk_fd, &sb, sizeof(super_block_t)) != sizeof(super_block_t)) {
        fprintf(stderr, "Error reading superblock\n");
        return -1;
    }

    if (sb.magic_number != 0x534D4152) {
        fprintf(stderr, "Invalid magic number.\n");
        return -1;
    }

    printf("Superblock loaded successfully!\n");
    return 0;
}

// src/main.c 的最底部

int main(int argc, char *argv[]) {
    // 1. 解析参数 (这部分可能你原来就有)
    int fuse_stat;
    struct smartfs_state *smartfs_data;
    
    // ... 这里可能有你之前的参数解析代码 ...

    // 2. 打开磁盘镜像文件
    disk_fd = open("test.img", O_RDWR);
    if (disk_fd < 0) {
        perror("Cannot open test.img");
        return 1;
    }

    // ==========================================
    // 🔴 必须添加：初始化模块 C (存储引擎)
    // ==========================================
    printf("[Init] Initializing LRU Cache (Capacity: 100 blocks)...\n");
    lru_init(100);  // <--- 加上这一行！分配100个块的缓存空间
    // ==========================================

    // 3. 启动 FUSE
    printf("[Init] Starting SmartFS...\n");
    fuse_stat = fuse_main(argc, argv, &smartfs_oper, smartfs_data);

    return fuse_stat;
}