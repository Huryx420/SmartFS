#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h" // 包含你的哈希和压缩接口

// 模拟一个简单的“去重数据库”
// 在真实项目中，这里应该用 B+ 树或者数据库文件
typedef struct {
    char hash[65];
    int block_id;
} DedupEntry;

DedupEntry mock_db[100]; // 假设最多存100个块
int db_count = 0;

// 模拟：去数据库里查 hash 是否存在
// 返回 block_id，如果不存在返回 -1
int lookup_fingerprint(const char *hash) {
    for (int i = 0; i < db_count; i++) {
        if (strcmp(mock_db[i].hash, hash) == 0) {
            return mock_db[i].block_id;
        }
    }
    return -1; // 没找到
}

// 模拟：把新 hash 存进数据库
void save_fingerprint(const char *hash, int block_id) {
    if (db_count < 100) {
        strcpy(mock_db[db_count].hash, hash);
        mock_db[db_count].block_id = block_id;
        db_count++;
    }
}


// === 你的核心任务：smart_write ===
int smart_write(long inode_id, long offset, const char *data, int len) {
    printf("\n[SmartWrite] 收到写入请求: Inode=%ld, 大小=%d 字节\n", inode_id, len);

    // 1. 计算指纹 (调用你之前的代码)
    char hash[65];
    calculate_sha256(data, len, hash);
    printf("  -> 数据指纹: %s\n", hash);

    // 2. 查重 (核心逻辑)
    int existing_block = lookup_fingerprint(hash);

    if (existing_block != -1) {
        // === 情况 A: 数据重复了 ===
        printf("  -> 发现重复数据！引用已有块 Block #%d\n", existing_block);
        printf("  -> 节省空间: %d 字节 (未执行磁盘写入)\n", len);
        // 这里实际上应该增加引用计数 (Reference Count)
        return len;
    } 
    
    // === 情况 B: 新数据 ===
    printf("  -> 新数据，准备存储...\n");

    // 3. 压缩 (调用你之前的代码)
    char *compressed_data = malloc(len + 100);
    int c_size = smart_compress(data, len, compressed_data);
    
    // 4. 落盘 (模拟写入物理文件)
    // 在真实代码中，这里是用 fopen/fwrite 把 compressed_data 写进一个叫 data_blocks 的文件
    int new_block_id = db_count + 1; // 简单生成一个 ID
    printf("  -> 写入磁盘: Block #%d (压缩后 %d 字节)\n", new_block_id, c_size);

    // 5. 记录指纹
    save_fingerprint(hash, new_block_id);
    // [新增] 6. 热点数据直接进缓存
    printf("  -> 🔥 将新数据加入 LRU 缓存 (Block #%d)\n", new_block_id);
    lru_put(new_block_id, data, len); // <--- 加这行

    free(compressed_data);
    return len;
}
// === 新增：智能读取逻辑 ===
int smart_read(long inode_id, long offset, char *buffer, int size) {
    printf("\n[SmartRead] 读取请求: Inode=%ld\n", inode_id);

    // 1. 【关键】先查 LRU 缓存
    // 这里我们要模拟算出 block_id (真实场景需查询元数据)
    // 假设：简单映射，block_id 就是 offset / 4096 (简化逻辑)
    int block_id = (int)(offset / 4096) + 1; 

    char *cached_data = lru_get(block_id);
    if (cached_data != NULL) {
        printf("  -> 🚀 缓存命中！直接返回内存数据\n");
        memcpy(buffer, cached_data, size); // 拷贝数据给用户
        return size;
    }

    // 2. 缓存没命中，去“硬盘”读 (模拟)
    printf("  -> 🐢 缓存未命中，正在从磁盘加载...\n");
    
    // (模拟：从磁盘读出来是压缩的数据)
    // 真实场景：fread(disk_file, ...)
    
    // 3. 解压 (调用你的 LZ4 模块)
    // char raw_data[4096];
    // smart_decompress(disk_data, ..., raw_data, ...);
    
    // 4. 【关键】读完记得放入缓存！下次就快了
    // lru_put(block_id, raw_data, size);

    return 0; // 暂时返回0，因为这只是演示流程
}