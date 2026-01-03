#ifndef SMARTFS_TYPES_H
#define SMARTFS_TYPES_H

#include <stdint.h>
#include <time.h>
#include <sys/stat.h>

// ---------------------------------------------------------
// 常量定义
// ---------------------------------------------------------
#define BLOCK_SIZE 4096          // 块大小 4KB
#define MAX_FILENAME 255         // 最大文件名长度
#define MAX_VERSIONS 128        // 每个文件最大保留版本数
#define HASH_SIZE 32             // SHA-256 哈希值长度
// 1. 定义扩展属性条目结构
typedef struct {
    char name[32];   // 属性名 (例如 "user.author")
    char value[32];  // 属性值 (例如 "leishen")
    int valid;       // 是否有效
} xattr_entry_t;
// ---------------------------------------------------------
// 1. 超级块 (SuperBlock) - 整个文件系统的“身份证”
// ---------------------------------------------------------
typedef struct {
    uint64_t magic_number;       // 魔数，用于识别是否是我们的FS
    uint64_t total_blocks;       // 总块数
    uint64_t free_blocks;        // 空闲块数
    uint64_t root_inode;         // 根目录的 Inode 编号
    uint64_t block_bitmap_start; // 位图起始位置
    uint64_t inode_area_start;   // Inode区起始位置
    uint64_t data_area_start;
    uint64_t inode_bitmap_start;    // 数据区起始位置
} super_block_t;

// ---------------------------------------------------------
// 2. 数据块索引 (Block Pointer) - 用于去重
// ---------------------------------------------------------
// 在我们的系统中，文件不直接存数据，而是存“数据的指纹”
typedef struct {
    uint8_t  hash[HASH_SIZE];    // 内容哈希 (用于去重)
    uint64_t physical_block_id;  // 实际存储数据的物理块号
    uint32_t ref_count;          // 引用计数 (有多少个文件用了这个块)
    uint32_t compressed_size;    // 压缩后的大小 (如果为0表示未压缩)
} block_index_t;

// ---------------------------------------------------------
// 3. 文件版本 (File Version) - 透明版本管理的核心
// ---------------------------------------------------------
typedef struct {
    uint32_t version_id;
    time_t timestamp;
    uint64_t file_size;
    uint64_t block_list_start_index;
    uint32_t block_count;
    char commit_msg[64];
    int is_pinned; // [新增] 1=锁定(不被自动清理), 0=普通
} file_version_t;

// ---------------------------------------------------------
// 4. Inode (元数据) - 文件的“户口本”
// ---------------------------------------------------------
typedef struct {
    uint64_t inode_id;           // 唯一编号
    mode_t   mode;               // 文件类型和权限 (rwxr-xr-x)
    uint32_t uid;                // 用户ID
    uint32_t gid;                // 组ID
    uint32_t latest_version;     // 当前最新版本号
    uint32_t total_versions;     // 历史版本总数
    uint32_t link_count;
    // 版本链表/数组：指向该文件的所有历史版本
    // 这里简单化处理，实际可能需要单独的元数据区存储版本信息
    file_version_t versions[MAX_VERSIONS]; 
    xattr_entry_t xattrs[4];
} inode_t;
// ---------------------------------------------------------
// 5. 目录项 (Directory Entry)
// ---------------------------------------------------------
// 目录本质上是一个特殊的文件，它的内容就是一堆这种结构体
typedef struct {
    char     name[MAX_FILENAME]; // 文件名 (例如 "homework.doc")
    uint64_t inode_no;           // 对应的 Inode 编号
    uint8_t  is_valid;           // 1=有效，0=已删除
} smartfs_dir_entry_t;
#endif
