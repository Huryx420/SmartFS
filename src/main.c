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
#include "storage.h"

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
    char time_str_dummy[32]; // [新增]
    parse_version_path(path, real_path, &version_id_dummy, time_str_dummy);
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

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    // 1. 解析路径看看有没有带版本号
    char real_path[MAX_FILENAME];
    int version_id = 0;
    char time_str[32] = {0}; // [新增]
// [修改] 调用新接口
    version_query_type_t query_type = parse_version_path(path, real_path, &version_id, time_str);

    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;
    printf("DEBUG: getattr path=%s -> resolved_inode=%lu\n", path, inode_id);

    inode_t inode;
    load_inode(inode_id, &inode);

    // 2. 确定我们要读哪个版本
    file_version_t *target_ver = NULL;

    if (query_type == VER_QUERY_ID) {
        // @v1 模式
        target_ver = version_mgr_get_version(&inode, version_id);
    } 
    else if (query_type == VER_QUERY_TIME) {
        // [新增] @2h 模式
        target_ver = version_mgr_find_by_time_str(&inode, time_str);
    } 
    else {
        // 普通模式，取最新
        target_ver = version_mgr_get_version(&inode, 0);
    }

    if (!target_ver) return -ENOENT; // 找不到对应的历史版本

    // 3. 填充属性
    stbuf->st_ino = inode_id;
    stbuf->st_mode = inode.mode;
    stbuf->st_nlink = inode.link_count;
    
    // [关键] 使用目标版本的大小和时间
    stbuf->st_size = target_ver->file_size;   
    stbuf->st_mtime = target_ver->timestamp;
    
    // 如果是只读的历史快照，去掉写权限（可选优化）
    if (query_type != VER_QUERY_NONE) {
        stbuf->st_mode &= ~0222; 
    }

    stbuf->st_uid = inode.uid;
    stbuf->st_gid = inode.gid;
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
    new_inode.total_versions = 1;
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

    // 1. 解析路径找到 Inode ID
    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    // 加载 Inode
    inode_t inode;
    load_inode(inode_id, &inode);

 int current_idx = inode.total_versions - 1;
    if (current_idx < 0) current_idx = 0;

    // 🔴 [优化] 时间间隔策略：防止版本爆炸
    // 只有当文件不为空，且距离上次快照超过 30 秒 (演示用) 时，才创建新快照
    // 实际生产环境可能是 300秒 或 3600秒
    int SNAPSHOT_INTERVAL = 30; 
    
    if (inode.versions[current_idx].file_size > 0) {
        if (version_mgr_should_snapshot(&inode, SNAPSHOT_INTERVAL)) {
            printf("DEBUG: Time strategy triggered (interval > %ds). Creating snapshot...\n", SNAPSHOT_INTERVAL);
            
            // 尝试创建快照，如果被 Pin 满了可能会失败，但不应阻止写入
            int res = version_mgr_create_snapshot(&inode, "Auto-save (Time Triggered)");
            if (res < 0) {
                 printf("WARNING: Snapshot failed (Pinned?), proceeding with write in current version.\n");
            }
        } else {
            printf("DEBUG: Write inside interval (<%ds), updating current version directly.\n", SNAPSHOT_INTERVAL);
        }
    }

    // ---------------------------------------------------------
    // 步骤 A: 准备缓冲区 (Read-Modify-Write)
    // ---------------------------------------------------------
    // 我们需要一个完整的块大小缓冲区，用来合并旧数据和新数据
    char merge_buffer[BLOCK_SIZE];
    memset(merge_buffer, 0, BLOCK_SIZE);

    // 获取当前最新版本 (v0) 的旧数据块 ID
    int latest_idx = inode.total_versions - 1;
    int old_block_id = inode.versions[latest_idx].block_list_start_index;
    int old_size = inode.versions[latest_idx].file_size;

    // 如果旧文件有内容，先读出来！(这是追加写入的关键)
