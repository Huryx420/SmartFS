#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "version_utils.h"

// 辅助函数：判断是否是时间后缀
// 简单规则：以数字开头且以 h/m/d 结尾，或者等于 "yesterday"
static int is_time_suffix(const char *suffix) {
    if (strcmp(suffix, "yesterday") == 0) return 1;
    
    // 检查是否是 2h, 30m 格式
    char *endptr;
    strtol(suffix, &endptr, 10); // 尝试转换数字
    if (endptr == suffix) return 0; // 没有数字开头
    
    // 检查单位
    if (strcmp(endptr, "h") == 0 || strcmp(endptr, "m") == 0 || strcmp(endptr, "d") == 0) {
        return 1;
    }
    return 0;
}

version_query_type_t parse_version_path(const char *path, char *real_path, int *version_id, char *query_str) {
    const char *at_sign = strrchr(path, '@');
    
    if (!at_sign || at_sign == path) {
        strcpy(real_path, path);
        return VER_QUERY_NONE;
    }

    // 计算文件名长度
    long name_len = at_sign - path;
    strncpy(real_path, path, name_len);
    real_path[name_len] = '\0';

    const char *suffix = at_sign + 1; // 跳过 @

    // 1. 尝试解析为版本号 vN
    if (suffix[0] == 'v' && isdigit(suffix[1])) {
        if (sscanf(suffix, "v%d", version_id) == 1) {
            return VER_QUERY_ID;
        }
    }

    // 2. 尝试解析为时间表达式
    if (is_time_suffix(suffix)) {
        if (query_str) strcpy(query_str, suffix);
        return VER_QUERY_TIME;
    }

    // 3. 都不匹配，视为普通文件名包含 @
    strcpy(real_path, path);
    return VER_QUERY_NONE;
}