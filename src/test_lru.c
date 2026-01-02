#include <stdio.h>
#include "include/storage.h"

int main() {
    printf("=== LRU 缓存淘汰算法测试 ===\n");

    // 1. 初始化容量为 3 的小缓存
    lru_init(3);

    // 2. 填满缓存 (存入 1, 2, 3)
    lru_put(1, "Data1", 5);
    lru_put(2, "Data2", 5);
    lru_put(3, "Data3", 5);

    // 3. 访问一下 1 (这时候 1 变成了最新的，2 变成了最老的)
    lru_get(1);

    // 4. 插入第 4 个数据 (这时候容量满了，应该淘汰最老的 2)
    // 预期输出：淘汰 Block #2
    lru_put(4, "Data4", 5);

    // 5. 验证：尝试获取 2 (应该没有) 和 1 (应该还在)
    char *val2 = lru_get(2);
    if (val2 == NULL) printf("验证通过：Block #2 已被淘汰\n");

    char *val1 = lru_get(1);
    if (val1 != NULL) printf("验证通过：Block #1 依然存在\n");

    return 0;
}