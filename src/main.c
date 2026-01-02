#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include "smartfs_types.h" // 引用我们的核心数据结构

// 全局变量：保存磁盘镜像的文件描述符
// (在实际项目中通常放在 fuse_get_context()->private_data 里，这里为了简单先用全局)
static int disk_fd = -1;
static super_block_t sb;
static const char *disk_path = "test.img"; // 暂时硬编码，以后通过参数传入

// ---------------------------------------------------------
// FUSE 操作实现
// ---------------------------------------------------------

// 1. 获取文件属性 (getattr)
static int smartfs_getattr(const char *path, struct stat *stbuf,
                         struct fuse_file_info *fi)
{
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    // 查找文件 (复用刚才的查找逻辑)
    const char *filename = path + 1;
    smartfs_dir_entry_t entries[BLOCK_SIZE / sizeof(smartfs_dir_entry_t)];
    lseek(disk_fd, sb.data_area_start * BLOCK_SIZE, SEEK_SET);
    read(disk_fd, entries, BLOCK_SIZE);
    
    for(int i=0; i<BLOCK_SIZE/sizeof(smartfs_dir_entry_t); i++) {
        if(entries[i].is_valid && strcmp(entries[i].name, filename) == 0) {
            // 找到了！读取 Inode 获取真实大小
            inode_t inode;
            load_inode(entries[i].inode_no, &inode);
            
            stbuf->st_mode = inode.mode;
            stbuf->st_nlink = 1;
            stbuf->st_size = inode.versions[0].file_size; // 显示最新版本大小
            return 0;
        }
    }

    return -ENOENT;
}
// 3. 读取目录内容 (readdir
// 当用户执行 ls 时调用
static int smartfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi,
                         enum fuse_readdir_flags flags)
{
    (void) offset; (void) fi; (void) flags;

    // 目前我们只支持根目录 "/"
    if (strcmp(path, "/") != 0)
        return -ENOENT;

    // 计算根目录数据块在磁盘的位置
    // 注意：这里逻辑要和 mkfs.c 保持一致
    off_t data_offset = sb.data_area_start * BLOCK_SIZE;
    
    // 申请一块内存来读取数据
    smartfs_dir_entry_t *entries = malloc(BLOCK_SIZE);
    
    // 从磁盘读取
    lseek(disk_fd, data_offset, SEEK_SET);
    read(disk_fd, entries, BLOCK_SIZE);

    // 遍历这块数据，找到有效的目录项
    // 假设一个块能存 BLOCK_SIZE / sizeof(entry) 个项
    int max_entries = BLOCK_SIZE / sizeof(smartfs_dir_entry_t);
    
    for (int i = 0; i < max_entries; i++) {
        if (entries[i].is_valid) {
            // filler 是 FUSE 提供的回调函数，用来把文件名填回去
            // 参数：buf, 文件名, stat结构(NULL), 偏移(0)
            filler(buf, entries[i].name, NULL, 0, 0);
        }
    }

    free(entries);
    return 0;
}
// 2. 统计文件系统信息 (statfs)
// 当你运行 df -h 时，会调用这个函数
static int smartfs_statfs(const char *path, struct statvfs *stbuf)
{
    (void) path;
    // 从超级块读取真实信息
    stbuf->f_bsize = BLOCK_SIZE;           // 块大小
    stbuf->f_blocks = sb.total_blocks;     // 总块数
    stbuf->f_bfree = sb.free_blocks;       // 空闲块数
    stbuf->f_bavail = sb.free_blocks;      // 可用块数
    stbuf->f_namemax = MAX_FILENAME;       // 最大文件名长度
    return 0;
}
// =========================================================
// 阶段 1：磁盘管理辅助函数 (粘贴到 load_superblock 上方)
// =========================================================

// 保存超级块 (更新空闲统计)
void save_superblock() {
    lseek(disk_fd, 0, SEEK_SET);
    write(disk_fd, &sb, sizeof(super_block_t));
}

