#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include "smartfs_types.h" // 引用刚才定义的头文件

// 定义磁盘大小为 100MB
#define DISK_SIZE (100 * 1024 * 1024) 

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <disk_image_name>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    printf("Formatting disk image: %s ...\n", filename);

    // 1. 打开/创建文件
    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Failed to open disk image");
        return 1;
    }

    // 2. 设定文件大小为 100MB (相当于把磁盘撑开)
    if (ftruncate(fd, DISK_SIZE) != 0) {
        perror("Failed to set disk size");
        close(fd);
        return 1;
    }

    // 3. 准备超级块 (SuperBlock)
    super_block_t sb;
    memset(&sb, 0, sizeof(sb));

    sb.magic_number = 0x534D4152; // 魔法数 "SMAR" 的十六进制
    sb.total_blocks = DISK_SIZE / BLOCK_SIZE;
    sb.free_blocks  = sb.total_blocks - 1; // 减去超级块本身

    // 简单布局：
    // [超级块 1个] [Inode位图] [数据块位图] [Inode区域] [数据区域]
    // 这里为了简化，我们暂时只写入超级块，后续随着开发再完善布局
    sb.inode_area_start = 1; // 第1块开始存inode（假设）

    // 4. 将超级块写入文件的最开头
    lseek(fd, 0, SEEK_SET);
    if (write(fd, &sb, sizeof(sb)) != sizeof(sb)) {
        perror("Failed to write superblock");
        close(fd);
        return 1;
    }

    printf("Format success! Disk size: %d MB, Total blocks: %lu\n", 
           DISK_SIZE / 1024 / 1024, sb.total_blocks);

    close(fd);
    return 0;
}
