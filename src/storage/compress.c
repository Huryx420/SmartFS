#include <lz4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 定义一个缓冲区大小 (4KB)
#define BLOCK_SIZE 4096

// 核心功能：压缩数据
// input: 原始数据
// input_len: 原始长度
// output: 压缩后的数据存放位置 (需要预先分配好内存)
// 返回值: 压缩后的大小 (字节数)。如果返回 0 或负数说明出错。
int smart_compress(const char *input, int input_len, char *output) {
    // LZ4_compress_default 是库函数，专门干这个的
    // 参数含义: 源数据, 目标buffer, 源长度, 目标buffer最大容量
    int compressed_size = LZ4_compress_default(input, output, input_len, BLOCK_SIZE);
    
    if (compressed_size <= 0) {
        printf("压缩失败！可能数据太大了或者不可压缩。\n");
        return -1;
    }
    return compressed_size;
}

// 核心功能：解压数据 (读文件时要用)
int smart_decompress(const char *input, int input_len, char *output, int max_output_len) {
    int decompressed_size = LZ4_decompress_safe(input, output, input_len, max_output_len);
    if (decompressed_size < 0) {
        printf("解压失败！数据可能损坏。\n");
        return -1;
    }
    return decompressed_size;
}

// 测试函数 (Main)
int main() {
    // 1. 准备一段数据
    // 重复的内容越多，压缩效果越好。如果是随机乱码，可能压不动。
    const char *original = "SmartFS is fast! SmartFS is fast! SmartFS is fast! SmartFS is fast! SmartFS is fast!";
    int original_len = strlen(original);
    
    // 2. 准备缓冲区
    char compressed_buffer[BLOCK_SIZE];
    char decompressed_buffer[BLOCK_SIZE];

    printf("=== LZ4 压缩测试 ===\n");
    printf("原始数据: %s\n", original);
    printf("原始大小: %d 字节\n", original_len);

    // 3. 尝试压缩
    int c_size = smart_compress(original, original_len, compressed_buffer);
    if (c_size > 0) {
        printf("压缩后大小: %d 字节 (节省了 %d%% 空间)\n", 
               c_size, (original_len - c_size) * 100 / original_len);
    }

    // 4. 尝试解压
    int d_size = smart_decompress(compressed_buffer, c_size, decompressed_buffer, BLOCK_SIZE);
    if (d_size > 0) {
        decompressed_buffer[d_size] = '\0'; // 补上字符串结尾
        printf("解压后验证: %s\n", decompressed_buffer);
    }

    // 5. 简单的校验
    if (strcmp(original, decompressed_buffer) == 0) {
        printf(">>> 测试通过：数据完美还原！ <<<\n");
    } else {
        printf(">>> 测试失败：数据不一致！ <<<\n");
    }

    return 0;
}