// 保存 Inode 信息
void save_inode(inode_t *inode) {
    off_t offset = sb.inode_area_start * BLOCK_SIZE + inode->inode_id * sizeof(inode_t);
    lseek(disk_fd, offset, SEEK_SET);
    write(disk_fd, inode, sizeof(inode_t));
}

// 读取 Inode 信息
void load_inode(uint64_t inode_id, inode_t *inode) {
    off_t offset = sb.inode_area_start * BLOCK_SIZE + inode_id * sizeof(inode_t);
    lseek(disk_fd, offset, SEEK_SET);
    read(disk_fd, inode, sizeof(inode_t));
}

// 分配一个新的 Inode (简单版：不查位图，直接递增)
// 实际项目需要遍历 inode_bitmap_start 区域
uint64_t allocate_inode() {
    // 简单策略：遍历 Inode 区域，找到第一个 type 为 0 的空闲 Inode
    // 为了性能，这里我们简化：假设系统刚启动，直接找下一个空闲的
    // 正规写法应该读位图。这里为了 Demo 跑通，我们做一个简单暴力搜索
    inode_t node;
    for (uint64_t i = 1; i < 1024; i++) { // 从1开始，0是根目录
        load_inode(i, &node);
        if (node.mode == 0) { // mode为0说明没被使用
            return i;
        }
    }
    return 0; // 满了
}

// 分配一个新的数据块 (返回物理块号)
uint64_t allocate_block() {
    // 同样简化：从数据区开始暴力搜索
    // 正规写法：读取 block_bitmap
    char buf[BLOCK_SIZE];
    uint64_t start_block = sb.data_area_start;
    uint64_t total = sb.total_blocks;
    
    // 这是一个极简的分配器，实际一定要写位图逻辑！
    // 这里我们用一个静态变量记录上次分配的位置，避免每次从头找
    static uint64_t last_alloc = 0;
    if (last_alloc == 0) last_alloc = start_block + 1; // +1 跳过根目录数据块

    if (sb.free_blocks == 0) return 0;

    sb.free_blocks--;
    save_superblock();
    return last_alloc++; // 返回并在下次+1
}