if (old_block_id > 0 && old_size > 0) {
    // ✅ 修正：补上 inode_id，并将 old_block_id 作为第二个参数(offset)传入
    smart_read((long)inode_id, (long)old_block_id, merge_buffer, BLOCK_SIZE);
}

    // ---------------------------------------------------------
    // 步骤 B: 合并数据
    // ---------------------------------------------------------
    // 检查是否越界 (简化版：仅支持单块 4KB)
    if (offset + size > BLOCK_SIZE) {
        return -EFBIG; // 文件太大，超过 demo 限制
    }

    // 将用户的新数据 memcpy 到 merge_buffer 的指定偏移位置
    // 这实现了覆盖或追加：
    // - 覆盖：offset=0, memcpy 覆盖开头
    // - 追加：offset=old_size, memcpy 接在后面
    memcpy(merge_buffer + offset, buf, size);

    // 计算新的文件总大小
    int new_total_size = offset + size;
    if (new_total_size < old_size) new_total_size = old_size; // 如果是中间修改，大小可能不变

    // ---------------------------------------------------------
    // 步骤 C: 写入新块
    // ---------------------------------------------------------
    int physical_block_id = 0;

    // 调用 Module C，写入合并后的完整块
    // 注意：这里传入的是 merge_buffer，大小是 new_total_size (或者 BLOCK_SIZE，取决于你的存储层设计)
    // 为了更精确的去重，我们传入实际有效数据长度
    int written = smart_write((long)inode_id, 0, merge_buffer, new_total_size, &physical_block_id);
    
    if (written < 0) {
        return -EIO;
    }

    // ---------------------------------------------------------
    // 步骤 D: 更新元数据
    // ---------------------------------------------------------
    // 将返回的 Block ID 存入 v0
    if (physical_block_id > 0) {
        inode.versions[latest_idx].block_list_start_index = physical_block_id;
        inode.versions[latest_idx].block_count = 1; 
    }

    // 更新文件大小和时间
    inode.versions[latest_idx].file_size = new_total_size;
    if (latest_idx != current_idx || inode.versions[latest_idx].timestamp == 0) {
    inode.versions[latest_idx].timestamp = time(NULL);
}

    // 保存 Inode
    save_inode(&inode);

    // 告诉 FUSE 我们成功写入了用户请求的 size 字节
    return size;
}
static int smartfs_read(const char *path, char *buf, size_t size, 
                       off_t offset, struct fuse_file_info *fi) 
{
    (void) fi;

    // 1. 解析路径与版本
    char real_path[MAX_FILENAME];
    int version_id = 0; 
    char time_str[32] = {0}; // [新增] 用于接收时间字符串

    version_query_type_t query_type = parse_version_path(path, real_path, &version_id, time_str);

    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    inode_t inode;
    load_inode(inode_id, &inode);
    
    file_version_t *v = NULL;

    if (query_type == VER_QUERY_ID) {
        // 情况 A: 用户指定了 @v1
        v = version_mgr_get_version(&inode, version_id);
    } 
    else if (query_type == VER_QUERY_TIME) {
        // 情况 B: 用户指定了 @2h
        v = version_mgr_find_by_time_str(&inode, time_str);
    } 
    else {
        // 情况 C: 普通读取，获取最新版本
        v = version_mgr_get_version(&inode, 0);
    }
    if (!v) return -ENOENT;

    // =================================================
    // [关键修复 A] 检查 EOF (End Of File)
    // 如果读取位置超过了文件实际大小，直接返回 0
    // =================================================
    if (offset >= v->file_size) {
        return 0;
    }

    // [关键修复 B] 截断读取长度
    // 比如文件剩 5 字节，用户想读 100 字节，那只能给 5 字节
    if (offset + size > v->file_size) {
        size = v->file_size - offset;
    }

    int physical_block_id = v->block_list_start_index;
    if (physical_block_id == 0) return 0;

    // =================================================
    // [关键修复 C] 处理块内偏移 (Block Offset)
    // smart_read 会解压 *整个* 4KB 块，我们不能直接写到用户 buf 里，
    // 否则 offset > 0 时，用户会错误地读到文件开头的数据。
    // =================================================
    
    char temp_block[BLOCK_SIZE]; // 在栈上申请 4KB 临时空间
    
    // 1. 把整个块解压到临时 buffer
   // ✅ 修正：补上 inode_id，并将 physical_block_id 作为第二个参数(offset)传入
    int bytes_in_block = smart_read((long)inode_id, (long)physical_block_id, temp_block, BLOCK_SIZE);
    
    if (bytes_in_block <= 0) return 0;

    // 2. 再次防御性检查
    if (offset >= bytes_in_block) return 0;

    // 3. 计算本次能拷贝多少 (防止跨块读取时溢出，虽然目前逻辑是单块文件)
    size_t copy_len = size;
    if (offset + copy_len > bytes_in_block) {
        copy_len = bytes_in_block - offset;
    }

    // 4. 从临时 buffer 的对应位置 (offset) 拷贝数据给用户
    memcpy(buf, temp_block + offset, copy_len);

    return copy_len;
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
// [修改] 修复后的 smartfs_truncate
static int smartfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void) fi;
    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    inode_t inode;
    load_inode(inode_id, &inode);

    // ---------------------------------------------------------
    // 步骤 1: 找到当前的最新版本 (Append Mode 逻辑)
    // ---------------------------------------------------------
    int current_idx = inode.total_versions - 1;
    if (current_idx < 0) current_idx = 0; // 防御性代码

