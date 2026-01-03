#ifndef SMARTFS_STORAGE_H
#define SMARTFS_STORAGE_H

#include <stddef.h> // 为了识别 size_t

// === 模块 C 功能清单 ===

// 1. 计算数据指纹 (来自 dedup.c)
void calculate_sha256(const char *input, size_t len, char *output);

// 2. 智能压缩 (来自 compress.c)
int smart_compress(const char *input, int input_len, char *output);

// 3. 智能解压 (来自 compress.c)
int smart_decompress(const char *input, int input_len, char *output, int max_output_len);

// === 模块 C 核心业务接口 ===

// 智能写入函数 (总指挥)
int smart_write(long inode_id, long offset, const char *data, int len);

// === LRU 缓存接口 ===
void lru_init(int capacity);
void lru_put(int block_id, const char *data); // 只有 ID 和 数据
char* lru_get(int block_id);

// 智能读取函数
int smart_read(long inode_id, long offset, char *buffer, int size);

// === [新增] L3 物理磁盘存储接口 (在这里添加!) ===
int l3_write(int block_id, const char *data, int len);
int l3_read(int block_id, char *buffer, int max_len);

// === 模块 C 监控接口 ===

typedef struct {
    unsigned long total_logical_bytes;   // 用户逻辑大小
    unsigned long bytes_after_dedup;     // 去重后大小
    unsigned long total_physical_bytes;  // 实际物理大小(压缩后)
    unsigned long deduplication_count;   // 触发去重的次数
} StorageStats;

// 打印监控报表
void print_storage_report();

#endif