#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define BLOCK_SIZE 4096

// === L1 Cache (内存链表) ===
typedef struct CacheNode {
    int block_id;
    char *data;
    struct CacheNode *prev, *next;
} CacheNode;

typedef struct {
    int capacity;
    int size;
    CacheNode *head, *tail;
} LRUCache;

static LRUCache *l1_cache = NULL;

// === L2 Cache (MMap 文件) ===
#define L2_CAPACITY 100
#define L2_FILENAME "smartfs_l2.cache"

typedef struct {
    int valid;
    int block_id;
    char data[BLOCK_SIZE];
} L2CacheEntry;

static L2CacheEntry *l2_mmap_ptr = NULL;
static int l2_fd = -1;

void init_l2_cache() {
    l2_fd = open(L2_FILENAME, O_RDWR | O_CREAT, 0666);
    if (l2_fd < 0) return;
    size_t file_size = L2_CAPACITY * sizeof(L2CacheEntry);
    ftruncate(l2_fd, file_size);
    l2_mmap_ptr = (L2CacheEntry *)mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, l2_fd, 0);
}

// L2 写入
void l2_put(int block_id, const char *data) {
    if (!l2_mmap_ptr) return;
    int index = block_id % L2_CAPACITY;
    l2_mmap_ptr[index].valid = 1;
    l2_mmap_ptr[index].block_id = block_id;
    memcpy(l2_mmap_ptr[index].data, data, BLOCK_SIZE);
    msync(&l2_mmap_ptr[index], sizeof(L2CacheEntry), MS_SYNC);
    printf("[L2] ↘️ Evicted to L2: Block #%d (Slot %d)\n", block_id, index);
}

// L2 读取
char* l2_get(int block_id) {
    if (!l2_mmap_ptr) return NULL;
    int index = block_id % L2_CAPACITY;
    if (l2_mmap_ptr[index].valid && l2_mmap_ptr[index].block_id == block_id) {
        printf("[L2] 🚀 L2 Cache Hit: Block #%d\n", block_id);
        return l2_mmap_ptr[index].data;
    }
    return NULL;
}

// === L1 操作实现 ===

void lru_init(int capacity) {
    l1_cache = (LRUCache *)malloc(sizeof(LRUCache));
    l1_cache->capacity = capacity;
    l1_cache->size = 0;
    l1_cache->head = l1_cache->tail = NULL;
    init_l2_cache();
    printf("[Cache] 🧠 L1 Initialized (%d blocks) + L2 MMap Linked.\n", capacity);
}

void lru_remove_node(CacheNode *node) {
    if (node->prev) node->prev->next = node->next;
    else l1_cache->head = node->next;
    if (node->next) node->next->prev = node->prev;
    else l1_cache->tail = node->prev;
}

void lru_add_to_head(CacheNode *node) {
    node->next = l1_cache->head;
    node->prev = NULL;
    if (l1_cache->head) l1_cache->head->prev = node;
    l1_cache->head = node;
    if (!l1_cache->tail) l1_cache->tail = node;
}

// 【关键修正】这里现在严格匹配 storage.h，只有 2 个参数！
void lru_put(int block_id, const char *data) {
    if (!l1_cache) return;

    // 1. 查重更新
    CacheNode *curr = l1_cache->head;
    while (curr) {
        if (curr->block_id == block_id) {
            memcpy(curr->data, data, BLOCK_SIZE);
            lru_remove_node(curr);
            lru_add_to_head(curr);
            return;
        }
        curr = curr->next;
    }

    // 2. 淘汰逻辑 (L1 -> L2)
    if (l1_cache->size >= l1_cache->capacity) {
        CacheNode *tail = l1_cache->tail;
        // 把被淘汰的数据写入 L2
        l2_put(tail->block_id, tail->data); 
        
        lru_remove_node(tail);
        free(tail->data);
        free(tail);
        l1_cache->size--;
    }

    // 3. 新增
    CacheNode *new_node = (CacheNode *)malloc(sizeof(CacheNode));
    new_node->block_id = block_id;
    new_node->data = (char *)malloc(BLOCK_SIZE);
    memcpy(new_node->data, data, BLOCK_SIZE);
    
    lru_add_to_head(new_node);
    l1_cache->size++;
    printf("[L1] 📥 Added to L1: Block #%d\n", block_id);
}

char* lru_get(int block_id) {
    if (!l1_cache) return NULL;
    CacheNode *curr = l1_cache->head;
    while (curr) {
        if (curr->block_id == block_id) {
            printf("[L1] ✅ L1 Hit: Block #%d\n", block_id);
            lru_remove_node(curr);
            lru_add_to_head(curr);
            return curr->data;
        }
        curr = curr->next;
    }
    
    // 查 L2
    char *l2_data = l2_get(block_id);
    if (l2_data) {
        // 如果 L2 找到了，把它“升级”回 L1
        lru_put(block_id, l2_data); 
        // 再次获取（这次一定在 L1 里了）
        return lru_get(block_id);
    }
    return NULL;
}