#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "version_utils.h"

int parse_version_path(const char *path, char *real_path, int *version_id) {
    // 1. 查找路径中最后一个 '@' 的位置
    const char *at_sign = strrchr(path, '@');
    
    // 情况A: 没有 @，或者 @ 是第一个字符(不合法)，当作普通文件
    if (!at_sign || at_sign == path) {
        strcpy(real_path, path);
        *version_id = 0;
        return 0;
    }

    // 2. 尝试解析版本号
    // 格式必须严格匹配 @v数字，例如 @v1, @v20
    int id = 0;
    // sscanf 返回匹配成功的个数
    if (sscanf(at_sign, "@v%d", &id) != 1) {
        // 情况B: 有 @ 但后面不是 v+数字 (比如 email@test.com)
        // 当作普通文件名处理
        strcpy(real_path, path);
        *version_id = 0;
        return 0;
    }

    // 情况C: 成功匹配到版本号
    // 计算真实文件名长度 (从开头到 @ 之前)
    long name_len = at_sign - path;
    
    // 复制真实路径
    strncpy(real_path, path, name_len);
    real_path[name_len] = '\0'; // 手动添加字符串结束符
    
    *version_id = id;
    return 1; // 返回 1 表示这是个历史版本
}