// =========================================================
    // 🔴 [修改] 在 Truncate 中也加入时间策略
    // =========================================================
    int SNAPSHOT_INTERVAL = 30; // 必须与 write 中的保持一致，建议定义宏

    // 原逻辑：if (inode.versions[current_idx].file_size > 0)
    if (inode.versions[current_idx].file_size > 0) {
        // 只有满足时间间隔，才创建快照
        if (version_mgr_should_snapshot(&inode, SNAPSHOT_INTERVAL)) {
            printf("DEBUG: Truncate triggering snapshot (Time OK)...\n");
            int res = version_mgr_create_snapshot(&inode, "Auto-save before truncate");
            if (res < 0) printf("WARNING: Snapshot failed in truncate.\n");
        } else {
            printf("DEBUG: Truncate skipping snapshot (Time < %ds). Overwriting current version.\n", SNAPSHOT_INTERVAL);
            // 这里不调用 snapshot，直接往下走，就会修改当前最新版本的 size
            // 从而实现了“原地更新”，不产生新版本
        }
    }

    // ---------------------------------------------------------
    // 步骤 3: 重新定位最新版本
    // ---------------------------------------------------------
    // 快照后，total_versions 增加了，最新的槽位变成了下一个
    int new_latest_idx = inode.total_versions - 1;

    // ---------------------------------------------------------
    // 步骤 4: [修复 Issue 2 的元数据部分] 修改新版本的大小
    // ---------------------------------------------------------
    inode.versions[new_latest_idx].file_size = size;
    if (new_latest_idx != current_idx || inode.versions[new_latest_idx].timestamp == 0) {
    inode.versions[new_latest_idx].timestamp = time(NULL);
}
    // 必须同步更新 latest_version 指针和 ID，否则读取时会错乱 (和 write 修复逻辑一致)
    inode.latest_version = inode.total_versions; 
    inode.versions[new_latest_idx].version_id = inode.latest_version;
   if (size == 0) {
        // 1. 告诉系统这个版本现在有 0 个块
        inode.versions[new_latest_idx].block_count = 0;
        
        // 2. 将块列表索引指向无效值 (通常 0 或 -1，视你的实现而定，这里设为 0 安全)
        // 这样 smartfs_write 里的 get_block_id 就找不到旧块了
        inode.versions[new_latest_idx].block_list_start_index = 0;
        
        printf("DEBUG: Truncate to 0 -> Reset block_count to 0.\n");
   }

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
    new_inode.total_versions = 1;
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
 // 如果用户使用了 "w" 模式 (echo > file)，会带上 O_TRUNC
    if ((fi->flags & O_TRUNC) && (fi->flags & (O_WRONLY | O_RDWR))) {
        printf("DEBUG: Open with O_TRUNC detected for %s -> Truncating to 0\n", path);
        // 手动调用你的截断函数
        return smartfs_truncate(path, 0, fi);
    }
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
// 1. 定义 init 函数
static void *smartfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    (void) conn;
    
    // 🔴 关键：在这里开启 use_ino
    cfg->use_ino = 1; 
    
    // 如果你想让内核缓存属性（提高 ls 速度），可以开启这个，但在调试阶段建议关掉
    // cfg->entry_timeout = 0;
    // cfg->attr_timeout = 0;
    // cfg->negative_timeout = 0;

    return NULL;
}
static int smartfs_flush(const char *path, struct fuse_file_info *fi) {
    (void) path; (void) fi;
    // 因为我们的 smartfs_write 是同步写入到 L3 (storage_write) 的，
    // 这里主要任务是确保 OS 把 disk_fd 的数据刷到物理磁盘。
    printf("DEBUG: Flush %s\n", path);
    if (disk_fd > 0) {
        // 调用系统调用 fsync 确保镜像文件落盘
        fsync(disk_fd); 
    }
    return 0;
}
static int smartfs_release(const char *path, struct fuse_file_info *fi) {
    (void) path; (void) fi;
    printf("DEBUG: Release %s\n", path);
    // 如果你有打开的文件句柄表，这里应该释放资源
    // 对于目前的无状态实现，直接返回成功即可
    return 0;
}
static int smartfs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi) {
    (void) path; (void) isdatasync; (void) fi;
    printf("DEBUG: Fsync %s\n", path);
    if (disk_fd > 0) {
        // 强制把 test.img 的所有脏页写入物理磁盘
        return fsync(disk_fd);
    }
    return 0;
}
static int smartfs_setxattr(const char *path, const char *name, const char *value, size_t size, int flags) {
    printf("DEBUG: setxattr path=%s name=%s value=%s\n", path, name, value);

    if (size > 31) return -ERANGE; // 我们的 Demo 限制值最大 32 字节

    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    inode_t inode;
    load_inode(inode_id, &inode);

    // [新增 1] 手动快照接口
    if (strcmp(name, "user.smartfs.snapshot") == 0) {
        char msg[64] = "Manual Snapshot";
        if (size > 0 && size < 63) {
            strncpy(msg, value, size);
            msg[size] = '\0';
        }
        
        int new_vid = version_mgr_create_snapshot(&inode, msg);
        if (new_vid < 0) return -ENOSPC; // 可能由于全被Pin住导致无法创建
        
        save_inode(&inode);
        return 0;
    }

    // [新增 2] 版本 Pin/Unpin 接口
    if (strcmp(name, "user.smartfs.pin") == 0) {
        // value 应该是 "v1", "v2" 这样的字符串
        int v_id = 0;
        if (sscanf(value, "v%d", &v_id) == 1) {
            int status = version_mgr_toggle_pin(&inode, v_id);
            if (status < 0) return -ENOENT;
            
            printf("DEBUG: Version v%d pin status changed to %d\n", v_id, status);
            save_inode(&inode);
            return 0;
        }
        return -EINVAL;
    }

    // 1. 查找是否存在同名属性
    int empty_slot = -1;
    int found_idx = -1;

    for (int i = 0; i < 4; i++) {
        if (inode.xattrs[i].valid) {
            if (strcmp(inode.xattrs[i].name, name) == 0) {
                found_idx = i;
            }
        } else if (empty_slot == -1) {
            empty_slot = i;
        }
    }

    // 处理 flags (XATTR_CREATE, XATTR_REPLACE)
    if (flags == 0x1 && found_idx != -1) return -EEXIST; // XATTR_CREATE (1) 但已存在
    if (flags == 0x2 && found_idx == -1) return -ENODATA; // XATTR_REPLACE (2) 但不存在

    // 确定写入位置
    int target = (found_idx != -1) ? found_idx : empty_slot;
    if (target == -1) return -ENOSPC; // 没有空位了

    // 写入数据
    strncpy(inode.xattrs[target].name, name, 31);
    inode.xattrs[target].name[31] = '\0';
    
    strncpy(inode.xattrs[target].value, value, size);
    inode.xattrs[target].value[size] = '\0'; // 确保 null结尾
    
    inode.xattrs[target].valid = 1;

    save_inode(&inode);
    return 0;
}

