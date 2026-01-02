#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h"

// 定义缓存容量
#define CACHE_CAPACITY 5

// 定义缓存节点 (双向链表)
typedef struct CacheNode {
    int block_id;             // 键：块ID
    char *data;               // 值：实际数据内容
    int data_len;             // 数据长度
    struct CacheNode *prev;   // 前一个节点
    struct CacheNode *next;   // 后一个节点
} CacheNode;

// 定义缓存管理器
typedef struct {
    int size;                 // 当前存了多少个
    int capacity;             // 最大能存多少
    CacheNode *head;          // 链表头 (最近使用的)
    CacheNode *tail;          // 链表尾 (最久没用的)
} LRUCache;

// 全局唯一的缓存实例
LRUCache *global_cache = NULL;

// 初始化缓存
void lru_init(int capacity) {
    global_cache = (LRUCache *)malloc(sizeof(LRUCache));
    global_cache->size = 0;
    global_cache->capacity = capacity;
    global_cache->head = NULL;
    global_cache->tail = NULL;
    printf("[LRU] 缓存系统初始化完毕，容量: %d\n", capacity);
}

// 内部函数：把节点移动到头部 (表示刚刚用过)
void _move_to_head(CacheNode *node) {
    if (node == global_cache->head) return; // 已经在头了

    // 1. 把自己在原来的位置断开
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    
    // 如果它是尾巴，更新尾巴
    if (node == global_cache->tail) global_cache->tail = node->prev;

    // 2. 插到头部
    node->next = global_cache->head;
    node->prev = NULL;
    
    if (global_cache->head) global_cache->head->prev = node;
    global_cache->head = node;
}

// 核心功能：存入缓存 (Put)
void lru_put(int block_id, const char *data, int len) {
    // 1. 先检查是不是已经存在了 (简化版：遍历查找)
    CacheNode *cur = global_cache->head;
    while (cur) {
        if (cur->block_id == block_id) {
            // 找到了！更新数据并移到头部
            printf("[LRU] 更新热点数据: Block #%d\n", block_id);
            free(cur->data); // 释放旧数据
            cur->data = (char*)malloc(len);
            memcpy(cur->data, data, len);
            cur->data_len = len;
            _move_to_head(cur);
            return;
        }
        cur = cur->next;
    }

    // 2. 如果是新数据
    printf("[LRU] 存入新缓存: Block #%d\n", block_id);
    
    // 检查是不是满了
    if (global_cache->size >= global_cache->capacity) {
        // 满了！淘汰尾部 (最久没用的)
        CacheNode *victim = global_cache->tail;
        printf("[LRU] 🔥 缓存已满，淘汰 Block #%d\n", victim->block_id);
        
        // 从链表移除
        if (victim->prev) victim->prev->next = NULL;
        global_cache->tail = victim->prev;
        
        // 释放内存
        free(victim->data);
        free(victim);
        global_cache->size--;
    }

    // 3. 创建新节点插到头部
    CacheNode *new_node = (CacheNode *)malloc(sizeof(CacheNode));
    new_node->block_id = block_id;
    new_node->data = (char*)malloc(len);
    memcpy(new_node->data, data, len);
    new_node->data_len = len;
    new_node->prev = NULL;
    new_node->next = global_cache->head;

    if (global_cache->head) global_cache->head->prev = new_node;
    global_cache->head = new_node;
    
    if (global_cache->size == 0) global_cache->tail = new_node;
    global_cache->size++;
}

// 核心功能：读取缓存 (Get)
// 返回：数据指针 (如果在缓存里) 或 NULL (如果不在)
char* lru_get(int block_id) {
    CacheNode *cur = global_cache->head;
    while (cur) {
        if (cur->block_id == block_id) {
            printf("[LRU] ✅ 命中缓存: Block #%d\n", block_id);
            _move_to_head(cur); // 关键：读了一次，它就变成最新的了
            return cur->data;
        }
        cur = cur->next;
    }
    printf("[LRU] ❌ 未命中: Block #%d\n", block_id);
    return NULL;
}