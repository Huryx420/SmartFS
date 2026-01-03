#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "storage.h"  // 确保能找到这个头文件

#define L3_DATA_FILE "/tmp/smartfs.data"
#define L3_IDX_FILE  "/tmp/smartfs.idx"

// [新增] 全局变量：保存从 main 传来的磁盘 fd
static int main_disk_fd = -1;

// =========================================================
// 🔴 关键修复：必须实现 storage_attach_disk
// =========================================================
void storage_attach_disk(int fd) {
    main_disk_fd = fd;
    printf("[Storage] Main Disk FD %d attached successfully.\n", fd);
}

// 索引条目结构
typedef struct {
    int valid;      // 1=有效
    long offset;    // 数据在 .data 文件中的起始位置
    int length;     // 数据长度 (压缩后的)
} IndexEntry;

// 辅助：获取或创建索引条目
void update_index(int block_id, long offset, int length) {
    FILE *f = fopen(L3_IDX_FILE, "rb+");
    if (!f) f = fopen(L3_IDX_FILE, "wb+"); // 不存在则创建
    
    if (!f) {
        printf("[L3 ERROR] 无法打开索引文件 %s: %s\n", L3_IDX_FILE, strerror(errno));
        return;
    }

    IndexEntry entry;
    entry.valid = 1;
    entry.offset = offset;
    entry.length = length;

    // 定位到第 block_id 个位置
    fseek(f, block_id * sizeof(IndexEntry), SEEK_SET);
    fwrite(&entry, sizeof(IndexEntry), 1, f);
    fclose(f);
}

// === L3 写接口 ===
int l3_write(int block_id, const char *data, int len) {
    // 1. 打开数据文件 (追加模式)
    printf("[L3 DEBUG] 正在尝试打开文件: %s 进行写入...\n", L3_DATA_FILE);
    FILE *f_data = fopen(L3_DATA_FILE, "ab"); 
    if (!f_data)
    {
        printf("[L3 ERROR] 打开数据文件失败 %s: %s\n", L3_DATA_FILE, strerror(errno));
        return -1;
    } 

    // 2. 获取当前写入位置
    fseek(f_data, 0, SEEK_END);
    long offset = ftell(f_data);

    // 3. 写入压缩数据
    fwrite(data, 1, len, f_data);
    fclose(f_data);

    // 4. 更新索引
    update_index(block_id, offset, len);
    
    printf("[L3] 💾 Persisted Block #%d to Disk (Offset: %ld, Len: %d)\n", block_id, offset, len);
    return 0;
}

// === L3 读接口 ===
int l3_read(int block_id, char *buffer, int max_len) {
    // 1. 查索引
    FILE *f_idx = fopen(L3_IDX_FILE, "rb");
    if (!f_idx) return -1;

    IndexEntry entry;
    fseek(f_idx, block_id * sizeof(IndexEntry), SEEK_SET);
    if (fread(&entry, sizeof(IndexEntry), 1, f_idx) < 1 || !entry.valid) {
        printf("[L3] ❌ Block #%d not found in Index.\n", block_id);
        fclose(f_idx);
        return -1;
    }
    fclose(f_idx);

    // 2. 读数据
    FILE *f_data = fopen(L3_DATA_FILE, "rb");
    if (!f_data) return -1;

    fseek(f_data, entry.offset, SEEK_SET);
    
    int read_len = entry.length;
    if (read_len > max_len) read_len = max_len; // 防止溢出
    
    fread(buffer, 1, read_len, f_data);
    fclose(f_data);

    printf("[L3] 💿 Loaded Block #%d from Disk (Size: %d)\n", block_id, read_len);
    return read_len;
}