#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "include/storage.h" // 引入你写的头文件

// 修改 integration_test.c 的 main 函数
int main() {
    printf("========== SmartFS 智能写入逻辑测试 ==========\n");

    const char *data1 = "Hello SmartFS! This is a duplicate test.";
    const char *data2 = "Hello World! This is different data.";
    const char *data3 = "Hello SmartFS! This is a duplicate test."; // 注意：data3 和 data1 一模一样

    // 第一次写入 data1
    smart_write(101, 0, data1, strlen(data1));

    // 第二次写入 data2 (内容不同)
    smart_write(101, 100, data2, strlen(data2));

    // 第三次写入 data3 (内容和 data1 一样)
    // 预期：程序应该检测到重复，不执行写入
    smart_write(101, 200, data3, strlen(data3));

    return 0;
}