#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "storage.h"

// 手动声明一下 smart_write.c 里有但头文件里没写的函数
void print_storage_report();

int main() {
    printf("========== Module C 独立单元测试启动 ==========\n\n");

    // 1. 初始化缓存 (容量为 5 个块)
    lru_init(5);

    // -------------------------------------------------
    // 场景 A: 写入第一份数据 (预期: 正常写入 + 压缩)
    // -------------------------------------------------
    printf("\n>>> [测试 1] 写入第一份数据 (Base Data)...\n");
    const char *data1 = "SmartFS is the best file system! Repeat: SmartFS is the best file system!"; 
    // 把这句话重复几遍，让它变长一点，容易看出压缩效果
    char buffer1[4096];
    strcpy(buffer1, data1);
    for(int i=0; i<10; i++) strcat(buffer1, data1); 
    
    // 模拟写入 Inode 100, Offset 0
    smart_write(100, 0, buffer1, strlen(buffer1));

    // -------------------------------------------------
    // 场景 B: 写入完全相同的数据 (预期: 触发去重)
    // -------------------------------------------------
    printf("\n>>> [测试 2] 再次写入相同数据 (Duplicate)...\n");
    // 模拟写入 Inode 101 (不同的文件), 但内容一样
    smart_write(101, 0, buffer1, strlen(buffer1));

    // -------------------------------------------------
    // 场景 C: 写入不同数据 (预期: 新增记录)
    // -------------------------------------------------
    printf("\n>>> [测试 3] 写入新数据 (Unique)...\n");
    const char *data2 = "This is completely different data.";
    smart_write(100, 4096, data2, strlen(data2));

    // -------------------------------------------------
    // 场景 D: 缓存命中测试
    // -------------------------------------------------
    printf("\n>>> [测试 4] 读取刚刚写入的数据 (Cache Hit)...\n");
    char read_buf[4096];
    // 读取刚才的 Block #1 (假设 smart_write 生成的 ID 是 1)
    // 注意：这里的 block_id 计算逻辑要和 smart_read 里的保持一致
    // 我们只是简单调用 smart_read 看看它是否打印 "命中缓存"
    smart_read(100, 0, read_buf, strlen(buffer1));

    // -------------------------------------------------
    // 最终报告
    // -------------------------------------------------
    printf("\n");
    print_storage_report();

    return 0;
}