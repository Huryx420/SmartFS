#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "version_mgr.h"

// 模拟打印 Inode 里的所有版本
void dump_versions(inode_t *inode) {
    printf("\n=== Dumping Inode Versions (Total: %d) ===\n", inode->total_versions);
    for (uint32_t i = 0; i < inode->total_versions; i++) {
        file_version_t *v = &inode->versions[i];
        printf("[%d] ID: v%d | Size: %lu | Msg: %s\n", 
               i, v->version_id, v->file_size, v->commit_msg);
    }
    printf("==========================================\n");
}

int main() {
    printf("=== Starting Snapshot Engine Stress Test ===\n");

    // 1. 模拟一个 Inode
    inode_t my_file;
    version_mgr_init_inode(&my_file); // 自动创建 v1

    // 2. 正常增长测试 (创建 v2 - v5)
    char msg[64];
    for (int i = 2; i <= 5; i++) {
        sprintf(msg, "Backup v%d", i);
        // 模拟文件变大：每次增长 100 字节
        // 注意：这里我们修改的是“最新版”，create_snapshot 会继承这个大小
        file_version_t *latest = version_mgr_get_version(&my_file, 0);
        latest->file_size += 100; 
        
        version_mgr_create_snapshot(&my_file, msg);
    }
    
    // 验证 v1 是否存在
    assert(version_mgr_get_version(&my_file, 1) != NULL);
    printf("PASS: Standard growth test.\n");

    // 3. 极限溢出测试 (Overflow Test)
    // 我们疯狂创建版本，直到超过 MAX_VERSIONS (128)
    // 当前已经是 v5 了，我们继续加到 v135
    printf("Running overflow test (up to 135 versions)...\n");
    for (int i = 6; i <= 135; i++) {
        sprintf(msg, "Stress v%d", i);
        version_mgr_create_snapshot(&my_file, msg);
    }

    // 4. 验证结果
    dump_versions(&my_file);

    // 验证 A: 最老版本应该是 v8 (因为 v1-v7 应该被挤出去了)
    // 计算：135 (当前) - 128 (最大容量) + 1 = 8
    file_version_t *oldest = &my_file.versions[0];
    if (oldest->version_id == 8) {
        printf("[SUCCESS] Old versions rotated correctly. First version is now v8.\n");
    } else {
        printf("[FAILED] Expected v8, got v%d\n", oldest->version_id);
    }

    // 验证 B: 最新版本应该是 v135
    if (my_file.latest_version == 135) {
        printf("[SUCCESS] Latest version is v135.\n");
    }

    // 验证 C: 尝试获取已经被删除的 v1
    if (version_mgr_get_version(&my_file, 1) == NULL) {
        printf("[SUCCESS] v1 is correctly gone (return NULL).\n");
    } else {
        printf("[FAILED] v1 still exists!\n");
    }

    return 0;
}