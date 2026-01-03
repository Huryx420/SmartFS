#ifndef VERSION_MGR_H
#define VERSION_MGR_H

#include "../include/smartfs_types.h"

/**
 * 初始化一个新的文件的版本信息 (在 mkdir/create 时调用)
 * @param inode: 指向需要初始化的 inode
 */
void version_mgr_init_inode(inode_t *inode);

/**
 * 创建新快照 (Create Snapshot)
 * 核心逻辑：
 * 1. 检查版本数是否达到上限 (MAX_VERSIONS)
 * 2. 如果满了，执行左移操作，丢弃最老版本 (Auto Cleanup)
 * 3. 继承上一个版本的元数据 (Copy-on-Write 准备)
 * 4. 分配新的 version_id
 * * @param inode: 操作的文件 Inode
 * @param commit_msg: 版本备注 (如 "Auto-save", "Manual-backup")
 * @return: 新版本的 version_id，失败返回 -1
 */
int version_mgr_create_snapshot(inode_t *inode, const char *commit_msg);

/**
 * 获取指定版本的详细信息 (用于读取历史版本)
 * @param inode: 文件 Inode
 * @param version_id: 想要查找的版本号 (输入 0 表示获取最新版)
 * @return: 指向该版本结构体的指针，未找到返回 NULL
 */
file_version_t* version_mgr_get_version(inode_t *inode, uint32_t version_id);

#endif