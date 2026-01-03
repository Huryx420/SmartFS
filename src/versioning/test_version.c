#include <stdio.h>
#include <string.h>
#include "version_utils.h"

// 辅助打印函数
void test(const char *input) {
    char real_path[256] = {0};
    int v_id = -999;
    
    printf("Testing: '%s' -> ", input);
    
    int res = parse_version_path(input, real_path, &v_id);
    
    if (res == 1) {
        printf("[HISTORY] Path: '%s', Version: %d\n", real_path, v_id);
    } else if (res == 0) {
        printf("[LATEST ] Path: '%s' (No version)\n", real_path);
    } else {
        printf("[ERROR  ] Failed to parse\n");
    }
}

int main() {
    printf("=== Starting Version Module Unit Test ===\n\n");

    // 测试用例 1: 正常版本访问
    test("/home/user/doc.txt@v1");
    
    // 测试用例 2: 更大的版本号
    test("image.png@v1024");
    
    // 测试用例 3: 普通文件 (无 @)
    test("/usr/bin/bash");
    
    // 测试用例 4: 干扰项 (文件名里带 @ 但不是版本)
    test("my_email@google.com");
    
    // 测试用例 5: 干扰项 (带 @v 但后面没数字)
    test("weird_file@v");

    return 0;
}