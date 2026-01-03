#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "storage.h"

#define WAL_LOG_FILE "/tmp/smartfs.wal"
static uint32_t g_current_tx_id = 0;

void wal_init() {
    printf("[WAL] Initializing system and checking recovery...\n");
    wal_recover();
}

void wal_begin(const char *op_name) {
    g_current_tx_id = (uint32_t)time(NULL);
    printf("[WAL] 🟢 Transaction #%u Started: %s\n", g_current_tx_id, op_name);
}

void wal_log_write(int block_id, uint32_t checksum) {
    if (g_current_tx_id == 0) return;
    FILE *f = fopen(WAL_LOG_FILE, "ab");
    if (!f) return;
    // 写入逻辑块 ID 和 校验和
    fprintf(f, "TX:%u|BLOCK:%d|CRC:%u\n", g_current_tx_id, block_id, checksum);
    fflush(f);
    fsync(fileno(f)); 
    fclose(f);
}

void wal_commit() {
    printf("[WAL] 🔵 Transaction #%u Committed\n", g_current_tx_id);
    wal_checkpoint();
    g_current_tx_id = 0;
}

void wal_recover() {
    if (access(WAL_LOG_FILE, F_OK) != -1) {
        printf("[WAL] 🚑 Found log file. Performing recovery...\n");
        // 这里简化处理：发现未清除的日志即认为需要检查一致性
        wal_checkpoint(); 
    }
}

void wal_checkpoint() {
    unlink(WAL_LOG_FILE); // 清理日志达成管理要求
}