// 获取扩展属性 (getxattr)
static int smartfs_getxattr(const char *path, const char *name, char *value, size_t size) {
    printf("DEBUG: getxattr path=%s name=%s\n", path, name);

    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    inode_t inode;
    load_inode(inode_id, &inode);

    // [新增] 特殊 Key: user.smartfs.versions
    // 当用户请求这个 key 时，我们动态生成版本列表返回
    if (strcmp(name, "user.smartfs.versions") == 0) {
        // 如果用户只询问大小 (size==0)，返回一个估算值（比如 4096 字节）
        // 这样用户会分配足够的内存再次调用我们
        if (size == 0) return 4096; 
        
        return version_mgr_list_versions(&inode, value, size);
    }

    for (int i = 0; i < 4; i++) {
        if (inode.xattrs[i].valid && strcmp(inode.xattrs[i].name, name) == 0) {
            int val_len = strlen(inode.xattrs[i].value);
            
            if (size == 0) return val_len; // 用户查询 value 长度
            if (size < val_len) return -ERANGE;

            memcpy(value, inode.xattrs[i].value, val_len);
            return val_len;
        }
    }
    return -ENODATA; // 属性不存在
}

// 列出扩展属性 (listxattr)
static int smartfs_listxattr(const char *path, char *list, size_t size) {
    printf("DEBUG: listxattr path=%s\n", path);

    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    inode_t inode;
    load_inode(inode_id, &inode);

    // 计算总长度
    size_t required_size = 0;
    for (int i = 0; i < 4; i++) {
        if (inode.xattrs[i].valid) {
            required_size += strlen(inode.xattrs[i].name) + 1; // +1 是为了 \0
        }
    }

    if (size == 0) return required_size;
    if (size < required_size) return -ERANGE;

    // 填充列表: name1\0name2\0
    char *ptr = list;
    for (int i = 0; i < 4; i++) {
        if (inode.xattrs[i].valid) {
            strcpy(ptr, inode.xattrs[i].name);
            ptr += strlen(inode.xattrs[i].name) + 1;
        }
    }
    return required_size;
}

