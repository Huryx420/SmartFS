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

#endif