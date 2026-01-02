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
// 当你在挂载点 ls -l 时，会调用这个函数
static int smartfs_getattr(const char *path, struct stat *stbuf,
                         struct fuse_file_info *fi)
{
    (void) fi;
    int res = 0;

    memset(stbuf, 0, sizeof(struct stat));

    // 如果是根目录 "/"
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755; // 目录，权限 755
        stbuf->st_nlink = 2;
        // 这里我们可以用超级块里的信息来填充
        stbuf->st_size = BLOCK_SIZE; 
    } 
    // 暂时只支持根目录，其他文件以后再加
    else {
        res = -ENOENT; // 文件不存在
    }

    return res;
}
// 3. 读取目录内容 (readdir)
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

static const struct fuse_operations smartfs_oper = {
    .getattr = smartfs_getattr,
    .statfs  = smartfs_statfs, 
    .readdir = smartfs_readdir,// 新增：支持 df 命令查看容量
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
