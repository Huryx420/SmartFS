#include <stdio.h>
#include <string.h>
#include <time.h>
#include "version_mgr.h"

// 内部辅助函数：获取最新版本的索引
static int get_latest_index(inode_t *inode) {
    if (inode->total_versions == 0) return -1;
    return inode->total_versions - 1;
}

void version_mgr_init_inode(inode_t *inode) {
    inode->total_versions = 0;
    inode->latest_version = 0;
    memset(inode->versions, 0, sizeof(inode->versions));
    
    // 创建初始版本 v1
    version_mgr_create_snapshot(inode, "Initial Creation");
}

int version_mgr_create_snapshot(inode_t *inode, const char *commit_msg) {
    if (!inode) return -1;

    // 1. 检查是否需要清理旧版本 (Rotation Policy) 
    if (inode->total_versions >= MAX_VERSIONS) {
        printf("[VersionMgr] Warning: Max versions reached (%d). Dropping oldest version.\n", MAX_VERSIONS);
        
        // 暴力左移：versions[1] 覆盖 versions[0], versions[2] 覆盖 versions[1]...
        // 在真实文件系统中，这里可能需要回收 versions[0] 占用的数据块引用计数
        // 但那是 Module C (Dedup) 的工作，Module B 只需要负责挪位置
        for (int i = 0; i < MAX_VERSIONS - 1; i++) {
            inode->versions[i] = inode->versions[i+1];
        }
        
        // 总数减1，因为马上要加1，腾出了最后一个位置
        inode->total_versions--;
    }

    // 2. 确定新旧位置
    int old_idx = get_latest_index(inode);
    int new_idx = inode->total_versions; // 新位置在末尾

    file_version_t *new_ver = &inode->versions[new_idx];
    
    // 3. 增量存储核心：继承 (Inheritance) 
    // 如果存在旧版本，新版本默认继承旧版本的所有状态
    // 这实现了 Copy-on-Write 的第一步：Copy Metadata
    if (old_idx >= 0) {
        file_version_t *old_ver = &inode->versions[old_idx];
        
        // 关键：复制旧版本的文件大小和块索引
        // 这样新版本 v2 在没写入数据前，物理上和 v1 共享完全相同的数据块
        new_ver->file_size = old_ver->file_size;
        new_ver->block_count = old_ver->block_count;
        new_ver->block_list_start_index = old_ver->block_list_start_index;
        
        // 版本号递增
        new_ver->version_id = old_ver->version_id + 1;
    } else {
        // 如果是全新的文件
        new_ver->file_size = 0;
        new_ver->block_count = 0;
        new_ver->block_list_start_index = 0; 
        new_ver->version_id = 1;
    }

    // 4. 更新新版本的独有元数据
    new_ver->timestamp = time(NULL);
    strncpy(new_ver->commit_msg, commit_msg, sizeof(new_ver->commit_msg) - 1);

    // 5. 更新 Inode 全局状态
    inode->total_versions++;
    inode->latest_version = new_ver->version_id;

    printf("[VersionMgr] Snapshot created: v%d (msg: %s) at index %d\n", 
           new_ver->version_id, commit_msg, new_idx);

    return new_ver->version_id;
}

file_version_t* version_mgr_get_version(inode_t *inode, uint32_t version_id) {
    if (!inode || inode->total_versions == 0) return NULL;

    // 如果请求 version_id == 0，返回最新版
    if (version_id == 0) {
        return &inode->versions[inode->total_versions - 1];
    }

    // 线性查找 (因为存在轮转，版本号和数组下标不一定对应)
    // 例如：数组里可能是 [v3, v4, v5]，你要找 v3，下标是 0
    for (uint32_t i = 0; i < inode->total_versions; i++) {
        if (inode->versions[i].version_id == version_id) {
            return &inode->versions[i];
        }
    }

    return NULL; // 没找到 (可能被删除了，或者压根不存在)
}