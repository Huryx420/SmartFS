#include "storage.h"
#include <lz4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 4096

// 获取系统负载
double get_system_load() {
    double load[1];
    if (getloadavg(load, 1) != -1) return load[0];
    return 0.0;
}

// 智能跳过检测
int is_already_compressed(const char *data, int len) {
    if (len < 4) return 0; 
    unsigned char *bytes = (unsigned char *)data;
    if (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) return 1;
    if (bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47) return 1;
    if (bytes[0] == 0x50 && bytes[1] == 0x4B && bytes[2] == 0x03 && bytes[3] == 0x04) return 1;
    if (bytes[0] == 0x1F && bytes[1] == 0x8B) return 1;
    return 0; 
}

// 智能压缩
int smart_compress(const char *input, int input_len, char *output) {
    if (is_already_compressed(input, input_len)) {
        printf("[Compress] ⏩ Smart Skip: Detected compressed data, skipping.\n");
        memcpy(output, input, input_len);
        return input_len; 
    }

    double load = get_system_load();
    int c_size;

    if (load > 2.0) {
        printf("[Compress] 🔥 High Load (%.2f)! Switching to FAST mode.\n", load);
        c_size = LZ4_compress_fast(input, output, input_len, BLOCK_SIZE, 5);
    } else {
        c_size = LZ4_compress_default(input, output, input_len, BLOCK_SIZE);
    }

    if (c_size <= 0 || c_size >= input_len) {
        printf("[Compress] ⚠️ Compression inefficient (Load: %.2f), storing raw data.\n", load);
        memcpy(output, input, input_len);
        return input_len;
    }

    printf("[Compress] ✅ Compressed (Load: %.2f): %d -> %d bytes\n", load, input_len, c_size);
    return c_size;
}

// 智能解压
int smart_decompress(const char *input, int input_len, char *output, int max_output_len) {
    int d_size = LZ4_decompress_safe(input, output, input_len, max_output_len);
    if (d_size < 0) {
        // 如果解压失败，可能原本就没压缩（Smart Skip 的数据），直接拷贝
        memcpy(output, input, input_len);
        return input_len;
    }
    return d_size;
}