// 在父目录中添加一个文件条目
int add_dir_entry(uint64_t parent_inode_id, const char *name, uint64_t child_inode_id) {
    inode_t parent;
    load_inode(parent_inode_id, &parent);

    // 获取父目录的数据块（根目录默认在 versions[0]）
    // 注意：mkfs 里我们把根目录数据放在 data_area_start + 0
    uint64_t data_block_idx = parent.versions[0].block_list_start_index; 
    // *修正：在mkfs里我们并没有设置block_list_start_index，而是硬编码写到了 data_area_start
    // 为了兼容 mkfs，我们这里暂时硬算：
    uint64_t phys_block = sb.data_area_start; // 根目录固定占第一个数据块

    smartfs_dir_entry_t entries[BLOCK_SIZE / sizeof(smartfs_dir_entry_t)];
    
    // 读出目录块
    off_t offset = phys_block * BLOCK_SIZE;
    lseek(disk_fd, offset, SEEK_SET);
    read(disk_fd, entries, BLOCK_SIZE);

    // 找个空位置塞进去
    int max_entries = BLOCK_SIZE / sizeof(smartfs_dir_entry_t);
    for (int i = 0; i < max_entries; i++) {
        if (!entries[i].is_valid) {
            strncpy(entries[i].name, name, MAX_FILENAME);
            entries[i].inode_no = child_inode_id;
            entries[i].is_valid = 1;
            
            // 写回磁盘
            lseek(disk_fd, offset, SEEK_SET);
            write(disk_fd, entries, BLOCK_SIZE);
            return 0;
        }
    }
    return -ENOSPC; // 目录满了
}
static int smartfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) fi;
    printf("DEBUG: Creating file %s\n", path);

    // 1. 既然是根目录下的文件，去掉路径前的 "/"
    const char *filename = path + 1;
    if (strlen(filename) > MAX_FILENAME) return -ENAMETOOLONG;

    // 2. 分配 Inode
    uint64_t new_inode_id = allocate_inode();
    if (new_inode_id == 0) return -ENOSPC;

    // 3. 初始化 Inode
    inode_t new_inode;
    memset(&new_inode, 0, sizeof(inode_t));
    new_inode.inode_id = new_inode_id;
    new_inode.mode = mode | S_IFREG; // 确保是普通文件
    new_inode.uid = getuid();
    new_inode.gid = getgid();
    new_inode.latest_version = 1;

    // 初始化版本 1
    new_inode.versions[0].version_id = 1;
    new_inode.versions[0].timestamp = time(NULL);
    new_inode.versions[0].file_size = 0;
    new_inode.versions[0].block_count = 0;
    // 暂时没有分配数据块，等 write 时再分配

    // 4. 写入 Inode 到磁盘
    save_inode(&new_inode);

    // 5. 添加到根目录
    add_dir_entry(0, filename, new_inode_id);

    return 0;
}
static int smartfs_open(const char *path, struct fuse_file_info *fi) {
    // 实际应该检查文件是否存在，这里为了 Demo 先简化
    return 0;
}
static int smartfs_write(const char *path, const char *buf, size_t size,
                       off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    printf("DEBUG: Writing to %s size=%lu offset=%lu\n", path, size, offset);

    // 1. 解析路径找 Inode (简化：直接遍历根目录找)
    // 实际应该写一个 find_inode_by_path 的通用函数
    const char *filename = path + 1;
    smartfs_dir_entry_t entries[BLOCK_SIZE / sizeof(smartfs_dir_entry_t)];
    lseek(disk_fd, sb.data_area_start * BLOCK_SIZE, SEEK_SET);
    read(disk_fd, entries, BLOCK_SIZE);

    uint64_t inode_id = 0;
    for(int i=0; i<BLOCK_SIZE/sizeof(smartfs_dir_entry_t); i++) {
        if(entries[i].is_valid && strcmp(entries[i].name, filename) == 0) {
            inode_id = entries[i].inode_no;
            break;
        }
    }
    if (inode_id == 0) return -ENOENT;

    // 2. 读取 Inode
    inode_t inode;
    load_inode(inode_id, &inode);
    file_version_t *v = &inode.versions[0]; // 写入最新版本

    // 3. 分配数据块
    // 简化逻辑：如果文件还是空的，分配第一个块
    // 如果文件很大，这里需要计算 offset 对应第几个块。
    // 为了 Demo，我们假设只写第一个块 (4KB以内)
    if (v->block_count == 0) {
        uint64_t new_block = allocate_block();
        if (new_block == 0) return -ENOSPC;
        v->block_list_start_index = new_block; // 这里暂存物理块号(简化)
        v->block_count = 1;
    }

    // 4. 写入数据
    uint64_t phys_block = v->block_list_start_index;
    off_t disk_offset = phys_block * BLOCK_SIZE + offset;

    lseek(disk_fd, disk_offset, SEEK_SET);
    write(disk_fd, buf, size);

    // 5. 更新文件大小
    if (offset + size > v->file_size) {
        v->file_size = offset + size;
    }
    save_inode(&inode);

    return size; // 必须返回写入的字节数
}
static int smartfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
    (void) fi;

    // 1. 找 Inode (同 write，重复代码以后要封装)
    const char *filename = path + 1;
    smartfs_dir_entry_t entries[BLOCK_SIZE / sizeof(smartfs_dir_entry_t)];
    lseek(disk_fd, sb.data_area_start * BLOCK_SIZE, SEEK_SET);
    read(disk_fd, entries, BLOCK_SIZE);

    uint64_t inode_id = 0;
    for(int i=0; i<BLOCK_SIZE/sizeof(smartfs_dir_entry_t); i++) {
        if(entries[i].is_valid && strcmp(entries[i].name, filename) == 0) {
            inode_id = entries[i].inode_no;
            break;
        }
    }
    if (inode_id == 0) return -ENOENT;

    // 2. 读 Inode
    inode_t inode;
    load_inode(inode_id, &inode);
    file_version_t *v = &inode.versions[0];

    if (offset >= v->file_size) return 0;
    if (offset + size > v->file_size) size = v->file_size - offset;

    // 3. 读数据 (简化：只读第一个块)
    if (v->block_count > 0) {
        uint64_t phys_block = v->block_list_start_index;
        lseek(disk_fd, phys_block * BLOCK_SIZE + offset, SEEK_SET);
        read(disk_fd, buf, size);
    }

    return size;
}
// 修改文件时间 (utimens)
// 消除 touch 命令的报错
static int smartfs_utimens(const char *path, const struct timespec tv[2],
                         struct fuse_file_info *fi)
{
    (void) fi; (void) tv;

