#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "include/storage.h" // 引入你写的头文件

// 修改 integration_test.c 的 main 函数
int main() {
    printf("========== SmartFS 全链路集成测试 ==========\n");
    
    // 1. 初始化缓存
    lru_init(5);

    const char *data = "This is hot data!";
    int len = strlen(data);

    // 2. 写入数据 (预期：会触发 lru_put)
    printf("\n--- 步骤1: 写入数据 ---\n");
    smart_write(101, 0, data, len);

    // 3. 马上读取 (预期：应该命中缓存，速度极快)
    printf("\n--- 步骤2: 读取数据 ---\n");
    char read_buf[100];
    // 这里的 offset 0 对应我们在 smart_read 里模拟的 block_id 1
    smart_read(101, 0, read_buf, len); 

    return 0;
}