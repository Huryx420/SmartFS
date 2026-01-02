#ifndef SMARTFS_STORAGE_H
#define SMARTFS_STORAGE_H

#include <stddef.h> // 为了识别 size_t

// === 模块 C 功能清单 ===

// 1. 计算数据指纹 (来自 dedup.c)
// input: 原始数据, len: 长度, output: 存放结果的数组(至少65字节)
void calculate_sha256(const char *input, size_t len, char *output);

// 2. 智能压缩 (来自 compress.c)
// 返回压缩后的长度
int smart_compress(const char *input, int input_len, char *output);

// 3. 智能解压 (来自 compress.c)
// 返回解压后的长度
int smart_decompress(const char *input, int input_len, char *output, int max_output_len);
// === 模块 C 核心业务接口 ===

// 智能写入函数 (总指挥)
// 参数: inode_id(这是哪个文件的), offset(写在哪), data(数据), len(长度)
// 返回: 实际写入的字节数
int smart_write(long inode_id, long offset, const char *data, int len);
// === LRU 缓存接口 ===
void lru_init(int capacity);
void lru_put(int block_id, const char *data, int len);
char* lru_get(int block_id);
// 智能读取函数
// 返回：实际读取的字节数
int smart_read(long inode_id, long offset, char *buffer, int size);
#endif