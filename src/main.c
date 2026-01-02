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
    stbuf->st_nlink = 1;
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
static int smartfs_write(const char *path, const char *buf, size_t size,
                       off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    
    // 1. 检查是否试图写入历史版本 (不允许)
    char real_path[MAX_FILENAME];
    int version_req;
    if (parse_version_path(path, real_path, &version_req) == 1) {
        return -EROFS; // Read-only file system
    }

    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    inode_t inode;
    load_inode(inode_id, &inode);

    // =========== [Mod B 集成点] ===========
    // 2. 在写入前，创建快照 (保存当前状态为历史)
    // 策略：每次写入都生成新版本 (为了演示效果)
    version_mgr_create_snapshot(&inode, "Auto-save on write");
    
    // 3. 获取最新的版本 (这将是我们即将写入的版本)
    file_version_t *v = version_mgr_get_version(&inode, 0);
    // =====================================

    // 4. [Copy-on-Write] 写时复制核心逻辑
    // 为了不覆盖旧版本的数据块，我们必须为新版本分配一个新块
    // (注意：这里简化处理，假设文件只有1个块。真实系统需要处理块列表)
    
    uint64_t old_block = v->block_list_start_index;
    uint64_t new_block = allocate_block(); // 申请新物理块
    if (new_block == 0) return -ENOSPC;

    // 如果旧块有数据，先搬运过来 (继承数据)
    if (old_block != 0) {
        char temp_buf[BLOCK_SIZE];
        lseek(disk_fd, old_block * BLOCK_SIZE, SEEK_SET);
        read(disk_fd, temp_buf, BLOCK_SIZE);
        
        lseek(disk_fd, new_block * BLOCK_SIZE, SEEK_SET);
        write(disk_fd, temp_buf, BLOCK_SIZE);
    }

    // 更新新版本指向新块
    v->block_list_start_index = new_block;
    v->block_count = 1;

    // 5. 执行真正的写入 (写入新块)
    off_t disk_offset = new_block * BLOCK_SIZE + offset;
    lseek(disk_fd, disk_offset, SEEK_SET);
    write(disk_fd, buf, size);

    // 6. 更新文件大小
    if (offset + size > v->file_size) {
        v->file_size = offset + size;
    }
    
    // 7. 保存 Inode (必须保存，否则版本信息丢失)
    save_inode(&inode);

    return size;
}

// 5. 读取文件 (read)
// [修改] 支持读取历史版本的 read
static int smartfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
    (void) fi;

    // 1. 解析路径和版本
    char real_path[MAX_FILENAME];
    int version_id = 0;
    parse_version_path(path, real_path, &version_id);

    uint64_t inode_id = resolve_path_to_inode(path); // 这里的 path 可以带 @，上面那个函数已经能处理了
    if (inode_id == 0) return -ENOENT;

    // 2. 读取 Inode
    inode_t inode;
    load_inode(inode_id, &inode);

    // 3. [关键] 获取指定版本的指针
    file_version_t *v = version_mgr_get_version(&inode, version_id);
    if (!v) return -ENOENT; // 版本不存在

    // 4. 读取数据
    if (offset >= v->file_size) return 0;
    if (offset + size > v->file_size) size = v->file_size - offset;

    if (v->block_count > 0) {
        uint64_t phys_block = v->block_list_start_index;
        lseek(disk_fd, phys_block * BLOCK_SIZE + offset, SEEK_SET);
        read(disk_fd, buf, size);
    }

    return size;
}

// 6. 删除文件 (unlink)
static int smartfs_unlink(const char *path) {
    printf("DEBUG: Unlink %s\n", path);
    
    // 解析路径
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

    // 查找目标
    uint64_t target_id = find_entry_in_dir(parent_id, file_name);
    if (target_id == 0) return -ENOENT;

    // 从目录移除
    if (remove_dir_entry(parent_id, file_name) != 0) return -ENOENT;

    // 回收
    free_inode(target_id);
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

int main(int argc, char *argv[])
{
    if (load_superblock() != 0) {
        return 1;
    }
    return fuse_main(argc, argv, &smartfs_oper, NULL);
}