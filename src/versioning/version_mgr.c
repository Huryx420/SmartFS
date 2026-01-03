#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "version_mgr.h"
#include <errno.h>
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

static time_t parse_time_str(const char *str) {
    time_t now = time(NULL);
    
    if (strcmp(str, "yesterday") == 0) {
        return now - 24 * 3600;
    }

    int val = atoi(str);
    char unit = str[strlen(str) - 1];

    switch (unit) {
        case 'h': return now - val * 3600;
        case 'm': return now - val * 60;
        case 'd': return now - val * 24 * 3600;
        default: return now;
    }
}


file_version_t* version_mgr_find_by_time_str(inode_t *inode, const char *time_str) {
    if (!inode || inode->total_versions == 0) return NULL;

    time_t target_time = parse_time_str(time_str);
    
    // 遍历版本链，找到 target_time 之前(或相等)的那个最新版本
    // 假设版本是按时间顺序存储的 (versions[0] 最旧, versions[total-1] 最新)
    // 我们要找最后一个 timestamp <= target_time 的版本
    
    file_version_t *best_match = NULL;

    // 因为数组可能会由于 rotation 变得无序(逻辑上有序)，建议按 version_id 遍历或者假设物理顺序
    // 简化起见，我们倒序遍历数组（通常是新的在后面）
    for (int i = inode->total_versions - 1; i >= 0; i--) {
        if (inode->versions[i].timestamp <= target_time) {
            best_match = &inode->versions[i];
            break; // 找到了最近的一个过去版本
        }
    }

    // 如果所有版本都比 target_time 晚（比如查找1年前，但文件是今天建的）
    // 返回最老的那个版本 (versions[0])，或者返回 NULL (文件当时不存在)
    // 这里我们策略是：如果当时文件不存在，返回 NULL
    return best_match;
}

size_t version_mgr_list_versions(inode_t *inode, char *buf, size_t size) {
    if (!inode) return 0;
    
    char line[256];
    size_t total_len = 0;
    
    // 如果 buffer 太小无法写入，这只是一个简单的 demo 实现
    // 实际应先计算长度
    
    for (int i = 0; i < inode->total_versions; i++) {
        file_version_t *v = &inode->versions[i];
        
        // 格式化时间
        struct tm *tm_info = localtime(&v->timestamp);
        char time_buf[30];
        strftime(time_buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);
        
        int len = snprintf(line, sizeof(line), "v%d%s | %s | %s | %lu bytes\n", 
                   v->version_id, 
                   v->is_pinned ? "[PIN]" : "", // <--- 新增：如果有锁，显示 [PIN]
                   time_buf, v->commit_msg, v->file_size);
        
        if (total_len + len < size) {
            strcpy(buf + total_len, line);
            total_len += len;
        } else {
            break; // 缓冲区满了
        }
    }
    return total_len;
}

int version_mgr_create_snapshot(inode_t *inode, const char *commit_msg) {
    if (!inode) return -1;

    // --- [修改] 升级后的清理策略 (Rotation Policy) ---
    if (inode->total_versions >= MAX_VERSIONS) {
        printf("[VersionMgr] Max versions reached. Trying to make room...\n");
        
        // 寻找一个可以删除的受害者（从最老的开始找）
        int victim_idx = -1;
        for (int i = 0; i < inode->total_versions - 1; i++) { // 保留最新的一个不删
            if (inode->versions[i].is_pinned == 0) {
                victim_idx = i;
                break;
            }
        }

        if (victim_idx != -1) {
            // 找到了受害者，移动数组覆盖它
            printf("  Dropping version v%d (index %d)\n", inode->versions[victim_idx].version_id, victim_idx);
            for (int i = victim_idx; i < inode->total_versions - 1; i++) {
                inode->versions[i] = inode->versions[i+1];
            }
            inode->total_versions--;
        } else {
            // 所有历史版本都被 Pin 住了！无法创建新快照
            // 策略：强制删除最老的，或者返回错误。这里简单起见，打印错误并返回
            printf("[VersionMgr] Error: All versions are pinned! Cannot snapshot.\n");
            return -1;
        }
    }
    // 2. 确定新旧位置
    int old_idx = get_latest_index(inode);
    int new_idx = inode->total_versions; // 新位置在末尾

    file_version_t *new_ver = &inode->versions[new_idx];
    new_ver->is_pinned = 0; // 初始化
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

int version_mgr_toggle_pin(inode_t *inode, int version_id) {
    file_version_t *v = version_mgr_get_version(inode, version_id);
    if (!v) return -ENOENT;
    
    v->is_pinned = !v->is_pinned; // 切换 0 <-> 1
    return v->is_pinned; // 返回新的状态
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

// 检查是否满足时间间隔策略
// interval_seconds: 最小间隔秒数 (例如 60秒)
int version_mgr_should_snapshot(inode_t *inode, int interval_seconds) {
    if (!inode || inode->total_versions == 0) return 1; // 没版本，肯定要快照

    // 获取最新版本
    int latest_idx = inode->total_versions - 1;
    time_t last_time = inode->versions[latest_idx].timestamp;
    time_t now = time(NULL);

    // 如果 (当前时间 - 上次时间) < 间隔，则不快照
    if ((now - last_time) < interval_seconds) {
        return 0; 
    }
    return 1;
}