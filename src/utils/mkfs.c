#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>
#include "smartfs_types.h"

#define DISK_SIZE (100 * 1024 * 1024)

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <disk_image_name>\n", argv[0]);
        return 1;
    }

    printf("Formatting disk image: %s ...\n", argv[1]);
    int fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return 1; }

    // 1. 撑大文件到 100MB
    if (ftruncate(fd, DISK_SIZE) != 0) { perror("ftruncate"); close(fd); return 1; }

    // 2. 规划布局
    // [SuperBlock 1块] [InodeBitMap 1块] [BlockBitMap 1块] [Inodes 区域...] [Data 区域...]
    super_block_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic_number = 0x534D4152;
    sb.total_blocks = DISK_SIZE / BLOCK_SIZE;
    sb.inode_bitmap_start = 1; // 第1块存 Inode 位图
    sb.block_bitmap_start = 2; // 第2块存 Block 位图
    sb.inode_area_start   = 3; // 第3块开始存 Inode 表
    sb.data_area_start    = 3 + (1024); // 假设预留1024块给Inode(够存几万个文件了)
    
    // 计算真正的空闲块 (简单减去元数据占用的)
    sb.free_blocks = sb.total_blocks - sb.data_area_start;
    sb.root_inode = 0; // 根目录的 Inode 号定为 0

    // 3. 创建根目录 Inode (Inode #0)
    inode_t root_inode;
    memset(&root_inode, 0, sizeof(inode_t));
    root_inode.inode_id = 0;
    root_inode.mode = S_IFDIR | 0755; // 目录，权限755
    root_inode.uid = getuid();
    root_inode.gid = getgid();
    root_inode.latest_version = 1;
    
    // 根目录使用第0个数据块
    file_version_t *v1 = &root_inode.versions[0];
    v1->version_id = 1;
    v1->block_count = 1;
    v1->file_size = BLOCK_SIZE;
    // 这里的 block_list_start_index 需要复杂的间接寻址，
    // 为了Demo简单，我们暂时约定：
    // 根目录的数据直接存在 data_area_start + 0 这个位置
    
    // 4. 写入根目录的数据块 (包含 . 和 ..)
    smartfs_dir_entry_t entries[2];
    // Entry 1: .
    strcpy(entries[0].name, ".");
    entries[0].inode_no = 0;
    entries[0].is_valid = 1;
    // Entry 2: ..
    strcpy(entries[1].name, "..");
    entries[1].inode_no = 0; // 根目录的上级还是自己
    entries[1].is_valid = 1;

    // 5. 执行写入
    // 写入 SuperBlock
    lseek(fd, 0, SEEK_SET);
    write(fd, &sb, sizeof(sb));

    // 写入 Root Inode (在 Inode 区域的第0个位置)
    off_t inode_offset = sb.inode_area_start * BLOCK_SIZE;
    lseek(fd, inode_offset, SEEK_SET);
    write(fd, &root_inode, sizeof(root_inode));

    // 写入 Root Directory Data (在数据区域的第0个位置)
    off_t data_offset = sb.data_area_start * BLOCK_SIZE;
    lseek(fd, data_offset, SEEK_SET);
    write(fd, entries, sizeof(entries));

    printf("Format success! Root Inode created at block %lu\n", sb.inode_area_start);
    close(fd);
    return 0;
}