    // 1. 查找 Inode (复用之前的查找逻辑)
    const char *filename = path + 1;
    smartfs_dir_entry_t entries[BLOCK_SIZE / sizeof(smartfs_dir_entry_t)];

    // 读取根目录数据块
    lseek(disk_fd, sb.data_area_start * BLOCK_SIZE, SEEK_SET);
    read(disk_fd, entries, BLOCK_SIZE);

    uint64_t inode_id = 0;
    // 遍历查找
    for(int i=0; i<BLOCK_SIZE/sizeof(smartfs_dir_entry_t); i++) {
        if(entries[i].is_valid && strcmp(entries[i].name, filename) == 0) {
            inode_id = entries[i].inode_no;
            break;
        }
    }

    if (inode_id == 0) return -ENOENT;

    // 2. 读取并更新 Inode
    inode_t inode;
    load_inode(inode_id, &inode);

    // 如果传入了时间就用传入的，否则用当前时间
    // tv[0] 是访问时间，tv[1] 是修改时间
    if (tv != NULL) {
        inode.versions[0].timestamp = tv[1].tv_sec;
    } else {
        inode.versions[0].timestamp = time(NULL);
    }

    save_inode(&inode);
    return 0;
}
static const struct fuse_operations smartfs_oper = {
    .getattr = smartfs_getattr,
    .statfs  = smartfs_statfs,
    .readdir = smartfs_readdir,
    .create  = smartfs_create, // 新增
    .open    = smartfs_open,   // 新增
    .write   = smartfs_write,  // 新增
    .read    = smartfs_read, 
    .utimens = smartfs_utimens,  // 新增
};

// ---------------------------------------------------------
// 辅助函数：加载超级块
// ---------------------------------------------------------
int load_superblock() {
    // 1. 打开磁盘镜像
    disk_fd = open(disk_path, O_RDWR);
    if (disk_fd < 0) {
        perror("Error opening disk image");
        return -1;
    }

    // 2. 读取超级块 (位于文件开头)
    lseek(disk_fd, 0, SEEK_SET);
    ssize_t ret = read(disk_fd, &sb, sizeof(super_block_t));
    if (ret != sizeof(super_block_t)) {
        fprintf(stderr, "Error reading superblock\n");
        close(disk_fd);
        return -1;
    }

    // 3. 验证魔数 (Authentication)
    // 0x534D4152 对应 ASCII 的 "SMAR"
    if (sb.magic_number != 0x534D4152) {
        fprintf(stderr, "Invalid magic number: 0x%lx. Is this a SmartFS image?\n", sb.magic_number);
        close(disk_fd);
        return -1;
    }

    printf("Superblock loaded successfully!\n");
    printf("Total Blocks: %lu, Free Blocks: %lu\n", sb.total_blocks, sb.free_blocks);
    return 0;
}

int main(int argc, char *argv[])
{
    // 在启动 FUSE 之前，先尝试加载磁盘
    // 注意：这里我们假设 test.img 就在运行目录下
    if (load_superblock() != 0) {
        return 1;
    }

    return fuse_main(argc, argv, &smartfs_oper, NULL);
}