// 删除扩展属性 (removexattr)
static int smartfs_removexattr(const char *path, const char *name) {
    printf("DEBUG: removexattr path=%s name=%s\n", path, name);

    uint64_t inode_id = resolve_path_to_inode(path);
    if (inode_id == 0) return -ENOENT;

    inode_t inode;
    load_inode(inode_id, &inode);

    for (int i = 0; i < 4; i++) {
        if (inode.xattrs[i].valid && strcmp(inode.xattrs[i].name, name) == 0) {
            inode.xattrs[i].valid = 0; // 标记失效
            memset(inode.xattrs[i].name, 0, 32);
            save_inode(&inode);
            return 0;
        }
    }
    return -ENODATA;
}
static const struct fuse_operations smartfs_oper = {
    .init       = smartfs_init,
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
    .flush      = smartfs_flush,
    .release    = smartfs_release,
    .fsync      = smartfs_fsync,
    .setxattr   = smartfs_setxattr,
    .getxattr   = smartfs_getxattr,
    .listxattr  = smartfs_listxattr,
    .removexattr= smartfs_removexattr,
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
    if (load_superblock() != 0) {
        fprintf(stderr, "Failed to load superblock. Did you run mkfs?\n");
        return 1;
    }
    printf("[Init] Superblock loaded. Free blocks: %lu\n", sb.free_blocks);
    // [新增] 将 disk_fd 传给模块 C
    storage_attach_disk(disk_fd); // <--- 加上这一行
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