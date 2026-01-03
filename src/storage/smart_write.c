#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h" 

#define MAX_BLOCKS 1024   
StorageStats global_stats = {0, 0, 0, 0};
#define VIRTUAL_DISK_CAPACITY (100 * 1024 * 1024)

typedef struct { char hash[65]; int block_id; } DedupEntry;
DedupEntry mock_db[MAX_BLOCKS]; 
int db_count = 0;
int ref_counts[MAX_BLOCKS];

int lookup_fingerprint(const char *hash) {
    for (int i = 0; i < db_count; i++) if (strcmp(mock_db[i].hash, hash) == 0) return mock_db[i].block_id;
    return -1; 
}

void save_fingerprint(const char *hash, int block_id) {
    if (db_count < MAX_BLOCKS) {
        strcpy(mock_db[db_count].hash, hash);
        mock_db[db_count].block_id = block_id;
        db_count++;
    }
}

// === 核心写入 ===
int smart_write(long inode_id, long offset, const char *data, int len) {
    global_stats.total_logical_bytes += len;
    printf("\n[SmartWrite] 收到写入请求: Inode=%ld, 大小=%d 字节\n", inode_id, len);

    char hash[65];
    calculate_sha256(data, len, hash);

    int existing_block = lookup_fingerprint(hash);
    if (existing_block != -1) {
        printf("  -> 发现重复数据！引用已有块 Block #%d\n", existing_block);
        global_stats.deduplication_count++;
        ref_counts[existing_block]++;
        return len;
    } 
    
    printf("  -> 新数据，准备存储...\n");
    // [安全优化] 申请时清零，防止脏数据
    char *compressed_data = malloc(4096 + 100);
    memset(compressed_data, 0, 4096 + 100); 
    int c_size = smart_compress(data, len, compressed_data);

    global_stats.bytes_after_dedup += len;
    global_stats.total_physical_bytes += c_size;
    
    int new_block_id = (int)inode_id; 
    if (inode_id < 100) new_block_id = db_count + 1; 

    ref_counts[new_block_id] = 1;

    // === [L3] 写入物理磁盘 ===
    l3_write(new_block_id, compressed_data, c_size);

    save_fingerprint(hash, new_block_id);

    printf("  -> 🔥 将新数据加入 LRU 缓存 (Block #%d)\n", new_block_id);
    lru_put(new_block_id, compressed_data); 

    free(compressed_data);
    return len;
}

// === 核心读取 (修复了 L3 解压长度问题) ===
int smart_read(long inode_id, long offset, char *buffer, int buf_len) {
    printf("\n[SmartRead] 读取请求: Inode=%ld\n", inode_id);
    int block_id = (int)inode_id;

    // 1. 查 L1/L2 缓存
    char *compressed_data = lru_get(block_id);
    char *temp_buf = NULL; 
    
    // [关键修复] 定义解压时的输入长度
    // 如果是缓存命中，默认是 4096 (因为缓存块固定大小)
    // 如果是 L3 命中，我们会更新这个值为实际读取长度
    int input_len = 4096; 

    // 2. 缓存未命中，查 L3 磁盘
    if (compressed_data == NULL) {
        printf("  -> 🐢 缓存未命中，查询 L3 物理磁盘...\n");
        temp_buf = malloc(4096 + 100);
        // [安全优化] 先清零，避免脏数据干扰 LZ4
        memset(temp_buf, 0, 4096 + 100);
        
        int l3_len = l3_read(block_id, temp_buf, 4096);
        
        if (l3_len > 0) {
            compressed_data = temp_buf; 
            
            // [关键修复] 告诉解压器：只解压这 l3_len 个字节，后面的别管！
            input_len = l3_len;

            // 回填缓存
            printf("  -> 🔥 触发回写机制: 将数据重载入 L1 缓存\n");
            lru_put(block_id, compressed_data);
        } else {
            printf("  -> ❌ L3 也找不到该数据 (IO Error or Not Found)\n");
            free(temp_buf);
            return -1;
        }
    }

    // 3. 解压 (使用正确的 input_len)
    int decompressed_size = smart_decompress(
        compressed_data, input_len, buffer, buf_len
    );

    if (temp_buf) free(temp_buf); 

    if (decompressed_size > 0) {
        printf("  -> ✅ 读取成功 (大小: %d 字节)\n", decompressed_size);
        return decompressed_size;
    } else {
        printf("  -> ⚠️ 解压失败! (InputLen=%d)\n", input_len);
        return -1;
    }
}

void print_storage_report() {
    printf("\n📊 ========== SmartFS 存储效率监控报告 ==========\n");
    printf("用户写入总量: %lu 字节\n", global_stats.total_logical_bytes);
    printf("实际占用磁盘: %lu 字节\n", global_stats.total_physical_bytes);
    printf("==================================================\n");
}